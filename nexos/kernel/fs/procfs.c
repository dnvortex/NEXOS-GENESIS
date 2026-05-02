/* NexOS — kernel/fs/procfs.c | /proc virtual filesystem | MIT License */
#include "procfs.h"
#include "vfs.h"
#include "../kernel.h"
#include "../drivers/timer.h"
#include "../mm/pmm.h"
#include "../mm/heap.h"
#include "../proc/process.h"

#define PROCFS_BUF 2048

/* ---- helpers ---- */
static void p_uint(uint64_t n, char *buf) {
    if (!n) { buf[0]='0'; buf[1]=0; return; }
    char t[20]; int i=0;
    while(n){ t[i++]='0'+(int)(n%10); n/=10; }
    int j=0; while(i>0) buf[j++]=t[--i]; buf[j]=0;
}
static void p_cat(char *d, const char *s, size_t mx) {
    size_t dl=0; while(d[dl]) dl++;
    size_t i=0; while(dl+i<mx-1&&s[i]){d[dl+i]=s[i];i++;} d[dl+i]=0;
}

/* ---- node private data ---- */
#define PTYPE_VERSION  1
#define PTYPE_UPTIME   2
#define PTYPE_MEMINFO  3
#define PTYPE_PID_STAT 4

typedef struct { int type; uint32_t pid; } ppriv_t;

/* ---- read callback for virtual proc files ---- */
static uint32_t pfile_read(vfs_node_t *node, uint64_t offset,
                           uint32_t size, uint8_t *buf) {
    ppriv_t *pr = (ppriv_t *)node->priv;
    if (!pr) return 0;

    char tmp[PROCFS_BUF]; tmp[0]=0;
    char num[24];

    switch (pr->type) {

    case PTYPE_VERSION:
        p_cat(tmp, "NexOS 0.1 built 2026\n", PROCFS_BUF);
        break;

    case PTYPE_UPTIME: {
        uint64_t secs = timer_get_ticks() / 1000;
        p_uint(secs, num);
        p_cat(tmp, num, PROCFS_BUF);
        p_cat(tmp, " seconds\n", PROCFS_BUF);
        break;
    }

    case PTYPE_MEMINFO: {
        uint64_t total = pmm_get_total_memory() / 1024;
        uint64_t free  = pmm_get_free_memory()  / 1024;
        uint64_t used  = total - free;
        p_cat(tmp, "MemTotal: ", PROCFS_BUF);
        p_uint(total, num); p_cat(tmp, num, PROCFS_BUF); p_cat(tmp, " kB\n", PROCFS_BUF);
        p_cat(tmp, "MemFree:  ", PROCFS_BUF);
        p_uint(free,  num); p_cat(tmp, num, PROCFS_BUF); p_cat(tmp, " kB\n", PROCFS_BUF);
        p_cat(tmp, "MemUsed:  ", PROCFS_BUF);
        p_uint(used,  num); p_cat(tmp, num, PROCFS_BUF); p_cat(tmp, " kB\n", PROCFS_BUF);
        break;
    }

    case PTYPE_PID_STAT: {
        process_t *p = proc_get_by_pid(pr->pid);
        if (!p) return 0;
        static const char *states[]={"running","ready","blocked","zombie","dead"};
        int st = (int)p->state;
        p_cat(tmp, "PID:   ", PROCFS_BUF);
        p_uint(p->pid, num); p_cat(tmp, num, PROCFS_BUF); p_cat(tmp, "\n", PROCFS_BUF);
        p_cat(tmp, "Name:  ", PROCFS_BUF); p_cat(tmp, p->name, PROCFS_BUF); p_cat(tmp,"\n",PROCFS_BUF);
        p_cat(tmp, "State: ", PROCFS_BUF);
        p_cat(tmp, states[st<5?st:4], PROCFS_BUF); p_cat(tmp,"\n",PROCFS_BUF);
        p_cat(tmp, "PPID:  ", PROCFS_BUF);
        p_uint(p->ppid, num); p_cat(tmp, num, PROCFS_BUF); p_cat(tmp,"\n",PROCFS_BUF);
        break;
    }

    default: return 0;
    }

    size_t tlen=0; while(tmp[tlen]) tlen++;
    if (offset >= tlen) return 0;
    uint32_t avail = (uint32_t)(tlen - offset);
    if (avail > size) avail = size;
    for (uint32_t i=0; i<avail; i++) buf[i]=(uint8_t)tmp[offset+i];
    return avail;
}

static vfs_node_t *make_pfile(const char *name, int type, uint32_t pid) {
    vfs_node_t *n = (vfs_node_t *)kmalloc(sizeof(vfs_node_t));
    if (!n) return NULL;
    uint8_t *z=(uint8_t*)n; for(size_t i=0;i<sizeof(vfs_node_t);i++) z[i]=0;

    ppriv_t *pr = (ppriv_t *)kmalloc(sizeof(ppriv_t));
    if (!pr) { kfree(n); return NULL; }
    pr->type = type; pr->pid = pid;

    int ni=0; while(name[ni]&&ni<VFS_NAME_MAX-1){n->name[ni]=name[ni];ni++;} n->name[ni]=0;
    n->type = VFS_NODE_FILE;
    n->priv = pr;
    n->read = pfile_read;
    return n;
}

/* ---- directory ---- */
#define PDIR_MAX_CHILDREN 32
typedef struct { vfs_node_t *children[PDIR_MAX_CHILDREN]; int count; } pdir_t;

