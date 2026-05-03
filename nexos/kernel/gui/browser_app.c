/* NexOS — kernel/gui/browser_app.c | GUI Web Browser | MIT License */
#include "browser_app.h"
#include "wm.h"
#include "../drivers/fb.h"
#include "../drivers/font.h"
#include "../net/http.h"
#include "../mm/heap.h"
#include <stdint.h>
#include <stddef.h>

void *kmalloc(size_t sz);
void  kfree(void *p);

/* ── String helpers ──────────────────────────────────────────────────────── */
static int blen(const char *s) { int n=0; while(s[n]) n++; return n; }
static void bcpy(char *d, const char *s, int max) {
    int i=0; while(i<max-1&&s[i]){d[i]=s[i];i++;} d[i]=0;
}
static int beq(const char *a, const char *b) {
    while(*a&&*b&&*a==*b){a++;b++;} return *a==0&&*b==0;
}

/* ── Simple HTML stripper ────────────────────────────────────────────────── */
static int html_strip(const uint8_t *src, int slen, char *dst, int dmax) {
    int di=0, in_tag=0, skip_block=0;
    char block_end[12];

    for (int i=0; i<slen && di<dmax-2; i++) {
        uint8_t c = src[i];

        /* Skipping <script> / <style> body */
        if (skip_block) {
            int bel = blen(block_end);
            int match = 1;
            for (int k=0; k<bel && i+k<slen; k++)
                if ((src[i+k]|32) != (uint8_t)block_end[k]) { match=0; break; }
            if (match) { i += bel-1; skip_block=0; }
            continue;
        }

        if (in_tag) {
            if (c=='>') in_tag=0;
            continue;
        }

        if (c=='<') {
            /* Peek tag name */
            int j=i+1;
            while (j<slen && src[j]==' ') j++;
            int closing = (j<slen && src[j]=='/');
            if (closing) j++;
            /* tag name up to 8 chars */
            char tn[9]; int ti=0;
            while (j<slen && src[j]!='>'&&src[j]!=' '&&src[j]!='/'&&ti<8)
                tn[ti++]=(char)(src[j++]|32);
            tn[ti]=0;

            /* skip script/style content entirely */
            if (beq(tn,"script"))  { bcpy(block_end,"</script>",12); skip_block=1; in_tag=1; continue; }
            if (beq(tn,"style"))   { bcpy(block_end,"</style>",12);  skip_block=1; in_tag=1; continue; }

            /* block-level elements → newline */
            int is_block = beq(tn,"p")||beq(tn,"div")||beq(tn,"br")||
                           beq(tn,"h1")||beq(tn,"h2")||beq(tn,"h3")||
                           beq(tn,"h4")||beq(tn,"h5")||beq(tn,"h6")||
                           beq(tn,"tr")||beq(tn,"li")||beq(tn,"ul")||
                           beq(tn,"ol")||beq(tn,"hr")||beq(tn,"article")||
                           beq(tn,"section")||beq(tn,"header")||beq(tn,"footer");
            if (is_block && di>0 && dst[di-1]!='\n') dst[di++]='\n';
            if (beq(tn,"li") && !closing)
                { dst[di++]=' '; if(di<dmax-2){dst[di++]='-';dst[di++]=' ';} }

            in_tag=1; continue;
        }

        /* HTML entities */
        if (c=='&') {
            /* &amp; */
            if (i+4<slen&&src[i+1]=='a'&&src[i+2]=='m'&&src[i+3]=='p'&&src[i+4]==';')
                { dst[di++]='&'; i+=4; continue; }
            /* &lt; */
            if (i+3<slen&&src[i+1]=='l'&&src[i+2]=='t'&&src[i+3]==';')
                { dst[di++]='<'; i+=3; continue; }
            /* &gt; */
            if (i+3<slen&&src[i+1]=='g'&&src[i+2]=='t'&&src[i+3]==';')
                { dst[di++]='>'; i+=3; continue; }
            /* &nbsp; */
            if (i+5<slen&&src[i+1]=='n'&&src[i+2]=='b'&&src[i+3]=='s'&&
                          src[i+4]=='p'&&src[i+5]==';')
                { dst[di++]=' '; i+=5; continue; }
            /* &#NNN; */
            if (i+2<slen&&src[i+1]=='#') {
                int v=0,j=i+2;
                while(j<slen&&src[j]>=48&&src[j]<=57){v=v*10+(src[j]-48);j++;}
                if(j<slen&&src[j]==';'){
                    dst[di++]=(v>=32&&v<127)?(char)v:' '; i=j; continue;
                }
            }
            /* skip unknown entity */
            int j=i+1; while(j<slen&&j<i+10&&src[j]!=';') j++;
            if(j<slen&&src[j]==';') i=j;
            continue;
        }

        /* Collapse whitespace (keep single \n) */
        if (c=='\r') continue;
        if (c=='\t') c=' ';
        if (c=='\n') {
            if (di>0 && dst[di-1]!='\n') dst[di++]='\n';
            continue;
        }
        if (c==' ') {
            if (di>0 && dst[di-1]!=' ' && dst[di-1]!='\n') dst[di++]=' ';
            continue;
        }
        if (c<32) continue;
        dst[di++]=(char)c;
    }
    /* Trim trailing whitespace */
    while (di>0 && (dst[di-1]=='\n'||dst[di-1]==' ')) di--;
    dst[di]=0;
    return di;
}

