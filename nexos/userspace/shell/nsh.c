/* NexOS — userspace/shell/nsh.c | NexOS Shell (nsh) | MIT License */
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

/* ---- String helpers ---- */
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
    uint8_t *b=(uint8_t*)p; for(size_t i=0;i<n;i++) b[i]=v;
}
static char *nsh_strcat(char *d, const char *s, size_t max) {
    size_t dl = nsh_strlen(d);
    size_t i = 0;
    while(dl+i < max-1 && s[i]) { d[dl+i]=s[i]; i++; }
    d[dl+i]=0; return d;
}

/* ---- VGA print helpers ---- */
static void nsh_putchar(char c) { vga_putchar(c); }
static void nsh_print(const char *s) { vga_puts(s); }
static void nsh_println(const char *s) { vga_puts(s); vga_putchar('\n'); }

/* Number to string */
static void nsh_uint_str(uint64_t n, char *buf) {
    if (n == 0) { buf[0]='0'; buf[1]=0; return; }
    char tmp[20]; int i=0;
    while(n){ tmp[i++]='0'+(int)(n%10); n/=10; }
    int j=0; while(i>0) buf[j++]=tmp[--i];
    buf[j]=0;
}
static void nsh_int_str(int64_t n, char *buf) {
    if (n<0) { buf[0]='-'; nsh_uint_str((uint64_t)(-n), buf+1); }
    else nsh_uint_str((uint64_t)n, buf);
}

/* ---- History ---- */
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

/* ---- Environment variables ---- */
#define NSH_ENV_MAX 64
#define NSH_ENV_NAMELEN 64
#define NSH_ENV_VALLEN  256

typedef struct { char name[NSH_ENV_NAMELEN]; char value[NSH_ENV_VALLEN]; } env_var_t;
static env_var_t env_vars[NSH_ENV_MAX];
static int       env_count = 0;
static int       last_exit_code = 0;

static void env_set(const char *name, const char *value) {
    for (int i = 0; i < env_count; i++) {
        if (nsh_strcmp(env_vars[i].name, name)==0) {
            nsh_strcpy(env_vars[i].value, value, NSH_ENV_VALLEN);
            return;
        }
    }
    if (env_count < NSH_ENV_MAX) {
        nsh_strcpy(env_vars[env_count].name,  name,  NSH_ENV_NAMELEN);
        nsh_strcpy(env_vars[env_count].value, value, NSH_ENV_VALLEN);
        env_count++;
    }
}

static const char *env_get(const char *name) {
    /* Special: $? = last exit code */
    if (nsh_strcmp(name, "?")==0) {
        static char ec[8];
        nsh_int_str(last_exit_code, ec);
        return ec;
    }
    for (int i = 0; i < env_count; i++)
        if (nsh_strcmp(env_vars[i].name, name)==0) return env_vars[i].value;
    return "";
}

static void env_init(void) {
    env_set("PATH",  "/bin:/usr/bin");
    env_set("HOME",  "/home/user");
    env_set("USER",  "root");
    env_set("PS1",   "[root@nexos ~]$ ");
    env_set("SHELL", "/bin/nsh");
}

/* ---- Variable expansion ---- */
static void expand_vars(const char *in, char *out, size_t max) {
    size_t i=0, j=0;
    while(in[i] && j < max-1) {
        if (in[i]=='$') {
            i++;
            char varname[NSH_ENV_NAMELEN];
            int vn=0;
            if (in[i]=='?') { varname[0]='?'; varname[1]=0; i++; }
            else while(in[i] && (in[i]=='_' || (in[i]>='a'&&in[i]<='z') ||
                    (in[i]>='A'&&in[i]<='Z') || (in[i]>='0'&&in[i]<='9')) && vn < NSH_ENV_NAMELEN-1)
                varname[vn++]=in[i++];
            varname[vn]=0;
            const char *val = env_get(varname);
            while(*val && j<max-1) out[j++]=*val++;
        } else {
            out[j++]=in[i++];
        }
    }
    out[j]=0;
}

/* ---- Working directory ---- */
static char nsh_cwd[1024] = "/";

/* ---- Argument parsing ---- */
#define NSH_ARGC_MAX 32

