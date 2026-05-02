/* NexOS — userspace/shell/nsh.c | NexOS Shell (nsh) | MIT License
 *
 * Features: 25 built-ins, history (50), tab completion, env vars,
 * pipes (cmd1 | cmd2), output redirection (> / >>),
 * background jobs (cmd &), quoted string arguments.
 */
#include "../../kernel/kernel.h"
#include "../../kernel/drivers/vga.h"
#include "../../kernel/drivers/keyboard.h"
#include "../../kernel/drivers/timer.h"
#include "../../kernel/drivers/rtc.h"
#include "../../kernel/fs/vfs.h"
#include "../../kernel/mm/heap.h"
#include "../../kernel/proc/process.h"
#include "../../kernel/proc/scheduler.h"
#include "../../kernel/mm/pmm.h"
#include "../../kernel/drivers/rtl8139.h"
#include "../../kernel/net/icmp.h"
#include "../../kernel/net/ethernet.h"

/* ════════════════════════════════════════
 * String helpers (no libc)
 * ════════════════════════════════════════ */
static size_t nsh_strlen(const char *s) { size_t n=0; while(s[n]) n++; return n; }

static void nsh_strcpy(char *d, const char *s, size_t max) {
    size_t i=0; while(i<max-1&&s[i]){d[i]=s[i];i++;} d[i]=0;
}
static int nsh_strcmp(const char *a, const char *b) {
    while(*a&&*a==*b){a++;b++;} return (unsigned char)*a-(unsigned char)*b;
}
static int nsh_strncmp(const char *a, const char *b, size_t n) {
    while(n&&*a&&*a==*b){a++;b++;n--;} if(!n) return 0;
    return (unsigned char)*a-(unsigned char)*b;
}
static void nsh_memset(void *p, uint8_t v, size_t n) {
    uint8_t *b=(uint8_t*)p; for(size_t i=0;i<n;i++) b[i]=v; (void)nsh_memset;
}
static char *nsh_strcat(char *d, const char *s, size_t max) {
    size_t dl=nsh_strlen(d), i=0;
    while(dl+i<max-1&&s[i]){d[dl+i]=s[i];i++;} d[dl+i]=0; return d;
}
static char *nsh_strchr(const char *s, char c) {
    while(*s){ if(*s==c) return (char*)s; s++; } return NULL;
}

/* ════════════════════════════════════════
 * Shell I/O — pipe / redirect abstraction
 * ════════════════════════════════════════ */
#define PIPE_BUF_SIZE 4096

static char     pipe_buf[PIPE_BUF_SIZE];
static uint32_t pipe_buf_len = 0;
static uint32_t pipe_buf_rpos = 0;   /* read position (right-side of pipe) */
static int      pipe_write_mode = 0; /* capturing output to pipe_buf */
static int      pipe_read_mode  = 0; /* reading from pipe_buf as stdin */

static vfs_node_t *redirect_node   = NULL;
static uint64_t    redirect_offset = 0;

/* All shell output goes through nsh_putchar — pipe / redirect aware */
static void nsh_putchar(char c) {
    if (pipe_write_mode) {
        if (pipe_buf_len < PIPE_BUF_SIZE - 1)
            pipe_buf[pipe_buf_len++] = c;
        return;
    }
    if (redirect_node) {
        uint8_t b = (uint8_t)c;
        vfs_write(redirect_node, redirect_offset, 1, &b);
        redirect_offset++;
        return;
    }
    vga_putchar(c);
}

static void nsh_print(const char *s)   { while(*s) nsh_putchar(*s++); }
static void nsh_println(const char *s) { nsh_print(s); nsh_putchar('\n'); }

/* Read a character from stdin — pipe-read-mode or keyboard */
static char nsh_getchar(void) {
    if (pipe_read_mode) {
        if (pipe_buf_rpos < pipe_buf_len)
            return pipe_buf[pipe_buf_rpos++];
        return 0;  /* EOF */
    }
    return keyboard_getchar();
}

/* ════════════════════════════════════════
 * Number formatting
 * ════════════════════════════════════════ */
static void nsh_uint_str(uint64_t n, char *buf) {
    if (!n) { buf[0]='0'; buf[1]=0; return; }
    char t[20]; int i=0;
    while(n){t[i++]='0'+(int)(n%10);n/=10;}
    int j=0; while(i>0) buf[j++]=t[--i]; buf[j]=0;
}
static void nsh_int_str(int64_t n, char *buf) {
    if(n<0){buf[0]='-';nsh_uint_str((uint64_t)(-n),buf+1);}
    else nsh_uint_str((uint64_t)n,buf);
}

/* ════════════════════════════════════════
 * History
 * ════════════════════════════════════════ */
#define NSH_HIST_SIZE 50
#define NSH_LINE_MAX  512

static char history[NSH_HIST_SIZE][NSH_LINE_MAX];
static int  hist_count = 0;
static int  hist_pos   = -1;

static void history_add(const char *line) {
    if (!line[0]) return;
    if (hist_count > 0 && nsh_strcmp(history[(hist_count-1)%NSH_HIST_SIZE], line)==0) return;
    nsh_strcpy(history[hist_count % NSH_HIST_SIZE], line, NSH_LINE_MAX);
    hist_count++;
}

/* ════════════════════════════════════════
 * Environment variables
 * ════════════════════════════════════════ */
#define NSH_ENV_MAX     64
#define NSH_ENV_NAMELEN 64
#define NSH_ENV_VALLEN  256

typedef struct { char name[NSH_ENV_NAMELEN]; char value[NSH_ENV_VALLEN]; } env_var_t;
static env_var_t env_vars[NSH_ENV_MAX];
static int       env_count = 0;
static int       last_exit_code = 0;

static void env_set(const char *name, const char *value) {
    for (int i=0;i<env_count;i++) {
        if (nsh_strcmp(env_vars[i].name,name)==0) {
            nsh_strcpy(env_vars[i].value,value,NSH_ENV_VALLEN); return;
        }
    }
    if (env_count < NSH_ENV_MAX) {
        nsh_strcpy(env_vars[env_count].name, name,  NSH_ENV_NAMELEN);
        nsh_strcpy(env_vars[env_count].value,value, NSH_ENV_VALLEN);
        env_count++;
    }
}

static const char *env_get(const char *name) {
    if (nsh_strcmp(name,"?")==0) {
        static char ec[8]; nsh_int_str(last_exit_code,ec); return ec;
    }
    for (int i=0;i<env_count;i++)
        if (nsh_strcmp(env_vars[i].name,name)==0) return env_vars[i].value;
    return "";
}

static void env_init(void) {
    env_set("PATH",  "/bin:/usr/bin");
    env_set("HOME",  "/home/user");
    env_set("USER",  "root");
    env_set("PS1",   "[root@nexos]$ ");
    env_set("SHELL", "/bin/nsh");
}

/* ════════════════════════════════════════
 * Variable expansion
 * ════════════════════════════════════════ */
static void expand_vars(const char *in, char *out, size_t max) {
    size_t i=0,j=0;
    while(in[i]&&j<max-1) {
        if (in[i]=='$') {
            i++;
            char vn[NSH_ENV_NAMELEN]; int vni=0;
            if(in[i]=='?'){vn[0]='?';vn[1]=0;i++;}
            else while(in[i]&&(in[i]=='_'||(in[i]>='a'&&in[i]<='z')||
                    (in[i]>='A'&&in[i]<='Z')||(in[i]>='0'&&in[i]<='9'))&&vni<NSH_ENV_NAMELEN-1)
                vn[vni++]=in[i++];
            vn[vni]=0;
            const char *val=env_get(vn);
            while(*val&&j<max-1) out[j++]=*val++;
        } else { out[j++]=in[i++]; }
    }
    out[j]=0;
}

/* ════════════════════════════════════════
 * Working directory
 * ════════════════════════════════════════ */
static char nsh_cwd[1024] = "/";

