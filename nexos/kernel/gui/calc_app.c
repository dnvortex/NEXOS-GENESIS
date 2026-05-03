/* NexOS — kernel/gui/calc_app.c | GUI Calculator | MIT License */
#include "calc_app.h"
#include "wm.h"
#include "../drivers/fb.h"
#include "../drivers/font.h"
#include "../mm/heap.h"
#include <stdint.h>
#include <stddef.h>

void *kmalloc(size_t sz);
void  kfree(void *p);

/* ── Integer ↔ string ────────────────────────────────────────────────────── */
static int64_t str_to_i64(const char *s) {
    int64_t v=0; int neg=0;
    if (*s=='-'){neg=1;s++;}
    while(*s>='0'&&*s<='9'){v=v*10+(*s-'0');s++;}
    return neg?-v:v;
}
static void i64_to_str(int64_t v, char *buf) {
    if(v==0){buf[0]='0';buf[1]=0;return;}
    char t[24]; int ti=0; int neg=(v<0);
    if(neg) v=-v;
    while(v){t[ti++]='0'+(int)(v%10);v/=10;}
    int bi=0;
    if(neg) buf[bi++]='-';
    while(ti>0) buf[bi++]=t[--ti];
    buf[bi]=0;
}
static int clen(const char *s){int n=0;while(s[n])n++;return n;}
static void ccpy(char *d,const char *s,int max){int i=0;while(i<max-1&&s[i]){d[i]=s[i];i++;}d[i]=0;}

/* ── Button layout ───────────────────────────────────────────────────────── */
#define BTN_COLS   4
#define BTN_ROWS   5
#define BTN_W      58
#define BTN_H      46
#define BTN_GAP     3
#define DISP_H     70
#define GRID_OX     4
#define GRID_OY    (DISP_H + 8)

typedef struct { const char *label; char type; int val; } btn_def_t;

static const btn_def_t btns[BTN_ROWS][BTN_COLS] = {
    { {"C",'C',0}, {"BS",'B',0}, {"%",'%',0}, {"/",'O','/'} },
    { {"7",'D',7}, {"8",'D',8}, {"9",'D',9}, {"*",'O','*'} },
    { {"4",'D',4}, {"5",'D',5}, {"6",'D',6}, {"-",'O','-'} },
    { {"1",'D',1}, {"2",'D',2}, {"3",'D',3}, {"+",'O','+'} },
    { {"+/-",'N',0},{"0",'D',0},{"=",'=',0}, {"",'_',0}    },
};

/* ── Compute result ──────────────────────────────────────────────────────── */
static void calc_compute(calc_app_t *c) {
    int64_t cur = str_to_i64(c->display);
    int64_t result = 0;
    c->error = 0;
    switch (c->pending_op) {
    case '+': result = c->operand + cur; break;
    case '-': result = c->operand - cur; break;
    case '*': result = c->operand * cur; break;
    case '/':
        if (cur == 0) { c->error = 1; ccpy(c->display,"Error",24); return; }
        result = c->operand / cur;
        break;
    default:  result = cur; break;
    }
    i64_to_str(result, c->display);
    c->has_operand  = 0;
    c->pending_op   = 0;
    c->new_number   = 1;
}