/* ── About pages ─────────────────────────────────────────────────────────── */
static void load_about(browser_app_t *b, const char *page) {
    if (beq(page,"about:blank")||beq(page,"about:")) {
        b->text[0]=0; b->text_len=0;
        b->state=BSTATE_DONE;
        bcpy(b->status,"about:blank",80);
        return;
    }
    if (beq(page,"about:nexos")) {
        const char *info =
            "NexOS Browser 1.0\n\n"
            "A minimal HTTP/1.0 browser for NexOS.\n\n"
            "- Supports http:// URLs\n"
            "- Renders plain text + stripped HTML\n"
            "- Scroll: click upper/lower content area\n"
            "- Navigate: type URL + Enter\n"
            "- Backspace: edit URL\n\n"
            "Keyboard shortcuts:\n"
            "  Enter     — navigate to URL\n"
            "  Backspace — delete URL character\n"
            "  Escape    — clear URL\n\n"
            "Built-in pages:\n"
            "  about:nexos  — this page\n"
            "  about:blank  — blank page\n";
        bcpy(b->text, info, BROWSER_BUF_MAX);
        b->text_len = blen(b->text);
        b->state = BSTATE_DONE;
        bcpy(b->status, "about:nexos", 80);
        return;
    }
    bcpy(b->status, "Unknown about: page", 80);
    b->state = BSTATE_ERROR;
}