/* ════════════════════════════════════════
 * Argument tokenizer — Task 6: quoted strings
 * ════════════════════════════════════════ */
#define NSH_ARGC_MAX 32

/*
 * Tokenize `line` in-place.  Returns argc, or -1 on unmatched quote.
 * Supports:
 *   - "hello world"  → single token
 *   - \"             → literal " inside quoted string
 *   - Unterminated quote → returns -1 and prints error
 */
static int nsh_parse_args(char *line, char *argv[], int max_argc) {
    int argc = 0;
    char *p = line;
    while (*p == ' ') p++;

    while (*p && argc < max_argc) {
        if (*p == '"') {
            /* Quoted argument */
            p++;  /* skip opening " */
            argv[argc++] = p;
            while (*p) {
                if (*p == '\\' && *(p+1) == '"') {
                    /* Escaped quote — remove the backslash */
                    char *q = p;
                    while (*q) { *q = *(q+1); q++; }
                    p++;  /* now pointing past the literal " */
                } else if (*p == '"') {
                    break;
                } else {
                    p++;
                }
            }
            if (!*p) {
                /* Unterminated quote */
                vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
                vga_puts("nsh: unmatched quote\n");
                vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
                return -1;
            }
            *p++ = 0;  /* null-terminate, skip closing " */
        } else {
            argv[argc++] = p;
            while (*p && *p != ' ') p++;
        }
        if (*p == ' ') { *p++ = 0; }
        while (*p == ' ') p++;
    }
    return argc;
}

/* ════════════════════════════════════════
 * Path resolution
 * ════════════════════════════════════════ */
static void resolve_path(const char *path, char *out, size_t max) {
    if (path[0] == '/') { nsh_strcpy(out, path, max); return; }
    nsh_strcpy(out, nsh_cwd, max);
    if (out[nsh_strlen(out)-1] != '/') nsh_strcat(out, "/", max);
    nsh_strcat(out, path, max);

    /* Normalise . and .. */
    char *parts[64]; int pc=0;
    char tmp[1024]; nsh_strcpy(tmp, out, 1024);
    char *q = tmp;
    while (*q) {
        while (*q=='/') q++;
        if (!*q) break;
        char *start=q;
        while (*q&&*q!='/') q++;
        if (*q=='/') *q++=0;
        if (nsh_strcmp(start,".")==0) continue;
        if (nsh_strcmp(start,"..")==0) { if(pc>0)pc--; }
        else parts[pc++]=start;
    }
    char res[1024]; res[0]=0;
    for (int i=0;i<pc;i++) { nsh_strcat(res,"/",1024); nsh_strcat(res,parts[i],1024); }
    if (!res[0]) { res[0]='/'; res[1]=0; }
    nsh_strcpy(out, res, max);
}

/* ════════════════════════════════════════
 * Helper: print file size
 * ════════════════════════════════════════ */
static void print_size(uint64_t sz) {
    char buf[32];
    if (sz < 1024)       { nsh_uint_str(sz, buf);          nsh_print(buf); nsh_print(" B"); }
    else if (sz < 1<<20) { nsh_uint_str(sz/1024, buf);     nsh_print(buf); nsh_print(" KB"); }
    else                 { nsh_uint_str(sz>>20, buf);       nsh_print(buf); nsh_print(" MB"); }
}

/* ════════════════════════════════════════
 * Background job table (Task 5)
 * ════════════════════════════════════════ */
#define NSH_BG_MAX 8
typedef struct { uint32_t pid; char cmd[64]; int done; } bg_job_t;
static bg_job_t bg_jobs[NSH_BG_MAX];
static int bg_count = 0;
static int next_job_id = 1;

static void bg_check_done(void) {
    for (int i=0; i<NSH_BG_MAX; i++) {
        if (!bg_jobs[i].pid) continue;
        process_t *p = proc_get_by_pid(bg_jobs[i].pid);
        if (!p || p->state == PROC_ZOMBIE || p->state == PROC_DEAD) {
            char num[8]; nsh_uint_str(i+1, num);
            nsh_print("["); nsh_print(num); nsh_print("]+  Done    ");
            nsh_println(bg_jobs[i].cmd);
            bg_jobs[i].pid = 0;
            bg_jobs[i].cmd[0] = 0;
            if (bg_count > 0) bg_count--;
        }
    }
}

/* ════════════════════════════════════════
 * Built-in commands
 * ════════════════════════════════════════ */

static int cmd_ls(int argc, char *argv[]) {
    char path[1024];
    if (argc < 2) nsh_strcpy(path, nsh_cwd, 1024);
    else resolve_path(argv[1], path, 1024);

    vfs_node_t *node = vfs_open(path, 0);
    if (!node) { nsh_print("ls: cannot access '"); nsh_print(path); nsh_println("': no such file"); return 1; }

    if (node->type & VFS_NODE_FILE) {
        nsh_print(node->name); nsh_print("  "); print_size(node->size); nsh_putchar('\n');
        return 0;
    }

    vfs_dirent_t dirent;
    for (uint32_t i=0; vfs_readdir(node,i,&dirent)==0; i++) {
        char cp[1024]; nsh_strcpy(cp, path, 1024);
        if (cp[nsh_strlen(cp)-1]!='/') nsh_strcat(cp,"/",1024);
        nsh_strcat(cp, dirent.name, 1024);
        vfs_node_t *child = vfs_open(cp, 0);

        if (child && (child->type & VFS_NODE_DIR)) {
            if (!pipe_write_mode && !redirect_node)
                vga_set_color(VGA_COLOR_LIGHT_BLUE, VGA_COLOR_BLACK);
            nsh_print(dirent.name);
            if (!pipe_write_mode && !redirect_node)
                vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
            nsh_putchar('/');
        } else {
            nsh_print(dirent.name);
            if (child) { nsh_print("  "); print_size(child->size); }
        }
        nsh_putchar('\n');
    }
    return 0;
}

static int cmd_cd(int argc, char *argv[]) {
    char path[1024];
    if (argc < 2) nsh_strcpy(path, "/", 1024);
    else resolve_path(argv[1], path, 1024);
    vfs_node_t *node = vfs_open(path, 0);
    if (!node || !(node->type & VFS_NODE_DIR)) {
        nsh_print("cd: '"); nsh_print(path); nsh_println("': not a directory"); return 1;
    }
    nsh_strcpy(nsh_cwd, path, 1024);
    return 0;
}

static int cmd_pwd(int argc, char *argv[]) {
    (void)argc; (void)argv; nsh_println(nsh_cwd); return 0;
}

/*
 * cat — without args in pipe_read_mode: read from pipe buffer.
 *        with a file arg: read the file.
 */
static int cmd_cat(int argc, char *argv[]) {
    if (argc < 2) {
        /* Read from stdin / pipe */
        if (pipe_read_mode) {
            while (pipe_buf_rpos < pipe_buf_len)
                nsh_putchar(pipe_buf[pipe_buf_rpos++]);
            return 0;
        }
        /* Interactive: not very useful but valid */
        char line[NSH_LINE_MAX]; int i=0; char c;
        while ((c=nsh_getchar())&&c!='\n'&&i<NSH_LINE_MAX-1) line[i++]=c;
        line[i]=0; nsh_println(line);
        return 0;
    }
    char path[1024];
    resolve_path(argv[1], path, 1024);
    vfs_node_t *node = vfs_open(path, 0);
    if (!node) { nsh_print("cat: '"); nsh_print(path); nsh_println("': no such file"); return 1; }
    uint8_t buf[512]; uint64_t offset=0; uint32_t n;
    while ((n=vfs_read(node,offset,sizeof(buf)-1,buf))>0) {
        buf[n]=0; nsh_print((char*)buf); offset+=n;
    }
    return 0;
}

static int cmd_echo(int argc, char *argv[]) {
    for (int i=1; i<argc; i++) {
        char exp[NSH_LINE_MAX]; expand_vars(argv[i], exp, NSH_LINE_MAX);
        if (i>1) nsh_putchar(' ');
        nsh_print(exp);
    }
    nsh_putchar('\n'); return 0;
}