static int pdir_readdir(vfs_node_t *node, uint32_t idx, vfs_dirent_t *out) {
    pdir_t *d = (pdir_t *)node->priv;
    if (!d || idx >= (uint32_t)d->count) return -1;
    vfs_node_t *c = d->children[idx];
    if (!c) return -1;
    out->inode = c->inode;
    int ni=0; while(c->name[ni]&&ni<VFS_NAME_MAX-1){out->name[ni]=c->name[ni];ni++;} out->name[ni]=0;
    return 0;
}

static vfs_node_t *pdir_finddir(vfs_node_t *node, const char *name) {
    pdir_t *d = (pdir_t *)node->priv;
    if (!d) return NULL;
    for (int i=0;i<d->count;i++) {
        vfs_node_t *c = d->children[i]; if(!c) continue;
        int ni=0,ok=1;
        while(c->name[ni]&&name[ni]){if(c->name[ni]!=name[ni]){ok=0;break;}ni++;}
        if(ok&&!c->name[ni]&&!name[ni]) return c;
    }
    return NULL;
}

static vfs_node_t *make_pdir(const char *name) {
    vfs_node_t *n = (vfs_node_t *)kmalloc(sizeof(vfs_node_t));
    if (!n) return NULL;
    uint8_t *z=(uint8_t*)n; for(size_t i=0;i<sizeof(vfs_node_t);i++) z[i]=0;

    pdir_t *d = (pdir_t *)kmalloc(sizeof(pdir_t));
    if (!d) { kfree(n); return NULL; }
    uint8_t *zd=(uint8_t*)d; for(size_t i=0;i<sizeof(pdir_t);i++) zd[i]=0;

    int ni=0; while(name[ni]&&ni<VFS_NAME_MAX-1){n->name[ni]=name[ni];ni++;} n->name[ni]=0;
    n->type    = VFS_NODE_DIR;
    n->priv    = d;
    n->readdir = pdir_readdir;
    n->finddir = pdir_finddir;
    return n;
}

static void pdir_add(vfs_node_t *dir, vfs_node_t *child) {
    pdir_t *d = (pdir_t *)dir->priv;
    if (!d || d->count >= PDIR_MAX_CHILDREN) return;
    d->children[d->count++] = child;
}

/* ---- proc root: static + dynamic PID dirs ---- */
static vfs_node_t *proc_root = NULL;

static int procroot_readdir(vfs_node_t *node, uint32_t idx, vfs_dirent_t *out) {
    pdir_t *d = (pdir_t *)node->priv;
    if (!d) return -1;

    /* Static entries first */
    if (idx < (uint32_t)d->count) {
        vfs_node_t *c = d->children[idx]; if(!c) return -1;
        out->inode = c->inode;
        int ni=0; while(c->name[ni]&&ni<VFS_NAME_MAX-1){out->name[ni]=c->name[ni];ni++;} out->name[ni]=0;
        return 0;
    }

    /* Dynamic PID directories */
    uint32_t pi = idx - (uint32_t)d->count;
    uint32_t found = 0;
    for (int i=0; i<MAX_PROCESSES; i++) {
        if (!processes[i]) continue;
        if (found == pi) {
            char ps[16];
            p_uint(processes[i]->pid, ps);
            out->inode = processes[i]->pid;
            int ni=0; while(ps[ni]&&ni<VFS_NAME_MAX-1){out->name[ni]=ps[ni];ni++;} out->name[ni]=0;
            return 0;
        }
        found++;
    }
    return -1;
}

static vfs_node_t *procroot_finddir(vfs_node_t *node, const char *name) {
    /* Check static entries */
    pdir_t *d = (pdir_t *)node->priv;
    if (d) {
        for (int i=0;i<d->count;i++) {
            vfs_node_t *c=d->children[i]; if(!c) continue;
            int ni=0,ok=1;
            while(c->name[ni]&&name[ni]){if(c->name[ni]!=name[ni]){ok=0;break;}ni++;}
            if(ok&&!c->name[ni]&&!name[ni]) return c;
        }
    }

    /* Try parsing as PID number */
    uint32_t pid=0;
    const char *p=name;
    while(*p>='0'&&*p<='9'){pid=pid*10+(*p-'0');p++;}
    if (!*p && pid>0 && proc_get_by_pid(pid)) {
        /* Build on-the-fly pid dir */
        vfs_node_t *pd = make_pdir(name);
        if (!pd) return NULL;
        vfs_node_t *status = make_pfile("status", PTYPE_PID_STAT, pid);
        if (status) pdir_add(pd, status);
        return pd;
    }
    return NULL;
}

void procfs_init(void) {
    proc_root = make_pdir("proc");
    if (!proc_root) { klog(LOG_ERROR, "procfs: failed to allocate root"); return; }

    /* Override for dynamic PID enumeration */
    proc_root->readdir = procroot_readdir;
    proc_root->finddir = procroot_finddir;

    /* Static virtual files */
    vfs_node_t *ver = make_pfile("version", PTYPE_VERSION, 0);
    if (ver) pdir_add(proc_root, ver);

    vfs_node_t *upt = make_pfile("uptime", PTYPE_UPTIME, 0);
    if (upt) pdir_add(proc_root, upt);

    vfs_node_t *mem = make_pfile("meminfo", PTYPE_MEMINFO, 0);
    if (mem) pdir_add(proc_root, mem);

    vfs_mount("/proc", proc_root);
    klog(LOG_INFO, "procfs: mounted at /proc");
}

vfs_node_t *procfs_get_root(void) { return proc_root; }