/* ── Fetch URL ───────────────────────────────────────────────────────────── */
static void browser_fetch(browser_app_t *b) {
    b->text_len = 0; b->text[0] = 0;
    b->scroll = 0; b->line_count = 0;
    b->state = BSTATE_LOADING;
    wm_invalidate(b->win);

    /* about: pages */
    if (b->url[0]=='a' && b->url[1]=='b') {
        load_about(b, b->url);
        return;
    }

    /* must start with http:// */
    if (!(b->url[0]=='h'&&b->url[1]=='t'&&b->url[2]=='t'&&b->url[3]=='p')) {
        bcpy(b->status, "Error: URL must start with http://", 80);
        b->state = BSTATE_ERROR;
        return;
    }

    http_response_t *resp = http_get(b->url);
    if (!resp) {
        bcpy(b->status, "Error: connection failed (no network?)", 80);
        b->state = BSTATE_ERROR;
        return;
    }

    if (resp->status_code < 100) {
        bcpy(b->status, "Error: no response", 80);
        b->state = BSTATE_ERROR;
        http_free(resp);
        return;
    }

    if (resp->status_code >= 400) {
        /* Show error code */
        char msg[80];
        msg[0]='H'; msg[1]='T'; msg[2]='T'; msg[3]='P'; msg[4]=' ';
        char code[8]; int ci=0;
        int sc=resp->status_code;
        char t[8]; int ti=0;
        if (!sc) { t[ti++]='0'; }
        else     { while (sc) { t[ti++]='0'+sc%10; sc/=10; } }
        while (ti > 0) code[ci++] = t[--ti];
        code[ci] = 0;
        int mi=5; int ki=0; while(code[ki]) msg[mi++]=code[ki++];
        msg[mi]=0;
        bcpy(b->status, msg, 80);
        b->state = BSTATE_ERROR;
        http_free(resp);
        return;
    }

    /* Strip HTML and store text */
    b->text_len = html_strip(resp->body, (int)resp->body_len,
                              b->text, BROWSER_BUF_MAX);
    b->state = BSTATE_DONE;
    /* Status: "200 OK — hostname" */
    bcpy(b->status, "200 OK", 80);
    http_free(resp);
}

/* ── Draw content area ───────────────────────────────────────────────────── */
#define TOOLBAR_H  38
#define STATUSBAR_H 20

static void browser_draw_content(browser_app_t *b, int bx, int by,
                                  int bw, int bh) {
    fb_fill_rect(bx, by, bw, bh, COL_BASE);

    if (b->state == BSTATE_IDLE) {
        int cx = bx + bw/2;
        int cy = by + bh/2;
        fb_fill_circle(cx, cy - 30, 28, COL_SURFACE0);
        fb_draw_circle(cx, cy - 30, 28, COL_BLUE);
        fb_draw_circle(cx, cy - 30, 18, COL_BLUE);
        fb_draw_line(cx - 28, cy - 30, cx + 28, cy - 30, COL_BLUE);
        fb_draw_line(cx, cy - 58, cx, cy - 2, COL_BLUE);
        font_puts(bx + bw/2 - 56, cy + 8,
                  "Enter a URL above", COL_SUBTEXT, COL_BASE);
        font_puts(bx + bw/2 - 72, cy + 26,
                  "try: about:nexos", COL_OVERLAY0, COL_BASE);
        return;
    }
    if (b->state == BSTATE_LOADING) {
        font_puts2x(bx + bw/2 - 48, by + bh/2 - 16,
                    "Loading", COL_BLUE, COL_BASE);
        font_puts(bx + bw/2 - 64, by + bh/2 + 20,
                  b->url, COL_SUBTEXT, COL_BASE);
        return;
    }
    if (b->state == BSTATE_ERROR) {
        fb_fill_rounded_rect(bx+16, by+16, bw-32, 56, 8, COL_SURFACE0);
        fb_fill_rect(bx+16, by+16, 4, 56, COL_RED);
        font_puts(bx+28, by+24, "Navigation Error", COL_RED, COL_SURFACE0);
        font_puts(bx+28, by+42, b->status, COL_SUBTEXT, COL_SURFACE0);
        return;
    }

    /* BSTATE_DONE — render text */
    int max_col   = (bw - 18) / 8;
    int vis_rows  = bh / 16;
    int line      = 0;
    char lbuf[256];
    int  li       = 0;

    for (int i = 0; i <= b->text_len; i++) {
        char c = (i < b->text_len) ? b->text[i] : '\n';
        int wrap = (li >= max_col);
        int newl = (c == '\n');

        if (newl || wrap) {
            lbuf[li] = 0;
            if (line >= b->scroll && line < b->scroll + vis_rows) {
                int row = line - b->scroll;
                /* Heuristic: lines starting with dash are list items */
                uint32_t fc = COL_TEXT;
                if (li>1 && lbuf[0]=='-' && lbuf[1]==' ') fc = COL_SUBTEXT;
                font_puts(bx + 10, by + row * 16, lbuf, fc, COL_BASE);
            }
            li = 0; line++;
            if (wrap && !newl && c >= 32 && li < 254) lbuf[li++] = c;
            /* Skip blank lines beyond visible area */
            if (line > b->scroll + vis_rows + 4) break;
        } else if (c >= 32 && c < 127 && li < 254) {
            lbuf[li++] = c;
        }
    }
    b->line_count = line;
}