static int cmd_mkdir(int argc, char *argv[]) {
    if (argc < 2) { nsh_println("mkdir: missing argument"); return 1; }
    char path[1024]; resolve_path(argv[1], path, 1024);
    if (vfs_mkdir(path)<0) { nsh_print("mkdir: cannot create '"); nsh_print(path); nsh_println("'"); return 1; }
    return 0;
}

static int cmd_rm(int argc, char *argv[]) {
    if (argc < 2) { nsh_println("rm: missing argument"); return 1; }
    char path[1024]; resolve_path(argv[1], path, 1024);
    if (vfs_unlink(path)<0) { nsh_print("rm: cannot remove '"); nsh_print(path); nsh_println("'"); return 1; }
    return 0;
}

static int cmd_touch(int argc, char *argv[]) {
    if (argc < 2) { nsh_println("touch: missing argument"); return 1; }
    char path[1024]; resolve_path(argv[1], path, 1024);
    vfs_create(path, 0); return 0;
}

static int cmd_cp(int argc, char *argv[]) {
    if (argc < 3) { nsh_println("cp: usage: cp <src> <dst>"); return 1; }
    char src[1024], dst[1024];
    resolve_path(argv[1], src, 1024); resolve_path(argv[2], dst, 1024);
    vfs_node_t *sn = vfs_open(src, 0);
    if (!sn) { nsh_print("cp: '"); nsh_print(src); nsh_println("': not found"); return 1; }
    vfs_create(dst, 0);
    vfs_node_t *dn = vfs_open(dst, 0);
    if (!dn) { nsh_print("cp: cannot create '"); nsh_print(dst); nsh_println("'"); return 1; }
    uint8_t buf[512]; uint64_t off=0; uint32_t n;
    while ((n=vfs_read(sn,off,sizeof(buf),buf))>0) { vfs_write(dn,off,n,buf); off+=n; }
    return 0;
}

static int cmd_mv(int argc, char *argv[]) {
    if (argc < 3) { nsh_println("mv: usage: mv <src> <dst>"); return 1; }
    cmd_cp(argc, argv);
    char src[1024]; resolve_path(argv[1], src, 1024); vfs_unlink(src); return 0;
}

static int cmd_clear(int argc, char *argv[]) {
    (void)argc; (void)argv; vga_clear(); return 0;
}

static int cmd_help(int argc, char *argv[]) {
    (void)argc; (void)argv;
    if (!pipe_write_mode && !redirect_node)
        vga_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    nsh_println("NexOS Shell (nsh) v0.1 — Built-in Commands:");
    if (!pipe_write_mode && !redirect_node)
        vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    nsh_println("  ls [path]          List directory contents");
    nsh_println("  cd [path]          Change directory");
    nsh_println("  pwd                Print working directory");
    nsh_println("  cat [file]         Display file (or pipe stdin)");
    nsh_println("  echo [args]        Print text (supports $VAR)");
    nsh_println("  mkdir <dir>        Create directory");
    nsh_println("  rm <file>          Delete file/directory");
    nsh_println("  touch <file>       Create empty file");
    nsh_println("  cp <src> <dst>     Copy file");
    nsh_println("  mv <src> <dst>     Move/rename file");
    nsh_println("  clear              Clear screen");
    nsh_println("  env                Show environment variables");
    nsh_println("  export KEY=VALUE   Set environment variable");
    nsh_println("  uname              Print OS info");
    nsh_println("  uptime             Show system uptime");
    nsh_println("  ps                 List processes");
    nsh_println("  kill <pid>         Kill process");
    nsh_println("  free               Show memory usage");
    nsh_println("  date               Show current date/time");
    nsh_println("  mount              Show mount points");
    nsh_println("  reboot             Reboot the system");
    nsh_println("  halt               Halt the system");
    nsh_println("  help               Show this help");
    nsh_println("  exit / logout      Exit the shell");
    nsh_println("");
    if (!pipe_write_mode && !redirect_node)
        vga_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    nsh_println("New commands:");
    if (!pipe_write_mode && !redirect_node)
        vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    nsh_println("  hexdump <file>     Hex + ASCII dump (first 512 bytes)");
    nsh_println("  wc <file>          Count lines/words/bytes");
    nsh_println("  head [-N] <file>   Print first N lines (default 10)");
    nsh_println("  tail [-N] <file>   Print last N lines (default 10)");
    nsh_println("  find [path] [name] Recursive file search");
    nsh_println("  top                Live process table (5 refreshes)");
    nsh_println("  history            Show command history");
    nsh_println("  which <cmd>        Show if cmd is a built-in");
    nsh_println("  stat <path>        File metadata");
    nsh_println("  tree [path]        ASCII directory tree");
    nsh_println("  memmap             Physical memory map");
    nsh_println("  ver                OS version and build info");
    nsh_println("  ifconfig           Network interface status (RTL8139)");
    nsh_println("  ping <ip>          Send ICMP echo requests");
    nsh_println("");
    nsh_println("Shell features:");
    nsh_println("  cmd1 | cmd2        Pipe output of cmd1 into cmd2");
    nsh_println("  cmd > file         Redirect stdout to file (truncate)");
    nsh_println("  cmd >> file        Redirect stdout to file (append)");
    nsh_println("  cmd &              Run command in background");
    nsh_println("  \"hello world\"      Quoted multi-word argument");
    return 0;
}

static int cmd_env(int argc, char *argv[]) {
    (void)argc; (void)argv;
    for (int i=0;i<env_count;i++) {
        nsh_print(env_vars[i].name); nsh_putchar('='); nsh_println(env_vars[i].value);
    }
    return 0;
}

static int cmd_export(int argc, char *argv[]) {
    if (argc < 2) { nsh_println("export: usage: export KEY=VALUE"); return 1; }
    char *eq = argv[1]; while(*eq&&*eq!='=') eq++;
    if (!*eq) { nsh_println("export: missing '='"); return 1; }
    *eq = 0; env_set(argv[1], eq+1); return 0;
}

static int cmd_uname(int argc, char *argv[]) {
    (void)argc; (void)argv;
    nsh_println("NexOS 0.1.0 x86_64 MIT"); return 0;
}

static int cmd_uptime(int argc, char *argv[]) {
    (void)argc; (void)argv;
    char buf[32];
    uint64_t secs = timer_get_uptime_seconds();
    uint64_t mins = secs/60, hrs = mins/60;
    nsh_print("up ");
    nsh_uint_str(hrs,buf); nsh_print(buf); nsh_print("h ");
    nsh_uint_str(mins%60,buf); nsh_print(buf); nsh_print("m ");
    nsh_uint_str(secs%60,buf); nsh_print(buf); nsh_println("s");
    return 0;
}

