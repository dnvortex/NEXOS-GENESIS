/* NexOS — kernel/gui/sysmon_app.c | System Monitor | MIT License */
#include "sysmon_app.h"
#include "wm.h"
#include "../drivers/fb.h"
#include "../drivers/font.h"
#include "../drivers/timer.h"
#include "../drivers/rtc.h"
#include "../mm/pmm.h"
#include "../mm/heap.h"
#include "../proc/process.h"
#include <stdint.h>
#include <stddef.h>

void *kmalloc(size_t sz);
void  kfree(void *p);

static void mzero(void *p, int n){uint8_t *b=(uint8_t*)p;for(int i=0;i<n;i++)b[i]=0;}

/* ── Number helpers ──────────────────────────────────────────────────────── */
static void u64s(uint64_t v, char *buf) {
    char t[24]; int ti=0;
    if(!v){t[ti++]='0';}else while(v){t[ti++]='0'+(int)(v%10);v/=10;}
    int bi=0; while(ti>0) buf[bi++]=t[--ti]; buf[bi]=0;
}
static int mlen(const char *s){int n=0;while(s[n])n++;return n;}
static void mcpy(char *d,const char *s,int max){int i=0;while(i<max-1&&s[i]){d[i]=s[i];i++;}d[i]=0;}

static void fmt_mem(uint64_t bytes, char *buf) {
    uint64_t mb = bytes / (1024*1024);
    uint64_t kb = (bytes % (1024*1024)) / 1024;
    char ms[12], ks[12];
    u64s(mb, ms); u64s(kb, ks);
    int bi=0;
    const char *p=ms; while(*p) buf[bi++]=*p++;
    buf[bi++]='.';
    if (ks[0]=='0'&&!ks[1]){buf[bi++]='0';buf[bi++]='0';}
    else { if(kb<100){buf[bi++]='0';} if(kb<10){buf[bi++]='0';} p=ks; while(*p) buf[bi++]=*p++; }
    const char *m=" MB"; while(*m) buf[bi++]=*m++;
    buf[bi]=0;
}

static void fmt_uptime(uint64_t s, char *buf) {
    uint64_t h=s/3600; s%=3600; uint64_t m=s/60; s%=60;
    char t[8]; int bi=0;
    u64s(h,t); const char *p=t; while(*p) buf[bi++]=*p++;
    buf[bi++]='h'; buf[bi++]=' ';
    u64s(m,t); p=t; while(*p) buf[bi++]=*p++;
    buf[bi++]='m'; buf[bi++]=' ';
    u64s(s,t); p=t; while(*p) buf[bi++]=*p++;
    buf[bi++]='s'; buf[bi]=0;
}

/* ── Sparkline ───────────────────────────────────────────────────────────── */
static void draw_sparkline(int sx, int sy, int sw, int sh,
                            const uint8_t *hist, int hlen, int pos,
                            uint32_t col, uint32_t bg) {
    fb_fill_rect(sx, sy, sw, sh, bg);
    if (hlen < 2) return;
    int step = (sw > hlen) ? sw / hlen : 1;
    int x = sx;
    for (int i = 0; i < hlen; i++) {
        int idx = (pos - hlen + i + SYSMON_HISTORY) % SYSMON_HISTORY;
        int hv  = (int)hist[idx] * sh / 100;
        if (hv < 0) hv = 0;
        if (hv > sh) hv = sh;
        fb_fill_rect(x, sy + sh - hv, step, hv, col);
        x += step;
        if (x >= sx + sw) break;
    }
}

/* ── Section header helper ───────────────────────────────────────────────── */
static void section_hdr(int wx, int y, int ww, const char *title) {
    fb_fill_rect(wx+14, y+8, 3, 12, COL_BLUE);
    font_puts(wx+22, y+7, title, COL_SUBTEXT, COL_BASE);
    fb_fill_rect(wx+22 + mlen(title)*8 + 6, y+12, ww-22-mlen(title)*8-20, 1, COL_SURFACE1);
}

/* ── Usage bar ───────────────────────────────────────────────────────────── */
static void usage_bar(int bx, int by, int bw, int bh, int pct,
                       uint32_t col, const char *label) {
    fb_fill_rounded_rect(bx, by, bw, bh, 4, COL_SURFACE0);
    if (pct > 100) pct = 100;
    int fill = bw * pct / 100;
    if (fill > 2) {
        fb_fill_rounded_rect(bx, by, fill, bh, 4, col);
        fb_fill_rect_blend(bx, by, fill, bh/2, 0xFFFFFF, 20);
    }
    /* Percent text */
    char ps[8]; int pi=0; u64s((uint64_t)pct, ps);
    char pb[12]; int pbi=0;
    const char *pp=ps; while(*pp) pb[pbi++]=*pp++;
    pb[pbi++]='%'; pb[pbi]=0;
    int tx = bx + bw + 6;
    font_puts(tx, by+bh/2-4, pb, COL_SUBTEXT, COL_BASE);
    if (label) font_puts(bx+4, by+bh/2-4, label, COL_TEXT, COL_SURFACE0);
}