/* ── Paint ───────────────────────────────────────────────────────────────── */
static void browser_paint(window_t *win) {
    browser_app_t *b = (browser_app_t *)win->userdata;
    if (!b) return;

    int wx = win->x, wy = win->y + WM_TITLEBAR_H;
    int ww = win->w;
    int client_h = win->h - WM_TITLEBAR_H;

    /* ── Toolbar ── */
    fb_fill_rect(wx, wy, ww, TOOLBAR_H, COL_SURFACE0);
    fb_fill_rect_blend(wx, wy, ww, 1, 0xFFFFFF, 14);

    /* Back button */
    fb_fill_rounded_rect(wx + 6, wy + 5, 28, 28, 6, COL_SURFACE1);
    font_puts(wx + 13, wy + 11, "<", COL_SUBTEXT, COL_SURFACE1);

    /* Reload button */
    fb_fill_rounded_rect(wx + 38, wy + 5, 28, 28, 6, COL_SURFACE1);
    font_puts(wx + 43, wy + 11, "R", COL_SUBTEXT, COL_SURFACE1);

    /* URL bar */
    int ub_x = wx + 70, ub_w = ww - 78;
    uint32_t ub_bg = (b->state==BSTATE_ERROR) ? 0x2A1520 : COL_BASE;
    fb_fill_rounded_rect(ub_x, wy + 5, ub_w, 28, 6, ub_bg);
    fb_draw_rect_outline(ub_x, wy + 5, ub_w, 28,
                         (b->state==BSTATE_ERROR) ? COL_RED : COL_SURFACE2, 1);

    /* URL or placeholder */
    uint32_t url_col = (b->url_len > 0) ? COL_TEXT : COL_OVERLAY0;
    const char *url_disp = (b->url_len > 0) ? b->url : "http://";
    /* Clamp display to fit bar */
    int max_url_chars = (ub_w - 16) / 8;
    int start = 0;
    if (b->url_len > max_url_chars) start = b->url_len - max_url_chars;
    font_puts(ub_x + 10, wy + 11, url_disp + start, url_col, ub_bg);

    /* Cursor blink in URL bar */
    if ((b->url_len < max_url_chars)) {
        int cur_x = ub_x + 10 + (b->url_len - start) * 8;
        fb_fill_rect(cur_x, wy + 9, 1, 18, COL_BLUE);
    }

    /* Toolbar bottom divider */
    fb_fill_rect(wx, wy + TOOLBAR_H - 1, ww, 1, COL_SURFACE1);

    /* ── Status bar ── */
    int sb_y = wy + client_h - STATUSBAR_H;
    fb_fill_rect(wx, sb_y, ww, STATUSBAR_H, COL_SURFACE0);
    fb_fill_rect(wx, sb_y, ww, 1, COL_SURFACE1);

    /* State indicator pill */
    uint32_t pill_col = (b->state==BSTATE_DONE)    ? COL_GREEN
                      : (b->state==BSTATE_ERROR)   ? COL_RED
                      : (b->state==BSTATE_LOADING) ? COL_YELLOW
                                                    : COL_OVERLAY0;
    fb_fill_rounded_rect(wx+6, sb_y+3, 8, 14, 4, pill_col);

    font_puts(wx+18, sb_y+4, b->status, COL_SUBTEXT, COL_SURFACE0);

    /* Scroll indicator */
    if (b->line_count > 0) {
        char sc[16]; int si=0;
        int v=b->scroll+1; char t[8]; int ti=0;
        if(!v){t[ti++]='0';}else while(v){t[ti++]='0'+v%10;v/=10;}
        while(ti>0) sc[si++]=t[--ti];
        sc[si++]='/'; ti=0; v=b->line_count;
        if(!v){t[ti++]='0';}else while(v){t[ti++]='0'+v%10;v/=10;}
        while(ti>0) sc[si++]=t[--ti];
        sc[si]=0;
        font_puts(wx+ww-si*8-10, sb_y+4, sc, COL_OVERLAY0, COL_SURFACE0);
    }

    /* ── Content area ── */
    int cy = wy + TOOLBAR_H;
    int ch = sb_y - cy;
    browser_draw_content(b, wx, cy, ww, ch);
}