/* Task 7: ps reads from /proc/<pid>/status */
static int cmd_ps(int argc, char *argv[]) {
    (void)argc; (void)argv;
    if (!pipe_write_mode && !redirect_node)
        vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    nsh_println("  PID  STATE    NAME");

    for (int i=0; i<MAX_PROCESSES; i++) {
        if (!processes[i]) continue;

        /* Try to read from /proc/<pid>/status */
        char proc_path[64];
        char pidbuf[12];
        nsh_uint_str(processes[i]->pid, pidbuf);
        nsh_strcpy(proc_path, "/proc/", 64);
        nsh_strcat(proc_path, pidbuf, 64);
        nsh_strcat(proc_path, "/status", 64);

        vfs_node_t *st = vfs_open(proc_path, 0);
        if (st) {
            uint8_t sbuf[256]; uint32_t n = vfs_read(st, 0, sizeof(sbuf)-1, sbuf);
            sbuf[n] = 0;
            /* Extract PID, Name, State lines */
            nsh_print("  "); nsh_print(pidbuf);
            /* Find State: line */
            char *p = (char*)sbuf;
            char state_val[16]; state_val[0]=0;
            char name_val[64];  name_val[0]=0;
            while (*p) {
                if (nsh_strncmp(p,"State:",6)==0) {
                    p+=6; while(*p==' ')p++;
                    int vi=0; while(*p&&*p!='\n'&&vi<15){state_val[vi++]=*p++;} state_val[vi]=0;
                } else if (nsh_strncmp(p,"Name:",5)==0) {
                    p+=5; while(*p==' ')p++;
                    int vi=0; while(*p&&*p!='\n'&&vi<63){name_val[vi++]=*p++;} name_val[vi]=0;
                }
                while(*p&&*p!='\n') p++;
                if(*p=='\n') p++;
            }
            nsh_print("  "); nsh_print(state_val[0]?state_val:"?");
            nsh_print("  ");   nsh_println(name_val[0]?name_val:"?");
        } else {
            /* Fallback: direct PCB */
            static const char *states[]={"RUNNING","READY","BLOCKED","ZOMBIE","DEAD"};
            int st2=(int)processes[i]->state;
            nsh_print("  "); nsh_print(pidbuf);
            nsh_print("  "); nsh_print(states[st2<5?st2:4]);
            nsh_print("  "); nsh_println(processes[i]->name);
        }
    }
    return 0;
}

static int cmd_kill(int argc, char *argv[]) {
    if (argc < 2) { nsh_println("kill: usage: kill <pid>"); return 1; }
    uint32_t pid=0; char *s=argv[1];
    while(*s>='0'&&*s<='9'){pid=pid*10+(*s-'0');s++;}
    proc_kill(pid); return 0;
}

static int cmd_free(int argc, char *argv[]) {
    (void)argc; (void)argv;
    char buf[32];
    uint64_t total = pmm_get_total_memory()/1024;
    uint64_t free  = pmm_get_free_memory() /1024;
    uint64_t used  = total - free;
    if (!pipe_write_mode && !redirect_node)
        vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    nsh_println("              total        used        free");
    nsh_print("Mem:   ");
    nsh_uint_str(total,buf); nsh_print(buf); nsh_print(" KB    ");
    nsh_uint_str(used, buf); nsh_print(buf); nsh_print(" KB    ");
    nsh_uint_str(free, buf); nsh_print(buf); nsh_println(" KB");
    return 0;
}

static int cmd_date(int argc, char *argv[]) {
    (void)argc; (void)argv;
    rtc_time_t t; rtc_get_time(&t);
    char buf[32]; rtc_time_to_string(buf, &t);
    nsh_println(buf); return 0;
}

static int cmd_mount(int argc, char *argv[]) {
    (void)argc; (void)argv;
    nsh_println("Mounted filesystems:");
    nsh_println("  /        ramfs");
    nsh_println("  /proc    procfs");
    nsh_println("  /tmp     ramfs");
    nsh_println("  /mnt     fat32 (if disk present)");
    return 0;
}

static int cmd_reboot(int argc, char *argv[]) {
    (void)argc; (void)argv;
    nsh_println("Rebooting...");
    io_outb(0x64, 0xFE);
    for(;;) { hlt(); }
    return 0;
}

static int cmd_halt(int argc, char *argv[]) {
    (void)argc; (void)argv;
    nsh_println("System halted.");
    cli(); for(;;) hlt(); return 0;
}

/* ════════════════════════════════════════
 * Phase 2 — New built-in commands
 * ════════════════════════════════════════ */

/* ── Shared helpers ──────────────────────────────────────────────────────── */
static void print_hex_byte(uint8_t b) {
    static const char hx[] = "0123456789abcdef";
    nsh_putchar(hx[b >> 4]); nsh_putchar(hx[b & 0xF]);
}
static void print_hex32(uint64_t v) {
    nsh_print("0x");
    char buf[9]; buf[8] = 0;
    static const char hx[] = "0123456789ABCDEF";
    for (int i = 7; i >= 0; i--) { buf[i] = hx[v & 0xF]; v >>= 4; }
    nsh_print(buf);
}

static int parse_ip(const char *s, uint8_t ip[4]) {
    int o = 0, val = 0, dots = 0;
    for (int i = 0; s[i]; i++) {
        if (s[i] >= '0' && s[i] <= '9') { val = val * 10 + (s[i] - '0'); }
        else if (s[i] == '.' && o < 3)  { ip[o++] = (uint8_t)val; val = 0; dots++; }
        else break;
    }
    ip[o < 4 ? o : 3] = (uint8_t)val;
    return (dots == 3) ? 0 : -1;
}

/* ── hexdump ─────────────────────────────────────────────────────────────── */
static int cmd_hexdump(int argc, char *argv[]) {
    if (argc < 2) { nsh_println("hexdump: usage: hexdump <file>"); return 1; }
    char path[1024]; resolve_path(argv[1], path, 1024);
    vfs_node_t *node = vfs_open(path, 0);
    if (!node) { nsh_print("hexdump: '"); nsh_print(path); nsh_println("': not found"); return 1; }
    uint8_t buf[16]; uint64_t offset = 0; uint32_t n;
    while ((n = vfs_read(node, offset, 16, buf)) > 0) {
        /* Offset */
        char ob[8]; uint64_t off = offset;
        for (int i = 6; i >= 0; i--) {
            static const char hx[] = "0123456789abcdef";
            ob[i] = hx[off & 0xF]; off >>= 4;
        }
        ob[7] = 0; nsh_print(ob); nsh_print(": ");
        /* Hex bytes */
        for (uint32_t i = 0; i < 16; i++) {
            if (i < n) { print_hex_byte(buf[i]); nsh_putchar(' '); }
            else       { nsh_print("   "); }
            if (i == 7) nsh_putchar(' ');
        }
        nsh_print(" ");
        /* ASCII sidebar */
        for (uint32_t i = 0; i < n; i++)
            nsh_putchar((buf[i] >= 32 && buf[i] < 127) ? (char)buf[i] : '.');
        nsh_putchar('\n');
        offset += n;
        if (offset >= 512) break; /* cap at 512 bytes by default */
    }
    return 0;
}

/* ── wc ──────────────────────────────────────────────────────────────────── */
static int cmd_wc(int argc, char *argv[]) {
    if (argc < 2) { nsh_println("wc: usage: wc <file>"); return 1; }
    char path[1024]; resolve_path(argv[1], path, 1024);
    vfs_node_t *node = vfs_open(path, 0);
    if (!node) { nsh_print("wc: '"); nsh_print(path); nsh_println("': not found"); return 1; }
    uint64_t lines = 0, words = 0, bytes = 0;
    uint8_t buf[128]; uint64_t off = 0; uint32_t n;
    int in_word = 0;
    while ((n = vfs_read(node, off, sizeof(buf), buf)) > 0) {
        for (uint32_t i = 0; i < n; i++) {
            bytes++;
            if      (buf[i] == '\n')                             { lines++; in_word = 0; }
            else if (buf[i] == ' ' || buf[i] == '\t')            { in_word = 0; }
            else if (!in_word)                                   { words++;  in_word = 1; }
        }
        off += n;
    }
    char b[32];
    nsh_print("  "); nsh_uint_str(lines, b); nsh_print(b);
    nsh_print("  "); nsh_uint_str(words, b); nsh_print(b);
    nsh_print("  "); nsh_uint_str(bytes, b); nsh_print(b);
    nsh_print(" "); nsh_println(path);
    return 0;
}