/* ── Handle button press ─────────────────────────────────────────────────── */
static void calc_press(calc_app_t *c, char type, int val) {
    if (c->error && type != 'C') return;

    switch (type) {
    case 'C':  /* All clear */
        ccpy(c->display, "0", 24);
        c->operand     = 0;
        c->pending_op  = 0;
        c->has_operand = 0;
        c->new_number  = 1;
        c->error       = 0;
        break;

    case 'B':  /* Backspace */
        if (!c->new_number) {
            int l = clen(c->display);
            if (l > 1) { c->display[l-1]=0; }
            else        { ccpy(c->display,"0",24); c->new_number=1; }
        }
        break;

    case 'D':  /* Digit */
        if (c->new_number) {
            i64_to_str((int64_t)val, c->display);
            c->new_number = 0;
        } else {
            if (clen(c->display) < 18) {
                int l = clen(c->display);
                /* Don't prepend zeros */
                if (!(l==1 && c->display[0]=='0')) {
                    c->display[l]   = (char)('0' + val);
                    c->display[l+1] = 0;
                } else {
                    c->display[0]=(char)('0'+val); c->display[1]=0;
                }
            }
        }
        break;

    case 'O':  /* Operator */
        if (c->has_operand && !c->new_number)
            calc_compute(c);
        c->operand    = str_to_i64(c->display);
        c->pending_op = (char)val;
        c->has_operand = 1;
        c->new_number  = 1;
        break;

    case '=':
        if (c->has_operand) calc_compute(c);
        break;

    case 'N':  /* Negate */
        { int64_t v = str_to_i64(c->display);
          i64_to_str(-v, c->display);
          c->new_number = 0; }
        break;

    case '%':  /* Percent — divide by 100 (integer) */
        { int64_t v = str_to_i64(c->display) / 100;
          i64_to_str(v, c->display);
          c->new_number = 1; }
        break;
    }
}

/* ── Draw ────────────────────────────────────────────────────────────────── */
static uint32_t btn_bg(char type, int hov) {
    uint32_t base;
    switch (type) {
    case 'C': base = 0x4A1010; break;
    case 'B': base = 0x1A2540; break;
    case 'O': base = 0x1A3050; break;
    case '=': base = 0x0A3060; break;
    default:  base = COL_SURFACE0; break;
    }
    if (hov) {
        /* lighten */
        uint32_t r=((base>>16)&0xFF)+40u, g=((base>>8)&0xFF)+40u, b=(base&0xFF)+40u;
        if (r>255) r=255;
        if (g>255) g=255;
        if (b>255) b=255;
        return (r<<16)|(g<<8)|b;
    }
    return base;
}
static uint32_t btn_fg(char type) {
    switch (type) {
    case 'C': return COL_RED;
    case 'B': return COL_LAVENDER;
    case 'O': return COL_SKY;
    case '=': return COL_BLUE;
    default:  return COL_TEXT;
    }
}

static void calc_paint(window_t *win) {
    calc_app_t *c = (calc_app_t *)win->userdata;
    if (!c) return;

    int wx = win->x, wy = win->y + WM_TITLEBAR_H;
    int ww = win->w;

    fb_fill_rect(wx, wy, ww, win->h - WM_TITLEBAR_H, COL_MANTLE);

    /* ── Display panel ── */
    fb_fill_rect(wx, wy, ww, DISP_H, COL_CRUST);
    fb_fill_rect_blend(wx, wy, ww, 1, 0xFFFFFF, 14);
    fb_fill_rect(wx, wy + DISP_H - 1, ww, 1, COL_SURFACE1);

    /* Pending operator indicator */
    if (c->has_operand && c->pending_op) {
        char op_s[3] = { c->pending_op, 0, 0 };
        fb_fill_rounded_rect(wx+8, wy+10, 22, 22, 5, COL_SURFACE1);
        font_puts(wx+14, wy+14, op_s, COL_SKY, COL_SURFACE1);
    }

    /* Number display — right-aligned */
    uint32_t disp_col = c->error ? COL_RED : COL_TEXT;
    int dlen = clen(c->display);
    int big = (dlen <= 9);   /* use 2x font if short enough */
    if (big) {
        int dx = wx + ww - dlen*16 - 14;
        font_puts2x(dx, wy + DISP_H/2 - 16, c->display, disp_col, COL_CRUST);
    } else {
        int dx = wx + ww - dlen*8 - 10;
        font_puts(dx, wy + DISP_H/2 - 8, c->display, disp_col, COL_CRUST);
    }

    /* ── Button grid ── */
    for (int row = 0; row < BTN_ROWS; row++) {
        for (int col = 0; col < BTN_COLS; col++) {
            const btn_def_t *b = &btns[row][col];
            if (b->type == '_') continue;  /* empty cell */

            int bx = wx + GRID_OX + col * (BTN_W + BTN_GAP);
            int by = wy + GRID_OY + row * (BTN_H + BTN_GAP);

            uint32_t bg = btn_bg(b->type, 0);
            uint32_t fg = btn_fg(b->type);

            fb_fill_rounded_rect(bx, by, BTN_W, BTN_H, 8, bg);
            /* Specular top line */
            fb_fill_rect_blend(bx+4, by, BTN_W-8, 1, 0xFFFFFF, 20);
            /* Label */
            int lw = clen(b->label) * 8;
            int lx = bx + (BTN_W - lw) / 2;
            int ly = by + (BTN_H - 16) / 2;
            font_puts(lx, ly, b->label, fg, bg);
        }
    }
}