static int nsh_parse_args(char *line, char *argv[], int max_argc) {
    int argc = 0;
    char *p = line;

    while (*p == ' ') p++;
    while (*p && argc < max_argc) {
        if (*p == '"') {
            p++;
            argv[argc++] = p;
            while (*p && *p != '"') p++;
            if (*p == '"') { *p = 0; p++; }
        } else {
            argv[argc++] = p;
            while (*p && *p != ' ') p++;
        }
        if (*p == ' ') { *p = 0; p++; }
        while (*p == ' ') p++;
    }
    return argc;
}

/* ---- Path resolution ---- */
static void resolve_path(const char *path, char *out, size_t max) {
    if (path[0] == '/') {
        nsh_strcpy(out, path, max);
        return;
    }
    /* Relative path */
    nsh_strcpy(out, nsh_cwd, max);
    if (out[nsh_strlen(out)-1] != '/') nsh_strcat(out, "/", max);
    nsh_strcat(out, path, max);

    /* Handle '..' and '.' */
    char resolved[1024];
    char *parts[64];
    int pc = 0;
    char tmp[1024];
    nsh_strcpy(tmp, out, 1024);

    char *p = tmp;
    while (*p) {
        while (*p == '/') p++;
        if (!*p) break;
        char *start = p;
        while (*p && *p != '/') p++;
        if (*p == '/') *p++ = 0;
        if (nsh_strcmp(start, ".")==0) continue;
        if (nsh_strcmp(start, "..")==0) { if (pc>0) pc--; }
        else parts[pc++] = start;
    }

    resolved[0] = 0;
    for (int i = 0; i < pc; i++) {
        nsh_strcat(resolved, "/", 1024);
        nsh_strcat(resolved, parts[i], 1024);
    }
    if (resolved[0]==0) resolved[0]='/', resolved[1]=0;
    nsh_strcpy(out, resolved, max);
}

/* ---- Command helpers ---- */
static void print_size(uint64_t sz) {
    char buf[32];
    if (sz < 1024) { nsh_uint_str(sz, buf); nsh_print(buf); nsh_print(" B"); }
    else if (sz < 1024*1024) { nsh_uint_str(sz/1024, buf); nsh_print(buf); nsh_print(" KB"); }
    else { nsh_uint_str(sz/(1024*1024), buf); nsh_print(buf); nsh_print(" MB"); }
}

/* ---- Built-in commands ---- */

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
    for (uint32_t i = 0; vfs_readdir(node, i, &dirent) == 0; i++) {
        char child_path[1024];
        nsh_strcpy(child_path, path, 1024);
        if (child_path[nsh_strlen(child_path)-1] != '/') nsh_strcat(child_path, "/", 1024);
        nsh_strcat(child_path, dirent.name, 1024);
        vfs_node_t *child = vfs_open(child_path, 0);

        if (child && (child->type & VFS_NODE_DIR)) {
            vga_set_color(VGA_COLOR_LIGHT_BLUE, VGA_COLOR_BLACK);
            nsh_print(dirent.name);
            vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
            nsh_print("/");
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
        nsh_print("cd: '"); nsh_print(path); nsh_println("': not a directory");
        return 1;
    }
    nsh_strcpy(nsh_cwd, path, 1024);
    return 0;
}

static int cmd_pwd(int argc, char *argv[]) {
    (void)argc; (void)argv;
    nsh_println(nsh_cwd);
    return 0;
}

static int cmd_cat(int argc, char *argv[]) {
    if (argc < 2) { nsh_println("cat: missing file argument"); return 1; }
    char path[1024];
    resolve_path(argv[1], path, 1024);
    vfs_node_t *node = vfs_open(path, 0);
    if (!node) { nsh_print("cat: '"); nsh_print(path); nsh_println("': no such file"); return 1; }

    uint8_t buf[512];
    uint64_t offset = 0;
    uint32_t n;
    while ((n = vfs_read(node, offset, sizeof(buf)-1, buf)) > 0) {
        buf[n] = 0;
        nsh_print((char *)buf);
        offset += n;
    }
    return 0;
}