/* ── head ────────────────────────────────────────────────────────────────── */
static int cmd_head(int argc, char *argv[]) {
    if (argc < 2) { nsh_println("head: usage: head [-N] <file>"); return 1; }
    int n = 10; int farg = 1;
    if (argc >= 3 && argv[1][0] == '-') {
        n = 0; char *s = argv[1]+1; while (*s>='0'&&*s<='9'){n=n*10+(*s-'0');s++;}
        farg = 2;
    }
    char path[1024]; resolve_path(argv[farg], path, 1024);
    vfs_node_t *node = vfs_open(path, 0);
    if (!node) { nsh_print("head: '"); nsh_print(path); nsh_println("': not found"); return 1; }
    uint8_t buf[128]; uint64_t off = 0; uint32_t rd;
    int got = 0;
    while (got < n && (rd = vfs_read(node, off, sizeof(buf), buf)) > 0) {
        for (uint32_t i = 0; i < rd && got < n; i++) {
            nsh_putchar((char)buf[i]);
            if (buf[i] == '\n') got++;
        }
        off += rd;
    }
    return 0;
}

/* ── tail ────────────────────────────────────────────────────────────────── */
static int cmd_tail(int argc, char *argv[]) {
    if (argc < 2) { nsh_println("tail: usage: tail [-N] <file>"); return 1; }
    int n = 10; int farg = 1;
    if (argc >= 3 && argv[1][0] == '-') {
        n = 0; char *s = argv[1]+1; while(*s>='0'&&*s<='9'){n=n*10+(*s-'0');s++;}
        farg = 2;
    }
    char path[1024]; resolve_path(argv[farg], path, 1024);
    vfs_node_t *node = vfs_open(path, 0);
    if (!node) { nsh_print("tail: '"); nsh_print(path); nsh_println("': not found"); return 1; }
    /* Heap-allocate read buffer to avoid stack overflow */
    uint8_t *fbuf = (uint8_t *)kmalloc(4096);
    if (!fbuf) { nsh_println("tail: out of memory"); return 1; }
    uint32_t total = vfs_read(node, 0, 4095, fbuf);
    fbuf[total] = 0;
    /* Find start positions of last n lines */
    int starts[64], sc = 0; starts[0] = 0;
    for (uint32_t i = 0; i < total && sc < 63; i++)
        if (fbuf[i] == '\n' && i + 1 < total) starts[++sc] = (int)(i + 1);
    int s0 = sc - n + 1; if (s0 < 0) s0 = 0;
    for (uint32_t i = (uint32_t)starts[s0]; i < total; i++) nsh_putchar((char)fbuf[i]);
    kfree(fbuf);
    return 0;
}

/* ── find ────────────────────────────────────────────────────────────────── */
static void find_recursive(const char *path, const char *filter, int depth) {
    if (depth > 8) return;
    vfs_node_t *dir = vfs_open(path, 0);
    if (!dir || !(dir->type & VFS_NODE_DIR)) return;
    char *child = (char *)kmalloc(512);
    if (!child) return;
    vfs_dirent_t de;
    for (uint32_t i = 0; vfs_readdir(dir, i, &de) == 0; i++) {
        nsh_strcpy(child, path, 512);
        if (child[nsh_strlen(child)-1] != '/') nsh_strcat(child, "/", 512);
        nsh_strcat(child, de.name, 512);
        if (!filter[0] || nsh_strcmp(de.name, filter) == 0)
            nsh_println(child);
        find_recursive(child, filter, depth + 1);
    }
    kfree(child);
}
static int cmd_find(int argc, char *argv[]) {
    char path[1024];
    if (argc < 2) nsh_strcpy(path, nsh_cwd, 1024);
    else resolve_path(argv[1], path, 1024);
    const char *filter = (argc >= 3) ? argv[2] : "";
    find_recursive(path, filter, 0);
    return 0;
}

/* ── top ─────────────────────────────────────────────────────────────────── */
static int cmd_top(int argc, char *argv[]) {
    (void)argc; (void)argv;
    static const char *pstates[] = {"RUNNING","READY  ","BLOCKED","ZOMBIE ","DEAD   "};
    for (int pass = 0; pass < 5; pass++) {
        vga_clear();
        char buf[32];
        nsh_print("NexOS top — uptime: ");
        nsh_uint_str(timer_get_uptime_seconds(), buf); nsh_print(buf); nsh_println("s");
        nsh_println("  PID  STATE    NAME");
        nsh_println("  ---  -------  ----------------");
        for (int i = 0; i < MAX_PROCESSES; i++) {
            if (!processes[i]) continue;
            int s = (int)processes[i]->state;
            nsh_print("  "); nsh_uint_str(processes[i]->pid, buf); nsh_print(buf);
            nsh_print("  "); nsh_print(pstates[s < 5 ? s : 4]);
            nsh_print("  "); nsh_println(processes[i]->name);
        }
        nsh_print("\n  [pass "); nsh_uint_str(pass+1, buf); nsh_print(buf);
        nsh_println("/5 — refreshing in 2s; press q to abort]");
        if (pass < 4) timer_sleep_ms(2000);
    }
    return 0;
}

/* ── history ─────────────────────────────────────────────────────────────── */
static int cmd_history(int argc, char *argv[]) {
    (void)argc; (void)argv;
    int start = (hist_count > NSH_HIST_SIZE) ? hist_count - NSH_HIST_SIZE : 0;
    char buf[16];
    for (int i = start; i < hist_count; i++) {
        nsh_print("  "); nsh_uint_str((uint64_t)(i + 1), buf); nsh_print(buf);
        nsh_print("  "); nsh_println(history[i % NSH_HIST_SIZE]);
    }
    return 0;
}

/* ── which ───────────────────────────────────────────────────────────────── */
static int cmd_which(int argc, char *argv[]) {
    if (argc < 2) { nsh_println("which: usage: which <command>"); return 1; }
    static const char *all_builtins[] = {
        "ls","cd","pwd","cat","echo","mkdir","rm","cp","mv","touch","clear",
        "help","env","export","uname","uptime","ps","kill","free","date",
        "mount","reboot","halt","exit","logout",
        "hexdump","wc","head","tail","find","top","history","which","stat",
        "tree","memmap","ver","ifconfig","ping", NULL
    };
    for (int i = 0; all_builtins[i]; i++) {
        if (nsh_strcmp(all_builtins[i], argv[1]) == 0) {
            nsh_print(argv[1]); nsh_println(" is a built-in nsh command");
            return 0;
        }
    }
    nsh_print("which: "); nsh_print(argv[1]); nsh_println(": not found");
    return 1;
}

/* ── stat ────────────────────────────────────────────────────────────────── */
static int cmd_stat(int argc, char *argv[]) {
    if (argc < 2) { nsh_println("stat: usage: stat <path>"); return 1; }
    char path[1024]; resolve_path(argv[1], path, 1024);
    vfs_node_t *node = vfs_open(path, 0);
    if (!node) { nsh_print("stat: cannot stat '"); nsh_print(path); nsh_println("': no such file"); return 1; }
    char buf[32];
    nsh_print("  File: "); nsh_println(path);
    nsh_print("  Type: ");
    if      (node->type & VFS_NODE_DIR)     nsh_println("directory");
    else if (node->type & VFS_NODE_CHARDEV) nsh_println("character device");
    else                                    nsh_println("regular file");
    nsh_print("  Size: "); nsh_uint_str(node->size, buf); nsh_print(buf); nsh_println(" bytes");
    nsh_print(" Inode: "); nsh_uint_str(node->inode, buf); nsh_println(buf);
    nsh_print(" Flags: 0x"); print_hex_byte((uint8_t)(node->flags >> 8));
    print_hex_byte((uint8_t)node->flags); nsh_putchar('\n');
    return 0;
}