/* ── Interaction ─────────────────────────────────────────────────────────── */
static void browser_key(window_t *win, char key) {
    browser_app_t *b = (browser_app_t *)win->userdata;
    if (!b) return;

    if (key == '\n' || key == '\r') {
        if (b->url_len > 0) browser_fetch(b);
    } else if (key == 27) { /* Escape — clear URL */
        b->url_len = 0; b->url[0] = 0;
    } else if (key == '\b' || key == 127) {
        if (b->url_len > 0) b->url[--b->url_len] = 0;
    } else if (key >= 32 && key < 127) {
        if (b->url_len < BROWSER_URL_MAX - 1) {
            b->url[b->url_len++] = key;
            b->url[b->url_len]   = 0;
        }
    }
    wm_invalidate(win);
}

static void browser_click(window_t *win, int cx, int cy, int btn) {
    (void)btn;
    browser_app_t *b = (browser_app_t *)win->userdata;
    if (!b) return;

    /* Back button */
    if (cy >= 5 && cy < 33 && cx >= 6 && cx < 34) {
        /* Navigate to about:blank for now */
        bcpy(b->url, "about:blank", BROWSER_URL_MAX);
        b->url_len = blen(b->url);
        browser_fetch(b);
        return;
    }
    /* Reload button */
    if (cy >= 5 && cy < 33 && cx >= 38 && cx < 66) {
        if (b->url_len > 0) browser_fetch(b);
        return;
    }

    /* Content area scroll — top third scrolls up, bottom third scrolls down */
    if (cy >= TOOLBAR_H) {
        int client_h = win->h - WM_TITLEBAR_H - TOOLBAR_H - STATUSBAR_H;
        int third    = client_h / 3;
        int rel_y    = cy - TOOLBAR_H;
        if (rel_y < third)              { if (b->scroll > 0) b->scroll -= 3; }
        else if (rel_y > 2 * third)     { b->scroll += 3; }
    }
    wm_invalidate(win);
}

static void browser_close(window_t *win) {
    browser_app_t *b = (browser_app_t *)win->userdata;
    if (b) { kfree(b); win->userdata = NULL; }
    wm_close(win);
}

/* ── Constructor ─────────────────────────────────────────────────────────── */
browser_app_t *browser_create(int x, int y) {
    window_t *win = wm_new(x, y, 720, 520, "NexOS Browser");
    if (!win) return NULL;

    browser_app_t *b = (browser_app_t *)kmalloc(sizeof(browser_app_t));
    if (!b) { wm_close(win); return NULL; }
    for (int i = 0; i < (int)sizeof(browser_app_t); i++) ((uint8_t *)b)[i] = 0;

    b->win   = win;
    b->state = BSTATE_IDLE;
    bcpy(b->status, "Ready", 80);

    win->on_paint = browser_paint;
    win->on_key   = browser_key;
    win->on_click = browser_click;
    win->on_close = browser_close;
    win->userdata = b;
    return b;
}