static void calc_click(window_t *win, int cx, int cy, int btn) {
    (void)btn;
    calc_app_t *c = (calc_app_t *)win->userdata;
    if (!c) return;

    /* Convert client coords to grid coords */
    int gx = cx - GRID_OX;
    int gy = cy - GRID_OY;
    if (gx < 0 || gy < 0) return;

    int col = gx / (BTN_W + BTN_GAP);
    int row = gy / (BTN_H + BTN_GAP);
    if (col >= BTN_COLS || row >= BTN_ROWS) return;

    /* Check we're within the button itself, not the gap */
    int lx = gx - col * (BTN_W + BTN_GAP);
    int ly = gy - row * (BTN_H + BTN_GAP);
    if (lx >= BTN_W || ly >= BTN_H) return;

    const btn_def_t *b = &btns[row][col];
    if (b->type == '_') return;

    calc_press(c, b->type, b->val);
    wm_invalidate(win);
}

static void calc_key(window_t *win, char key) {
    calc_app_t *c = (calc_app_t *)win->userdata;
    if (!c) return;

    if (key>='0' && key<='9')       calc_press(c,'D', key-'0');
    else if (key=='+')              calc_press(c,'O', '+');
    else if (key=='-')              calc_press(c,'O', '-');
    else if (key=='*')              calc_press(c,'O', '*');
    else if (key=='/')              calc_press(c,'O', '/');
    else if (key=='\n'||key=='=')   calc_press(c,'=', 0);
    else if (key=='\b'||key==127)   calc_press(c,'B', 0);
    else if (key=='c'||key=='C')    calc_press(c,'C', 0);
    else return;

    wm_invalidate(win);
}

static void calc_close(window_t *win) {
    calc_app_t *c = (calc_app_t *)win->userdata;
    if (c) { kfree(c); win->userdata = NULL; }
    wm_close(win);
}

/* ── Constructor ─────────────────────────────────────────────────────────── */
calc_app_t *calc_create(int x, int y) {
    int win_w = GRID_OX*2 + BTN_COLS*(BTN_W+BTN_GAP) - BTN_GAP;
    int win_h = WM_TITLEBAR_H + GRID_OY + BTN_ROWS*(BTN_H+BTN_GAP) - BTN_GAP + 8;

    window_t *win = wm_new(x, y, win_w, win_h - WM_TITLEBAR_H + WM_TITLEBAR_H,
                           "Calculator");
    if (!win) return NULL;

    calc_app_t *c = (calc_app_t *)kmalloc(sizeof(calc_app_t));
    if (!c) { wm_close(win); return NULL; }
    for (int i=0; i<(int)sizeof(calc_app_t); i++) ((uint8_t*)c)[i]=0;

    c->win = win;
    ccpy(c->display, "0", 24);
    c->new_number = 1;

    win->on_paint = calc_paint;
    win->on_click = calc_click;
    win->on_key   = calc_key;
    win->on_close = calc_close;
    win->userdata = c;
    return c;
}