static int cmd_echo(int argc, char *argv[]) {
    for (int i = 1; i < argc; i++) {
        char expanded[NSH_LINE_MAX];
        expand_vars(argv[i], expanded, NSH_LINE_MAX);
        if (i > 1) nsh_putchar(' ');
        nsh_print(expanded);
    }
    nsh_putchar('\n');
    return 0;
}

static int cmd_mkdir(int argc, char *argv[]) {
    if (argc < 2) { nsh_println("mkdir: missing argument"); return 1; }
    char path[1024];
    resolve_path(argv[1], path, 1024);
    if (vfs_mkdir(path) < 0) { nsh_print("mkdir: cannot create '"); nsh_print(path); nsh_println("'"); return 1; }
    return 0;
}

static int cmd_rm(int argc, char *argv[]) {
    if (argc < 2) { nsh_println("rm: missing argument"); return 1; }
    char path[1024];
    resolve_path(argv[1], path, 1024);
    if (vfs_unlink(path) < 0) { nsh_print("rm: cannot remove '"); nsh_print(path); nsh_println("'"); return 1; }
    return 0;
}

static int cmd_touch(int argc, char *argv[]) {
    if (argc < 2) { nsh_println("touch: missing argument"); return 1; }
    char path[1024];
    resolve_path(argv[1], path, 1024);
    if (vfs_create(path, 0) < 0) {
        /* File may already exist — that's ok */
    }
    return 0;
}

static int cmd_cp(int argc, char *argv[]) {
    if (argc < 3) { nsh_println("cp: usage: cp <src> <dst>"); return 1; }
    char src[1024], dst[1024];
    resolve_path(argv[1], src, 1024);
    resolve_path(argv[2], dst, 1024);

    vfs_node_t *snode = vfs_open(src, 0);
    if (!snode) { nsh_print("cp: '"); nsh_print(src); nsh_println("': not found"); return 1; }

    vfs_create(dst, 0);
    vfs_node_t *dnode = vfs_open(dst, 0);
    if (!dnode) { nsh_print("cp: cannot create '"); nsh_print(dst); nsh_println("'"); return 1; }

    uint8_t buf[512];
    uint64_t offset = 0;
    uint32_t n;
    while ((n = vfs_read(snode, offset, sizeof(buf), buf)) > 0) {
        vfs_write(dnode, offset, n, buf);
        offset += n;
    }
    return 0;
}

static int cmd_mv(int argc, char *argv[]) {
    if (argc < 3) { nsh_println("mv: usage: mv <src> <dst>"); return 1; }
    cmd_cp(argc, argv);
    char src[1024];
    resolve_path(argv[1], src, 1024);
    vfs_unlink(src);
    return 0;
}

static int cmd_clear(int argc, char *argv[]) {
    (void)argc; (void)argv;
    vga_clear();
    return 0;
}

static int cmd_help(int argc, char *argv[]) {
    (void)argc; (void)argv;
    vga_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    nsh_println("NexOS Shell (nsh) — Built-in Commands:");
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    nsh_println("  ls [path]         List directory contents");
    nsh_println("  cd [path]         Change directory");
    nsh_println("  pwd               Print working directory");
    nsh_println("  cat <file>        Display file contents");
    nsh_println("  echo [text]       Print text (supports $VAR)");
    nsh_println("  mkdir <dir>       Create directory");
    nsh_println("  rm <file>         Delete file");
    nsh_println("  cp <src> <dst>    Copy file");
    nsh_println("  mv <src> <dst>    Move/rename file");
    nsh_println("  touch <file>      Create empty file");
    nsh_println("  clear             Clear screen");
    nsh_println("  env               Show environment variables");
    nsh_println("  export KEY=VALUE  Set environment variable");
    nsh_println("  uname             Print OS info");
    nsh_println("  uptime            Show system uptime");
    nsh_println("  ps                List processes");
    nsh_println("  kill <pid>        Kill a process");
    nsh_println("  free              Show memory usage");
    nsh_println("  date              Show current date/time");
    nsh_println("  mount             Show mount points");
    nsh_println("  reboot            Reboot the system");
    nsh_println("  halt              Halt the system");
    nsh_println("  help              Show this help");
    nsh_println("  exit / logout     Exit the shell");
    return 0;
}