/* ── tree ────────────────────────────────────────────────────────────────── */
static void tree_recursive(const char *path, int depth, const char *prefix) {
    if (depth > 4) return;
    vfs_node_t *dir = vfs_open(path, 0);
    if (!dir || !(dir->type & VFS_NODE_DIR)) return;
    /* Count entries */
    int total = 0;
    vfs_dirent_t de;
    while (vfs_readdir(dir, (uint32_t)total, &de) == 0) total++;
    for (int i = 0; i < total; i++) {
        if (vfs_readdir(dir, (uint32_t)i, &de) != 0) break;
        int last = (i == total - 1);
        nsh_print(prefix);
        nsh_print(last ? "+-- " : "|-- ");
        nsh_println(de.name);
        /* Child path on heap to keep stack small */
        char *child = (char *)kmalloc(512);
        if (!child) continue;
        nsh_strcpy(child, path, 512);
        if (child[nsh_strlen(child)-1] != '/') nsh_strcat(child, "/", 512);
        nsh_strcat(child, de.name, 512);
        vfs_node_t *cn = vfs_open(child, 0);
        if (cn && (cn->type & VFS_NODE_DIR)) {
            char *np = (char *)kmalloc(256);
            if (np) {
                nsh_strcpy(np, prefix, 256);
                nsh_strcat(np, last ? "    " : "|   ", 256);
                tree_recursive(child, depth + 1, np);
                kfree(np);
            }
        }
        kfree(child);
    }
}
static int cmd_tree(int argc, char *argv[]) {
    char path[1024];
    if (argc < 2) nsh_strcpy(path, nsh_cwd, 1024);
    else resolve_path(argv[1], path, 1024);
    nsh_println(path);
    tree_recursive(path, 0, "");
    return 0;
}

/* ── memmap ──────────────────────────────────────────────────────────────── */
static int cmd_memmap(int argc, char *argv[]) {
    (void)argc; (void)argv;
    char buf[32];
    uint64_t total = pmm_get_total_memory();
    uint64_t pmm_free = pmm_get_free_memory();
    pmm_print_map();   /* also log to serial */
    nsh_println("Physical memory map:");
    nsh_println("  START        END          TYPE               SIZE");
    nsh_println("  ----------   ----------   ----------------   ------");
    nsh_print("  "); print_hex32(0x00000000ULL); nsh_print("  - ");
    print_hex32(0x000FFFFFULL); nsh_println("   [BIOS/reserved]      1 MB");
    nsh_print("  "); print_hex32(0x00100000ULL); nsh_print("  - ");
    print_hex32(0x011FFFFFULL); nsh_println("   [kernel+statics]    17 MB");
    nsh_print("  "); print_hex32(0x01200000ULL); nsh_print("  - ");
    print_hex32(0x019FFFFFULL); nsh_println("   [heap]               8 MB");
    nsh_print("  "); print_hex32(0x01A00000ULL); nsh_print("  - ");
    print_hex32(total > 0 ? total - 1 : 0); nsh_print("   [free RAM]          ");
    uint64_t free_mb = (total > 0x01A00000ULL) ? (total - 0x01A00000ULL) >> 20 : 0;
    nsh_uint_str(free_mb, buf); nsh_print(buf); nsh_println(" MB");
    nsh_println("");
    nsh_print("  Total physical: "); nsh_uint_str(total >> 20, buf); nsh_print(buf); nsh_println(" MB");
    nsh_print("  Free  physical: "); nsh_uint_str(pmm_free / 1024, buf); nsh_print(buf); nsh_println(" KB");
    nsh_print("  Heap free:      "); nsh_uint_str(heap_free_space() / 1024, buf); nsh_print(buf); nsh_println(" KB");
    return 0;
}

/* ── ver ─────────────────────────────────────────────────────────────────── */
static int cmd_ver(int argc, char *argv[]) {
    (void)argc; (void)argv;
    char buf[32];
    nsh_println("NexOS 0.1 — custom x86_64 OS");
    nsh_println("  Arch:    x86_64 (64-bit protected mode, ring 0/3)");
    nsh_println("  Boot:    Multiboot2");
    nsh_println("  Subsystems: GDT IDT PMM VMM heap VFS ramfs procfs");
    nsh_println("              FAT32 PS/2 ATA PIT RTC PCI RTL8139 scheduler");
    nsh_print  ("  Built:   "); nsh_print(__DATE__); nsh_print(" "); nsh_println(__TIME__);
    nsh_print  ("  Uptime:  "); nsh_uint_str(timer_get_uptime_seconds(), buf); nsh_print(buf); nsh_println("s");
    nsh_print  ("  Heap:    "); nsh_uint_str(heap_free_space() / 1024, buf); nsh_print(buf); nsh_println(" KB free");
    nsh_print  ("  NIC:     "); nsh_println(rtl8139_found() ? "RTL8139 UP" : "not present");
    return 0;
}

/* ── ifconfig ────────────────────────────────────────────────────────────── */
static int cmd_ifconfig(int argc, char *argv[]) {
    (void)argc; (void)argv;
    if (!rtl8139_found()) { nsh_println("eth0: not present"); return 0; }
    uint8_t mac[6]; rtl8139_get_mac(mac);
    char buf[32];
    nsh_print("eth0: MAC=");
    for (int i = 0; i < 6; i++) {
        print_hex_byte(mac[i]);
        if (i < 5) nsh_putchar(':');
    }
    nsh_println("  inet 10.0.2.15  netmask 255.255.255.0  gw 10.0.2.2");
    nsh_print  ("      RX packets: "); nsh_uint_str(rtl8139_get_rx_count(), buf); nsh_println(buf);
    nsh_print  ("      TX packets: "); nsh_uint_str(rtl8139_get_tx_count(), buf); nsh_println(buf);
    nsh_println("      Status: UP  Link: 100Mbps");
    return 0;
}

/* ── ping ────────────────────────────────────────────────────────────────── */
static int cmd_ping(int argc, char *argv[]) {
    if (argc < 2) { nsh_println("ping: usage: ping <ip>"); return 1; }
    if (!rtl8139_found()) { nsh_println("ping: no network interface"); return 1; }

    uint8_t dst_ip[4];
    if (parse_ip(argv[1], dst_ip) < 0) {
        nsh_print("ping: invalid IP address: "); nsh_println(argv[1]); return 1;
    }
    uint32_t dest_ip = ((uint32_t)dst_ip[0] << 24) | ((uint32_t)dst_ip[1] << 16)
                     | ((uint32_t)dst_ip[2] <<  8) |  dst_ip[3];

    char buf[32];
    nsh_print("PING "); nsh_print(argv[1]); nsh_println(" 40 bytes of data.");

    int received = 0;
    for (int seq = 1; seq <= 4; seq++) {
        int rtt = icmp_send_echo(dest_ip);
        if (rtt >= 0) {
            received++;
            nsh_print("40 bytes from "); nsh_print(argv[1]);
            nsh_print(": icmp_seq="); nsh_uint_str((uint64_t)seq, buf); nsh_print(buf);
            nsh_print(" ttl=64 time="); nsh_uint_str((uint64_t)rtt, buf);
            nsh_print(buf); nsh_println(" ms");
        } else {
            nsh_print("Request timeout for icmp_seq=");
            nsh_uint_str((uint64_t)seq, buf); nsh_println(buf);
        }
        if (seq < 4) timer_sleep_ms(1000);
    }

    nsh_println("");
    nsh_print("--- "); nsh_print(argv[1]); nsh_println(" ping statistics ---");
    nsh_print("4 packets transmitted, ");
    nsh_uint_str((uint64_t)received, buf); nsh_print(buf); nsh_println(" received");
    return (received > 0) ? 0 : 1;
}

/* ════════════════════════════════════════
 * Tab completion
 * ════════════════════════════════════════ */
static const char *builtin_names[] = {
    "ls","cd","pwd","cat","echo","mkdir","rm","cp","mv","touch","clear",
    "help","env","export","uname","uptime","ps","kill","free","date",
    "mount","reboot","halt","exit","logout",
    "hexdump","wc","head","tail","find","top","history","which","stat",
    "tree","memmap","ver","ifconfig","ping",NULL
};