/* ── Paint ───────────────────────────────────────────────────────────────── */
static void sysmon_paint(window_t *win) {
    sysmon_app_t *m = (sysmon_app_t *)win->userdata;
    if (!m) return;

    int wx = win->x, wy = win->y + WM_TITLEBAR_H;
    int ww = win->w;

    /* Update stats every second */
    uint64_t now = timer_get_ticks();
    if (now - m->last_update >= 1000) {
        m->last_update = now;

        uint64_t heap_free  = heap_free_space();
        uint64_t heap_total = 8ULL * 1024 * 1024;  /* 8 MB heap */
        uint64_t heap_used  = (heap_total > heap_free) ? heap_total - heap_free : 0;
        int heap_pct = (int)(heap_used * 100 / (heap_total ? heap_total : 1));
        m->heap_hist[m->hist_pos] = (uint8_t)heap_pct;

        uint64_t pmm_free  = pmm_get_free_memory();
        uint64_t pmm_total = pmm_get_total_memory();
        uint64_t pmm_used  = (pmm_total > pmm_free) ? pmm_total - pmm_free : 0;
        int pmm_pct = (int)(pmm_used * 100 / (pmm_total ? pmm_total : 1));
        m->pmm_hist[m->hist_pos] = (uint8_t)pmm_pct;

        m->hist_pos = (m->hist_pos + 1) % SYSMON_HISTORY;
        wm_invalidate(win);
    } else {
        wm_invalidate(win);
    }

    fb_fill_rect(wx, wy, ww, win->h - WM_TITLEBAR_H, COL_BASE);

    /* ── Header ── */
    fb_fill_rect(wx, wy, ww, 50, COL_SURFACE0);
    fb_fill_rect_blend(wx, wy, ww, 1, 0xFFFFFF, 14);
    fb_fill_rect(wx, wy, 4, 50, COL_MAUVE);
    font_puts2x(wx+14, wy+8, "SysMon", COL_MAUVE, COL_SURFACE0);

    /* Uptime */
    char utbuf[32];
    fmt_uptime(timer_get_uptime_seconds(), utbuf);
    font_puts(wx+14+6*16+10, wy+15, "Uptime:", COL_SUBTEXT, COL_SURFACE0);
    font_puts(wx+14+6*16+70, wy+15, utbuf, COL_TEXT, COL_SURFACE0);

    /* Date/time */
    rtc_time_t t; rtc_get_time(&t);
    char tbuf[32]; rtc_time_to_string(tbuf, &t);
    int tl=mlen(tbuf);
    font_puts(wx+ww-tl*8-12, wy+15, tbuf, COL_OVERLAY0, COL_SURFACE0);
    fb_fill_rect(wx, wy+50, ww, 1, COL_SURFACE1);

    int y = wy + 60;

    /* ── Kernel Heap ── */
    section_hdr(wx, y, ww, "KERNEL HEAP");
    y += 28;
    {
        uint64_t heap_free  = heap_free_space();
        uint64_t heap_total = 8ULL * 1024 * 1024;
        uint64_t heap_used  = (heap_total > heap_free) ? heap_total - heap_free : 0;
        int pct = (int)(heap_used * 100 / (heap_total ? heap_total : 1));

        usage_bar(wx+14, y, ww-70, 18, pct, COL_BLUE, NULL);
        y += 24;

        char ub[24], fb[24], tb[24];
        fmt_mem(heap_used,  ub);
        fmt_mem(heap_free,  fb);
        fmt_mem(heap_total, tb);

        font_puts(wx+14, y, "Used:", COL_OVERLAY0, COL_BASE);
        font_puts(wx+50, y, ub, COL_TEXT, COL_BASE);
        font_puts(wx+14+140, y, "Free:", COL_OVERLAY0, COL_BASE);
        font_puts(wx+14+180, y, fb, COL_GREEN, COL_BASE);
        y += 18;

        /* Sparkline history */
        draw_sparkline(wx+14, y, ww-28, 30, m->heap_hist, SYSMON_HISTORY, m->hist_pos, COL_BLUE, COL_SURFACE0);
        y += 38;
    }

    /* ── Physical Memory ── */
    section_hdr(wx, y, ww, "PHYSICAL MEMORY");
    y += 28;
    {
        uint64_t pmm_free  = pmm_get_free_memory();
        uint64_t pmm_total = pmm_get_total_memory();
        uint64_t pmm_used  = (pmm_total > pmm_free) ? pmm_total - pmm_free : 0;
        int pct = (int)(pmm_used * 100 / (pmm_total ? pmm_total : 1));

        usage_bar(wx+14, y, ww-70, 18, pct, COL_MAUVE, NULL);
        y += 24;

        char ub[24], fb2[24], tb[24];
        fmt_mem(pmm_used,  ub);
        fmt_mem(pmm_free,  fb2);
        fmt_mem(pmm_total, tb);

        font_puts(wx+14, y, "Used:", COL_OVERLAY0, COL_BASE);
        font_puts(wx+50, y, ub, COL_TEXT, COL_BASE);
        font_puts(wx+14+140, y, "Free:", COL_OVERLAY0, COL_BASE);
        font_puts(wx+14+180, y, fb2, COL_GREEN, COL_BASE);
        font_puts(wx+14+320, y, "Total:", COL_OVERLAY0, COL_BASE);
        font_puts(wx+14+365, y, tb, COL_SUBTEXT, COL_BASE);
        y += 18;

        draw_sparkline(wx+14, y, ww-28, 30, m->pmm_hist, SYSMON_HISTORY, m->hist_pos, COL_MAUVE, COL_SURFACE0);
        y += 38;
    }

    /* ── Process list ── */
    section_hdr(wx, y, ww, "PROCESSES");
    y += 26;

    /* Column headers */
    fb_fill_rect(wx+14, y, ww-28, 18, COL_SURFACE0);
    font_puts(wx+18, y+1, "PID", COL_OVERLAY0, COL_SURFACE0);
    font_puts(wx+58, y+1, "STATE", COL_OVERLAY0, COL_SURFACE0);
    font_puts(wx+118, y+1, "PRIO", COL_OVERLAY0, COL_SURFACE0);
    font_puts(wx+166, y+1, "NAME", COL_OVERLAY0, COL_SURFACE0);
    y += 20;

    static const char *state_names[] = { "RUN", "RDY", "BLK", "ZMB", "DED" };
    uint32_t state_cols[5];
    state_cols[0]=COL_GREEN; state_cols[1]=COL_TEAL; state_cols[2]=COL_YELLOW;
    state_cols[3]=COL_PEACH; state_cols[4]=COL_RED;

    int shown = 0;
    int max_rows = (win->h - WM_TITLEBAR_H - (y - (win->y + WM_TITLEBAR_H))) / 16 - 1;
    if (max_rows < 0) max_rows = 0;

    for (int i = 0; i < MAX_PROCESSES && shown < max_rows; i++) {
        if (!processes[i]) continue;

        process_t *proc = processes[i];
        int row_y = y + shown * 16;
        uint32_t row_bg = (shown & 1) ? COL_BASE : COL_MANTLE;
        fb_fill_rect(wx+14, row_y, ww-28, 15, row_bg);

        /* PID */
        char pid_s[8]; u64s(proc->pid, pid_s);
        font_puts(wx+18, row_y, pid_s, COL_SUBTEXT, row_bg);

        /* State */
        int si = (int)proc->state; if (si>4) si=4;
        font_puts(wx+58, row_y, state_names[si], state_cols[si], row_bg);

        /* Priority */
        char pr[4]; pr[0]='0'+proc->priority%10; pr[1]=0;
        font_puts(wx+118, row_y, pr, COL_OVERLAY0, row_bg);

        /* Name */
        font_puts(wx+166, row_y, proc->name, COL_TEXT, row_bg);

        shown++;
    }

    /* Process count */
    char pc[32]; int pci=0;
    const char *pcl="Total processes: "; while(*pcl) pc[pci++]=*pcl++;
    char pcn[8]; u64s((uint64_t)process_count, pcn);
    const char *pp=pcn; while(*pp) pc[pci++]=*pp++;
    pc[pci]=0;
    font_puts(wx+14, y + shown*16 + 4, pc, COL_OVERLAY0, COL_BASE);

    (void)mcpy;
}

static void sysmon_close(window_t *win) {
    sysmon_app_t *m = (sysmon_app_t *)win->userdata;
    if (m) { kfree(m); win->userdata = NULL; }
    wm_close(win);
}

/* ── Constructor ─────────────────────────────────────────────────────────── */
sysmon_app_t *sysmon_create(int x, int y) {
    window_t *win = wm_new(x, y, 580, 460, "System Monitor");
    if (!win) return NULL;

    sysmon_app_t *m = (sysmon_app_t *)kmalloc(sizeof(sysmon_app_t));
    if (!m) { wm_close(win); return NULL; }
    mzero(m, sizeof(sysmon_app_t));

    m->win         = win;
    m->last_update = 0;

    win->on_paint = sysmon_paint;
    win->on_close = sysmon_close;
    win->userdata = m;
    wm_invalidate(win);
    return m;
}