static int cmd_env(int argc, char *argv[]) {
    (void)argc; (void)argv;
    for (int i = 0; i < env_count; i++) {
        nsh_print(env_vars[i].name);
        nsh_putchar('=');
        nsh_println(env_vars[i].value);
    }
    return 0;
}

static int cmd_export(int argc, char *argv[]) {
    if (argc < 2) { nsh_println("export: usage: export KEY=VALUE"); return 1; }
    char *eq = argv[1];
    while (*eq && *eq != '=') eq++;
    if (!*eq) { nsh_println("export: missing '='"); return 1; }
    *eq = 0;
    env_set(argv[1], eq + 1);
    return 0;
}

static int cmd_uname(int argc, char *argv[]) {
    (void)argc; (void)argv;
    nsh_println("NexOS 0.1.0 x86_64 MIT");
    return 0;
}

static int cmd_uptime(int argc, char *argv[]) {
    (void)argc; (void)argv;
    char buf[32];
    uint64_t secs = timer_get_uptime_seconds();
    uint64_t mins = secs / 60;
    uint64_t hrs  = mins / 60;
    nsh_print("up ");
    nsh_uint_str(hrs, buf); nsh_print(buf); nsh_print("h ");
    nsh_uint_str(mins % 60, buf); nsh_print(buf); nsh_print("m ");
    nsh_uint_str(secs % 60, buf); nsh_print(buf); nsh_println("s");
    return 0;
}

static int cmd_ps(int argc, char *argv[]) {
    (void)argc; (void)argv;
    static const char *states[] = {"RUNNING","READY","BLOCKED","ZOMBIE","DEAD"};
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    nsh_println("  PID  STATE    NAME");
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (!processes[i]) continue;
        char pidbuf[8];
        nsh_uint_str(processes[i]->pid, pidbuf);
        nsh_print("  "); nsh_print(pidbuf);
        nsh_print("  ");
        int st = (int)processes[i]->state;
        nsh_print(states[st < 5 ? st : 4]);
        nsh_print("  ");
        nsh_println(processes[i]->name);
    }
    return 0;
}

static int cmd_kill(int argc, char *argv[]) {
    if (argc < 2) { nsh_println("kill: usage: kill <pid>"); return 1; }
    uint32_t pid = 0;
    char *s = argv[1];
    while (*s >= '0' && *s <= '9') { pid = pid*10 + (*s-'0'); s++; }
    proc_kill(pid);
    return 0;
}

static int cmd_free(int argc, char *argv[]) {
    (void)argc; (void)argv;
    char buf[32];
    uint64_t total = pmm_get_total_memory() / 1024;
    uint64_t free  = pmm_get_free_memory()  / 1024;
    uint64_t used  = total - free;
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    nsh_println("              total        used        free");
    nsh_print("Mem:   ");
    nsh_uint_str(total, buf); nsh_print(buf); nsh_print(" KB    ");
    nsh_uint_str(used,  buf); nsh_print(buf); nsh_print(" KB    ");
    nsh_uint_str(free,  buf); nsh_print(buf); nsh_println(" KB");
    return 0;
}

static int cmd_date(int argc, char *argv[]) {
    (void)argc; (void)argv;
    rtc_time_t t;
    rtc_get_time(&t);
    char buf[32];
    rtc_time_to_string(buf, &t);
    nsh_println(buf);
    return 0;
}

static int cmd_mount(int argc, char *argv[]) {
    (void)argc; (void)argv;
    nsh_println("Mounted filesystems:");
    nsh_println("  /          ramfs");
    nsh_println("  /tmp       ramfs");
    nsh_println("  /mnt       fat32 (if disk present)");
    return 0;
}

static int cmd_reboot(int argc, char *argv[]) {
    (void)argc; (void)argv;
    nsh_println("Rebooting...");
    /* Keyboard controller reset */
    io_outb(0x64, 0xFE);
    for (;;) hlt();
    return 0;
}

static int cmd_halt(int argc, char *argv[]) {
    (void)argc; (void)argv;
    nsh_println("System halted.");
    cli();
    for (;;) hlt();
    return 0;
}