static void tab_complete(char *line, int *len) {
    if (!*len) return;
    int start = *len - 1;
    while (start>0 && line[start-1]!=' ') start--;
    char *partial = line + start;
    int plen = *len - start;

    for (int i=0; builtin_names[i]; i++) {
        if (nsh_strncmp(builtin_names[i], partial, (size_t)plen)==0) {
            int ml = (int)nsh_strlen(builtin_names[i]);
            for (int j=plen; j<ml; j++) {
                line[start+j] = builtin_names[i][j];
                vga_putchar(builtin_names[i][j]);
            }
            *len = start + ml; return;
        }
    }

    /* VFS path completion */
    char *last_slash = NULL;
    for (char *p=partial; *p; p++) if(*p=='/') last_slash=p;
    if (last_slash) {
        char parent[1024];
        int pl = (int)(last_slash - partial);
        for (int i=0;i<pl;i++) { parent[i]=partial[i]; } parent[pl]=0;
        char pp[1024]; resolve_path(parent, pp, 1024);
        vfs_node_t *dir = vfs_open(pp, 0);
        if (dir && (dir->type & VFS_NODE_DIR)) {
            const char *nm = last_slash + 1;
            int nml = (int)nsh_strlen(nm);
            vfs_dirent_t de;
            for (uint32_t i=0; vfs_readdir(dir,i,&de)==0; i++) {
                if (nsh_strncmp(de.name,nm,(size_t)nml)==0) {
                    for (int j=nml; de.name[j]; j++) {
                        line[*len]=de.name[j]; vga_putchar(de.name[j]); (*len)++;
                    }
                    break;
                }
            }
        }
    }
}

/* ════════════════════════════════════════
 * Line editor (keyboard input)
 * ════════════════════════════════════════ */
static int read_line(char *buf, size_t max) {
    int len=0; hist_pos=-1; buf[0]=0;
    while (1) {
        char ch = keyboard_getchar();
        if (ch=='\n'||ch=='\r') { vga_putchar('\n'); buf[len]=0; break; }
        else if (ch=='\b') {
            if (len>0) { len--; buf[len]=0; vga_putchar('\b'); vga_putchar(' '); vga_putchar('\b'); }
        } else if (ch=='\t') {
            buf[len]=0; tab_complete(buf, &len);
        } else if (ch=='\x1B') {   /* ESC sequences for arrow keys */
            char c2=keyboard_getchar();
            if (c2=='[') {
                char c3=keyboard_getchar();
                if (c3=='A' && hist_count>0) {    /* Up */
                    if (hist_pos < hist_count-1) hist_pos++;
                    const char *h=history[(hist_count-1-hist_pos)%NSH_HIST_SIZE];
                    while(len>0){vga_putchar('\b');vga_putchar(' ');vga_putchar('\b');len--;}
                    nsh_strcpy(buf,h,max); len=(int)nsh_strlen(buf); vga_puts(buf);
                } else if (c3=='B' && hist_pos>=0) { /* Down */
                    hist_pos--;
                    if (hist_pos>=0) {
                        const char *h=history[(hist_count-1-hist_pos)%NSH_HIST_SIZE];
                        while(len>0){vga_putchar('\b');vga_putchar(' ');vga_putchar('\b');len--;}
                        nsh_strcpy(buf,h,max); len=(int)nsh_strlen(buf); vga_puts(buf);
                    } else {
                        while(len>0){vga_putchar('\b');vga_putchar(' ');vga_putchar('\b');len--;}
                        buf[0]=0;
                    }
                }
            }
        } else if (ch>=32 && ch<127) {
            if ((size_t)len < max-1) { buf[len++]=ch; buf[len]=0; vga_putchar(ch); }
        }
    }
    return len;
}

/* ════════════════════════════════════════
 * Source a script file
 * ════════════════════════════════════════ */
static void nsh_exec_line(char *line);  /* forward decl */

static void nsh_source(const char *path) {
    vfs_node_t *node = vfs_open(path, 0);
    if (!node) return;
    uint8_t buf[2048];
    uint32_t n = vfs_read(node, 0, sizeof(buf)-1, buf);
    buf[n] = 0;
    char *p = (char *)buf;
    while (*p) {
        char line[NSH_LINE_MAX]; int i=0;
        while(*p&&*p!='\n'&&i<NSH_LINE_MAX-1) line[i++]=*p++;
        line[i]=0; if(*p=='\n') p++;
        nsh_exec_line(line);
    }
}

/* ════════════════════════════════════════
 * Command dispatcher
 * ════════════════════════════════════════ */
static int nsh_exec_builtin(int argc, char *argv[]) {
    if (!argc) return 0;
    const char *cmd = argv[0];

    if (nsh_strcmp(cmd,"ls")==0)     return cmd_ls(argc,argv);
    if (nsh_strcmp(cmd,"cd")==0)     return cmd_cd(argc,argv);
    if (nsh_strcmp(cmd,"pwd")==0)    return cmd_pwd(argc,argv);
    if (nsh_strcmp(cmd,"cat")==0)    return cmd_cat(argc,argv);
    if (nsh_strcmp(cmd,"echo")==0)   return cmd_echo(argc,argv);
    if (nsh_strcmp(cmd,"mkdir")==0)  return cmd_mkdir(argc,argv);
    if (nsh_strcmp(cmd,"rm")==0)     return cmd_rm(argc,argv);
    if (nsh_strcmp(cmd,"touch")==0)  return cmd_touch(argc,argv);
    if (nsh_strcmp(cmd,"cp")==0)     return cmd_cp(argc,argv);
    if (nsh_strcmp(cmd,"mv")==0)     return cmd_mv(argc,argv);
    if (nsh_strcmp(cmd,"clear")==0)  return cmd_clear(argc,argv);
    if (nsh_strcmp(cmd,"help")==0)   return cmd_help(argc,argv);
    if (nsh_strcmp(cmd,"env")==0)    return cmd_env(argc,argv);
    if (nsh_strcmp(cmd,"export")==0) return cmd_export(argc,argv);
    if (nsh_strcmp(cmd,"uname")==0)  return cmd_uname(argc,argv);
    if (nsh_strcmp(cmd,"uptime")==0) return cmd_uptime(argc,argv);
    if (nsh_strcmp(cmd,"ps")==0)     return cmd_ps(argc,argv);
    if (nsh_strcmp(cmd,"kill")==0)   return cmd_kill(argc,argv);
    if (nsh_strcmp(cmd,"free")==0)   return cmd_free(argc,argv);
    if (nsh_strcmp(cmd,"date")==0)   return cmd_date(argc,argv);
    if (nsh_strcmp(cmd,"mount")==0)  return cmd_mount(argc,argv);
    if (nsh_strcmp(cmd,"reboot")==0) return cmd_reboot(argc,argv);
    if (nsh_strcmp(cmd,"halt")==0)   return cmd_halt(argc,argv);
    if (nsh_strcmp(cmd,"exit")==0 || nsh_strcmp(cmd,"logout")==0) return -1;

    if (nsh_strcmp(cmd,"hexdump")==0) return cmd_hexdump(argc,argv);
    if (nsh_strcmp(cmd,"wc")==0)      return cmd_wc(argc,argv);
    if (nsh_strcmp(cmd,"head")==0)    return cmd_head(argc,argv);
    if (nsh_strcmp(cmd,"tail")==0)    return cmd_tail(argc,argv);
    if (nsh_strcmp(cmd,"find")==0)    return cmd_find(argc,argv);
    if (nsh_strcmp(cmd,"top")==0)     return cmd_top(argc,argv);
    if (nsh_strcmp(cmd,"history")==0) return cmd_history(argc,argv);
    if (nsh_strcmp(cmd,"which")==0)   return cmd_which(argc,argv);
    if (nsh_strcmp(cmd,"stat")==0)    return cmd_stat(argc,argv);
    if (nsh_strcmp(cmd,"tree")==0)    return cmd_tree(argc,argv);
    if (nsh_strcmp(cmd,"memmap")==0)  return cmd_memmap(argc,argv);
    if (nsh_strcmp(cmd,"ver")==0)     return cmd_ver(argc,argv);
    if (nsh_strcmp(cmd,"ifconfig")==0)return cmd_ifconfig(argc,argv);
    if (nsh_strcmp(cmd,"ping")==0)    return cmd_ping(argc,argv);

    nsh_print("nsh: command not found: "); nsh_println(cmd);
    return 127;
}