/* ---- Tab completion ---- */
static const char *builtin_names[] = {
    "ls","cd","pwd","cat","echo","mkdir","rm","cp","mv","touch","clear",
    "help","env","export","uname","uptime","ps","kill","free","date",
    "mount","reboot","halt","exit","logout",NULL
};

static void tab_complete(char *line, int *len) {
    if (*len == 0) return;

    /* Find the last word */
    int start = *len - 1;
    while (start > 0 && line[start-1] != ' ') start--;
    char *partial = line + start;
    int partial_len = *len - start;

    /* Find first match */
    for (int i = 0; builtin_names[i]; i++) {
        if (nsh_strncmp(builtin_names[i], partial, (size_t)partial_len)==0) {
            int match_len = (int)nsh_strlen(builtin_names[i]);
            for (int j = partial_len; j < match_len; j++) {
                line[start + j] = builtin_names[i][j];
                vga_putchar(builtin_names[i][j]);
            }
            *len = start + match_len;
            break;
        }
    }

    /* Try VFS path completion */
    /* (simplified: just try the partial as a path) */
    char path[1024];
    resolve_path(partial, path, 1024);
    /* Find parent dir */
    char *slash = (char *)partial; char *last_slash = NULL;
    for (char *p = partial; *p; p++) if (*p=='/') last_slash=p;
    if (last_slash) {
        char parent[1024];
        int parent_len = (int)(last_slash - partial);
        for (int i=0; i<parent_len; i++) parent[i]=partial[i];
        parent[parent_len]=0;
        char parent_path[1024];
        resolve_path(parent, parent_path, 1024);

        vfs_node_t *dir = vfs_open(parent_path, 0);
        if (dir && (dir->type & VFS_NODE_DIR)) {
            const char *name_part = last_slash + 1;
            int name_part_len = (int)nsh_strlen(name_part);
            vfs_dirent_t dirent;
            for (uint32_t i = 0; vfs_readdir(dir, i, &dirent)==0; i++) {
                if (nsh_strncmp(dirent.name, name_part, (size_t)name_part_len)==0) {
                    for (int j = name_part_len; dirent.name[j]; j++) {
                        line[*len] = dirent.name[j];
                        vga_putchar(dirent.name[j]);
                        (*len)++;
                    }
                    break;
                }
            }
        }
    }
    (void)slash;
}

/* ---- Line editor ---- */
static int read_line(char *buf, size_t max) {
    int len = 0;
    hist_pos = -1;

    buf[0] = 0;

    while (1) {
        char ch = keyboard_getchar();

        /* Special keys from scan codes are mapped to ASCII surrogates:
           For simplicity, map via known sequences */

        if (ch == '\n' || ch == '\r') {
            vga_putchar('\n');
            buf[len] = 0;
            break;
        } else if (ch == '\b') {
            if (len > 0) {
                len--;
                buf[len] = 0;
                vga_putchar('\b');
                vga_putchar(' ');
                vga_putchar('\b');
            }
        } else if (ch == '\t') {
            buf[len] = 0;
            tab_complete(buf, &len);
        } else if (ch >= 32 && ch < 127) {
            if ((size_t)len < max - 1) {
                buf[len++] = ch;
                buf[len]   = 0;
                vga_putchar(ch);
            }
        }
    }
    return len;
}

/* ---- Source a script file ---- */
static void nsh_source(const char *path);