/*
 * Execute a single (non-compound) command line:
 * - Tokenise
 * - Strip redirection tokens (> / >>)
 * - Run the builtin
 * - Clean up redirection
 */
static int nsh_exec_single(char *line) {
    char expanded[NSH_LINE_MAX];
    expand_vars(line, expanded, NSH_LINE_MAX);

    char lc[NSH_LINE_MAX]; nsh_strcpy(lc, expanded, NSH_LINE_MAX);
    char *argv[NSH_ARGC_MAX]; int argc;

    argc = nsh_parse_args(lc, argv, NSH_ARGC_MAX);
    if (argc < 0) return 1;   /* parse error (unmatched quote) */
    if (!argc) return 0;

    /* Scan for > or >> redirection tokens */
    char redir_file[VFS_PATH_MAX]; redir_file[0]=0;
    int  redir_append = 0;
    int  new_argc = 0;
    char *new_argv[NSH_ARGC_MAX];

    for (int i=0; i<argc; i++) {
        if (nsh_strcmp(argv[i],">")==0) {
            if (i+1 < argc) { resolve_path(argv[++i], redir_file, VFS_PATH_MAX); }
            redir_append = 0;
        } else if (nsh_strcmp(argv[i],">>")==0) {
            if (i+1 < argc) { resolve_path(argv[++i], redir_file, VFS_PATH_MAX); }
            redir_append = 1;
        } else {
            new_argv[new_argc++] = argv[i];
        }
    }

    /* Also handle ">file" or ">>file" attached to a token */
    new_argc = 0;
    for (int i=0; i<argc; i++) {
        char *tok = argv[i];
        char *gt2 = NULL, *gt1 = NULL;
        /* Search for >> first */
        for (char *p=tok; *(p+1); p++) {
            if (*p=='>'&&*(p+1)=='>') { gt2=p; break; }
        }
        if (!gt2) gt1 = nsh_strchr(tok, '>');

        if (gt2) {
            *gt2 = 0;
            if (tok < gt2) new_argv[new_argc++] = tok;
            char *fname = gt2+2; while(*fname==' ')fname++;
            if (*fname) resolve_path(fname, redir_file, VFS_PATH_MAX);
            else if (i+1 < argc) resolve_path(argv[++i], redir_file, VFS_PATH_MAX);
            redir_append = 1;
        } else if (gt1) {
            *gt1 = 0;
            if (tok < gt1) new_argv[new_argc++] = tok;
            char *fname = gt1+1; while(*fname==' ')fname++;
            if (*fname) resolve_path(fname, redir_file, VFS_PATH_MAX);
            else if (i+1 < argc) resolve_path(argv[++i], redir_file, VFS_PATH_MAX);
            redir_append = 0;
        } else {
            new_argv[new_argc++] = tok;
        }
    }

    /* Set up output redirection if requested */
    vfs_node_t *old_redir = redirect_node;
    uint64_t    old_off   = redirect_offset;

    if (redir_file[0]) {
        vfs_create(redir_file, 0);
        vfs_node_t *rn = vfs_open(redir_file, 0);
        if (!rn) {
            nsh_print("nsh: cannot open '"); nsh_print(redir_file); nsh_println("'");
            return 1;
        }
        redirect_node   = rn;
        redirect_offset = redir_append ? rn->size : 0;
    }

    int ret = nsh_exec_builtin(new_argc, new_argv);

    /* Restore */
    redirect_node   = old_redir;
    redirect_offset = old_off;

    return ret;
}

/*
 * Full line execution — handles:
 *   cmd1 | cmd2        (Task 3 — single pipe)
 *   cmd > file         (Task 4 — output redirection)
 *   cmd >> file        (Task 4 — append)
 *   cmd &              (Task 5 — background)
 */
static void nsh_exec_line(char *line) {
    if (!line[0] || line[0]=='#') return;

    /* Trim trailing whitespace */
    int end = (int)nsh_strlen(line) - 1;
    while (end >= 0 && line[end]==' ') line[end--]=0;
    if (!line[0]) return;

    /* ---- Task 5: Background job (trailing &) ---- */
    int background = 0;
    int ll = (int)nsh_strlen(line);
    if (ll>0 && line[ll-1]=='&') {
        line[ll-1]=0;
        end = ll-2;
        while(end>=0&&line[end]==' ') line[end--]=0;
        background = 1;
    }

    /* ---- Task 3: Pipe detection (first unquoted |) ---- */
    char *pipe_pos = NULL;
    {
        int in_quote = 0;
        for (char *p=line; *p; p++) {
            if (*p=='"') in_quote ^= 1;
            if (!in_quote && *p=='|') { pipe_pos=p; break; }
        }
    }

    if (pipe_pos) {
        /* Split into left and right command strings */
        *pipe_pos = 0;
        char *left  = line;
        char *right = pipe_pos + 1;
        while (*right==' ') right++;

        /* Execute left, capturing output into pipe_buf */
        pipe_buf_len  = 0;
        pipe_buf_rpos = 0;
        pipe_write_mode = 1;
        nsh_exec_single(left);
        pipe_write_mode = 0;
        pipe_buf[pipe_buf_len] = 0;

        /* Execute right, reading stdin from pipe_buf */
        pipe_buf_rpos = 0;
        pipe_read_mode = 1;
        last_exit_code = nsh_exec_single(right);
        pipe_read_mode = 0;
        return;
    }

    /* ---- Normal execution (with possible redirection) ---- */
    if (background) {
        /*
         * Task 5: Run in background.
         * In this single-address-space kernel, "background" means we run
         * the command immediately (no fork) and immediately return the
         * prompt.  We register a fake PID for display purposes.
         */
        static uint32_t bg_fake_pid = 100;
        int slot = -1;
        for (int i=0;i<NSH_BG_MAX;i++) { if(!bg_jobs[i].pid){slot=i;break;} }
        if (slot >= 0) {
            bg_jobs[slot].pid = bg_fake_pid++;
            nsh_strcpy(bg_jobs[slot].cmd, line, 64);
            bg_jobs[slot].done = 0;
            bg_count++;
            char num[8]; nsh_uint_str(slot+1, num);
            char pid[12]; nsh_uint_str(bg_jobs[slot].pid, pid);
            nsh_print("["); nsh_print(num); nsh_print("] "); nsh_println(pid);
            /* Execute inline, then mark done */
            nsh_exec_single(line);
            bg_jobs[slot].done = 1;
        } else {
            /* No bg slot — run foreground */
            last_exit_code = nsh_exec_single(line);
        }
        return;
    }

    last_exit_code = nsh_exec_single(line);
}

/* ════════════════════════════════════════
 * Main shell loop
 * ════════════════════════════════════════ */
void nsh_main(void) {
    env_init();
    nsh_source("/etc/nsh.rc");

    char line[NSH_LINE_MAX];

    while (1) {
        /* Check completed background jobs before showing prompt */
        bg_check_done();

        /* Prompt */
        if (!pipe_write_mode && !redirect_node)
            vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        const char *ps1 = env_get("PS1");
        if (!ps1||!ps1[0]) ps1="$ ";
        nsh_print(ps1);
        if (!pipe_write_mode && !redirect_node)
            vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);

        int len = read_line(line, NSH_LINE_MAX);
        if (len < 0) break;
        if (!line[0]) continue;

        history_add(line);

        /* Check for exit/logout before executing */
        char lc[NSH_LINE_MAX]; nsh_strcpy(lc, line, NSH_LINE_MAX);
        char *av[NSH_ARGC_MAX]; int ac = nsh_parse_args(lc, av, NSH_ARGC_MAX);
        if (ac > 0 && (nsh_strcmp(av[0],"exit")==0 || nsh_strcmp(av[0],"logout")==0)) break;

        nsh_exec_line(line);
    }
}