/* ---- Command dispatcher ---- */
static int nsh_exec_builtin(int argc, char *argv[]) {
    if (!argc) return 0;
    const char *cmd = argv[0];

    if (nsh_strcmp(cmd,"ls")==0)     return cmd_ls(argc, argv);
    if (nsh_strcmp(cmd,"cd")==0)     return cmd_cd(argc, argv);
    if (nsh_strcmp(cmd,"pwd")==0)    return cmd_pwd(argc, argv);
    if (nsh_strcmp(cmd,"cat")==0)    return cmd_cat(argc, argv);
    if (nsh_strcmp(cmd,"echo")==0)   return cmd_echo(argc, argv);
    if (nsh_strcmp(cmd,"mkdir")==0)  return cmd_mkdir(argc, argv);
    if (nsh_strcmp(cmd,"rm")==0)     return cmd_rm(argc, argv);
    if (nsh_strcmp(cmd,"touch")==0)  return cmd_touch(argc, argv);
    if (nsh_strcmp(cmd,"cp")==0)     return cmd_cp(argc, argv);
    if (nsh_strcmp(cmd,"mv")==0)     return cmd_mv(argc, argv);
    if (nsh_strcmp(cmd,"clear")==0)  return cmd_clear(argc, argv);
    if (nsh_strcmp(cmd,"help")==0)   return cmd_help(argc, argv);
    if (nsh_strcmp(cmd,"env")==0)    return cmd_env(argc, argv);
    if (nsh_strcmp(cmd,"export")==0) return cmd_export(argc, argv);
    if (nsh_strcmp(cmd,"uname")==0)  return cmd_uname(argc, argv);
    if (nsh_strcmp(cmd,"uptime")==0) return cmd_uptime(argc, argv);
    if (nsh_strcmp(cmd,"ps")==0)     return cmd_ps(argc, argv);
    if (nsh_strcmp(cmd,"kill")==0)   return cmd_kill(argc, argv);
    if (nsh_strcmp(cmd,"free")==0)   return cmd_free(argc, argv);
    if (nsh_strcmp(cmd,"date")==0)   return cmd_date(argc, argv);
    if (nsh_strcmp(cmd,"mount")==0)  return cmd_mount(argc, argv);
    if (nsh_strcmp(cmd,"reboot")==0) return cmd_reboot(argc, argv);
    if (nsh_strcmp(cmd,"halt")==0)   return cmd_halt(argc, argv);
    if (nsh_strcmp(cmd,"exit")==0 || nsh_strcmp(cmd,"logout")==0) return -1;

    nsh_print("nsh: command not found: ");
    nsh_println(cmd);
    return 127;
}

static void nsh_exec_line(char *line) {
    if (!line[0] || line[0]=='#') return;

    char expanded[NSH_LINE_MAX];
    expand_vars(line, expanded, NSH_LINE_MAX);

    char *argv[NSH_ARGC_MAX];
    char linecopy[NSH_LINE_MAX];
    nsh_strcpy(linecopy, expanded, NSH_LINE_MAX);

    int argc = nsh_parse_args(linecopy, argv, NSH_ARGC_MAX);
    if (!argc) return;

    last_exit_code = nsh_exec_builtin(argc, argv);
}

static void nsh_source(const char *path) {
    vfs_node_t *node = vfs_open(path, 0);
    if (!node) return;

    uint8_t buf[2048];
    uint32_t n = vfs_read(node, 0, sizeof(buf)-1, buf);
    buf[n] = 0;

    /* Execute line by line */
    char *p = (char *)buf;
    while (*p) {
        char line[NSH_LINE_MAX];
        int i = 0;
        while (*p && *p != '\n' && i < NSH_LINE_MAX-1) line[i++] = *p++;
        line[i] = 0;
        if (*p == '\n') p++;
        nsh_exec_line(line);
    }
}

/* ---- Main shell loop ---- */
void nsh_main(void) {
    env_init();

    /* Source startup script */
    nsh_source("/etc/nsh.rc");

    char line[NSH_LINE_MAX];

    while (1) {
        /* Print prompt */
        vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        const char *ps1 = env_get("PS1");
        if (!ps1 || !ps1[0]) ps1 = "$ ";
        nsh_print(ps1);
        vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);

        /* Read input */
        int len = read_line(line, NSH_LINE_MAX);
        if (len < 0) break;
        if (!line[0]) continue;

        /* Add to history */
        history_add(line);

        /* Execute */
        int ret = 0;
        /* Check for exit */
        char linecopy[NSH_LINE_MAX];
        nsh_strcpy(linecopy, line, NSH_LINE_MAX);
        char *argv2[NSH_ARGC_MAX];
        char lc2[NSH_LINE_MAX];
        nsh_strcpy(lc2, line, NSH_LINE_MAX);
        int argc2 = nsh_parse_args(lc2, argv2, NSH_ARGC_MAX);
        if (argc2 > 0 && (nsh_strcmp(argv2[0],"exit")==0 || nsh_strcmp(argv2[0],"logout")==0)) break;
        (void)ret; (void)linecopy;

        nsh_exec_line(line);
    }
}
