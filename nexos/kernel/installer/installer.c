/* NexOS — kernel/installer/installer.c
 * archinstall-style TUI installer.
 * Left-panel menu + right-panel detail/edit.
 * Activated when kernel cmdline contains "nexos.install".
 * MIT License */

#include "installer.h"
#include "../drivers/fb.h"
#include "../drivers/font.h"
#include "../drivers/keyboard.h"
#include "../drivers/ata.h"
#include "../drivers/timer.h"
#include "../drivers/serial.h"
#include <stdint.h>
#include <stddef.h>

/* ────────────────────────────────────────────────────────────────────────────
 * Geometry
 * ──────────────────────────────────────────────────────────────────────────── */
#define SCR_W    1024
#define SCR_H     768
#define CW          8   /* font char width   */
#define CH         16   /* font char height  */
#define TITLE_H    20
#define FOOT_H     20
#define SIDE_W    224   /* left panel        */
#define DIV_X     224
#define RP_X      225   /* right panel left  */
#define RP_W      (SCR_W - RP_X)
#define CT_Y      TITLE_H               /* content top      */
#define CT_H      (SCR_H - TITLE_H - FOOT_H) /* content height */
#define ROW_H      18   /* menu item row height */
#define RP_PAD     18   /* right-panel left padding */

/* ────────────────────────────────────────────────────────────────────────────
 * Palette  (terminal dark, archinstall-inspired)
 * ──────────────────────────────────────────────────────────────────────────── */
#define C_BG       0xFF000000
#define C_HDR      0xFF1A4B8C
#define C_HDR_FG   0xFFFFFFFF
#define C_FG       0xFFCCCCCC
#define C_DIM      0xFF777777
#define C_SEL_BG   0xFF1A4B8C
#define C_SEL_FG   0xFFFFFFFF
#define C_VAL      0xFF55DDFF
#define C_GRN      0xFF44CC44
#define C_RED      0xFFFF5555
#define C_YEL      0xFFFFCC44
#define C_DIV      0xFF282828
#define C_INP_BG   0xFF111122
#define C_CURSOR   0xFF4488FF

/* ────────────────────────────────────────────────────────────────────────────
 * Item types
 * ──────────────────────────────────────────────────────────────────────────── */
#define IT_NORMAL   0   /* opens sub-screen in right panel */
#define IT_TOGGLE   1   /* toggles directly                */
#define IT_ACTION   2   /* runs an action                  */
#define IT_SEP      3   /* blank separator                 */

/* ────────────────────────────────────────────────────────────────────────────
 * Menu item definitions  (19 items matching the archinstall layout)
 * ──────────────────────────────────────────────────────────────────────────── */
#define NUM_ITEMS  19

#define IDX_LANG    0
#define IDX_LOCALE  1
#define IDX_MIRROR  2
#define IDX_DISK    3
#define IDX_SWAP    4
#define IDX_BOOT    5
#define IDX_KERN    6
#define IDX_HOST    7
#define IDX_AUTH    8
#define IDX_PROF    9
#define IDX_APPS   10
#define IDX_NET    11
#define IDX_PKG    12
#define IDX_TZ     13
#define IDX_NTP    14
#define IDX_SEP    15
#define IDX_SAVE   16
#define IDX_INST   17
#define IDX_ABORT  18

static const char *item_labels[NUM_ITEMS] = {
    "NexOS language",
    "Locales",
    "Mirrors and repositories",
    "Disk configuration",
    "Swap",
    "Bootloader",
    "Kernels",
    "Hostname",
    "Authentication",
    "Profile",
    "Applications",
    "Network configuration",
    "Additional packages",
    "Timezone",
    "Automatic time sync (NTP)",
    "",
    "Save configuration",
    "Install",
    "Abort",
};

static const int item_types[NUM_ITEMS] = {
    IT_NORMAL, IT_NORMAL, IT_NORMAL, IT_NORMAL, IT_NORMAL,
    IT_NORMAL, IT_NORMAL, IT_NORMAL, IT_NORMAL, IT_NORMAL,
    IT_NORMAL, IT_NORMAL, IT_NORMAL, IT_NORMAL, IT_TOGGLE,
    IT_SEP,
    IT_ACTION, IT_ACTION, IT_ACTION,
};

static const char *item_descs[NUM_ITEMS] = {
    "Select the installer display language and UI translation.",
    "Set system locale and character encoding for the installed system.",
    "Choose the nearest mirror region for fast package downloads.",
    "Select and configure the installation target disk.\n!! WARNING: all data on the disk will be erased !!",
    "Configure swap space type and size (zram or partition).",
    "Choose the bootloader to install. GRUB is recommended.",
    "Select the NexOS kernel variant to install.",
    "Set the system hostname (the computer name on the network).",
    "Configure the root account password for the installed system.",
    "Select a pre-defined system configuration profile.",
    "Choose which bundled NexOS applications to install.",
    "Select the network configuration manager daemon.",
    "Enter space-separated extra packages to install.",
    "Set your local timezone. Affects how the system clock is displayed.",
    "Automatically synchronise the system clock via NTP on every boot.",
    "",
    "Write current settings to /etc/nexos-install.conf on the live system.",
    "Begin installation using the current configuration.",
    "Exit the installer without making any changes.",
};

/* ── Mutable value buffers ────────────────────────────────────────────────── */
static char val_lang[40]  = "English (100%)";
static char val_loc[40]   = "en_US.UTF-8";
static char val_mir[40]   = "Auto (nearest)";
static char val_disk[56]  = "Not configured";
static char val_swap[40]  = "zram (auto)";
static char val_boot[40]  = "GRUB";
static char val_kern[40]  = "nexos-default";
static char val_host[72]  = "nexos";
static char val_auth[72]  = "No root password";
static char val_prof[40]  = "Desktop";
static char val_apps[56]  = "None";
static char val_net[40]   = "NetworkManager";
static char val_pkg[104]  = "None";
static char val_tz[40]    = "UTC";
static int  val_ntp       = 1;

static char *item_values[NUM_ITEMS] = {
    val_lang, val_loc, val_mir, val_disk, val_swap,
    val_boot, val_kern, val_host, val_auth, val_prof,
    val_apps, val_net, val_pkg, val_tz,
    NULL, NULL, NULL, NULL, NULL,
};

/* ── Option lists for each NORMAL item ───────────────────────────────────── */
static const char *opts_lang[]  = {
    "English (100%)", "Spanish (100%)", "French (100%)",
    "German (100%)", "Portuguese (100%)", "Italian (90%)",
    "Chinese (80%)", "Japanese (80%)", NULL
};
static const char *opts_loc[]   = {
    "en_US.UTF-8", "en_GB.UTF-8", "es_ES.UTF-8",
    "fr_FR.UTF-8", "de_DE.UTF-8", "it_IT.UTF-8",
    "pt_BR.UTF-8", "ja_JP.UTF-8", "zh_CN.UTF-8", NULL
};
static const char *opts_mir[]   = {
    "Auto (nearest)", "Americas", "Europe",
    "Asia-Pacific", "Africa", "Global (all mirrors)", NULL
};
/* item 3 (disk): dynamic, built from scan_drives() — handled specially */
static const char *opts_swap[]  = {
    "None", "zram (auto)", "zram (512MB)",
    "zram (1GB)", "zram (2GB)", "Swap partition", NULL
};
static const char *opts_boot[]  = {
    "GRUB", "NexBoot (legacy)", "None", NULL
};
static const char *opts_kern[]  = {
    "nexos-default", "nexos-lts",
    "nexos-hardened", "nexos-rt", NULL
};
/* item 7 (hostname): text input — handled specially */
static const char *opts_auth[]  = {
    "No root password", "Set root password", NULL
};
static const char *opts_prof[]  = {
    "Minimal", "Desktop", "Server", "Development", NULL
};
static const char *opts_apps[]  = {
    "None", "NSH + Terminal",
    "Text editor + Terminal", "System Monitor",
    "All bundled apps", NULL
};
static const char *opts_net[]   = {
    "NetworkManager", "dhcpcd", "Manual", "None", NULL
};
/* item 12 (packages): text input — handled specially */
static const char *opts_tz[]    = {
    "UTC", "US/Eastern", "US/Central", "US/Mountain", "US/Pacific",
    "Europe/London", "Europe/Paris", "Europe/Berlin",
    "Europe/Moscow", "Asia/Tokyo", "Asia/Shanghai",
    "Asia/Kolkata", "Australia/Sydney", NULL
};

static const char **get_opts(int idx) {
    switch (idx) {
    case IDX_LANG:   return opts_lang;
    case IDX_LOCALE: return opts_loc;
    case IDX_MIRROR: return opts_mir;
    case IDX_SWAP:   return opts_swap;
    case IDX_BOOT:   return opts_boot;
    case IDX_KERN:   return opts_kern;
    case IDX_AUTH:   return opts_auth;
    case IDX_PROF:   return opts_prof;
    case IDX_APPS:   return opts_apps;
    case IDX_NET:    return opts_net;
    case IDX_TZ:     return opts_tz;
    default:         return NULL;
    }
}

/* ── Navigator/editor state ──────────────────────────────────────────────── */
static int menu_sel    = 0;    /* selected item in left panel (0-based)   */
static int edit_mode   = 0;    /* 0=browse 1=list 2=textinput 3=disk      */
static int edit_cur    = 0;    /* cursor in sub-list                      */
static int saved_flag  = 0;    /* set when config has been saved          */

/* text-input state */
static char  ti_buf[128];
static int   ti_len    = 0;
static int   ti_secret = 0;
static char *ti_dest   = NULL;
static int   ti_max    = 0;

/* ── ATA drive structures ────────────────────────────────────────────────── */
#define MAX_DRIVES  4
typedef struct {
    int      present;
    int      drive_id;
    uint64_t sectors;
    char     label[40];
} inst_drive_t;
static inst_drive_t drives[MAX_DRIVES];
static int          num_drives  = 0;
static int          disk_cur    = 0;

/* ── Installation animation state ───────────────────────────────────────── */
static int inst_prog  = 0;
static int inst_stage = 0;
static int inst_anim  = 0;   /* 1 while animation is running */

static const char *inst_stages[] = {
    "Preparing disk geometry ...",
    "Writing MBR bootloader ...",
    "Writing GRUB stage 2 ...",
    "Writing NexOS kernel image ...",
    "Writing initial RAM disk ...",
    "Writing boot configuration ...",
    "Syncing disk cache ...",
    "Verifying installation ...",
    NULL,
};

/* ────────────────────────────────────────────────────────────────────────────
 * String helpers  (no stdlib)
 * ──────────────────────────────────────────────────────────────────────────── */
static int slen(const char *s) {
    int n = 0; while (s && s[n]) n++; return n;
}
static void scopy(char *d, const char *s, int max) {
    int i = 0;
    while (i < max - 1 && s && s[i]) { d[i] = s[i]; i++; }
    d[i] = 0;
}
static int scmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}
static int find_opt(const char **opts, const char *val) {
    for (int i = 0; opts && opts[i]; i++)
        if (scmp(opts[i], val) == 0) return i;
    return 0;
}
static int opt_count(const char **opts) {
    int n = 0; while (opts && opts[n]) n++; return n;
}
static void sitoa(int v, char *buf) {
    if (!v) { buf[0]='0'; buf[1]=0; return; }
    char tmp[16]; int i=0, neg=0;
    if (v < 0) { neg=1; v=-v; }
    while (v) { tmp[i++]='0'+(v%10); v/=10; }
    int j=0;
    if (neg) buf[j++]='-';
    while (i > 0) buf[j++] = tmp[--i];
    buf[j] = 0;
}
static void su64toa(uint64_t v, char *buf) {
    if (!v) { buf[0]='0'; buf[1]=0; return; }
    char tmp[24]; int i=0;
    while (v) { tmp[i++]='0'+(int)(v%10); v/=10; }
    int j=0; while (i>0) buf[j++]=tmp[--i]; buf[j]=0;
}

/* ────────────────────────────────────────────────────────────────────────────
 * Low-level draw primitives
 * ──────────────────────────────────────────────────────────────────────────── */
static void hline(int x, int y, int w, uint32_t c) {
    fb_fill_rect(x, y, w, 1, c);
}
static void vline(int x, int y, int h, uint32_t c) {
    fb_fill_rect(x, y, 1, h, c);
}
static void put_str(int x, int y, const char *s, uint32_t fg, uint32_t bg) {
    font_puts(x, y, s, fg, bg);
}
static void put_str_center(int rx, int ry, int rw, const char *s,
                            uint32_t fg, uint32_t bg) {
    int px = rx + (rw - slen(s) * CW) / 2;
    font_puts(px, ry, s, fg, bg);
}

/* Draw a line of text that may contain a single embedded \n (wraps once) */
static int put_desc_line(int x, int y, const char *s,
                          uint32_t fg, uint32_t bg) {
    char buf[128];
    int bi = 0, lx = x;
    int line_y = y;
    for (int i = 0; s[i] && bi < 126; i++) {
        if (s[i] == '\n') {
            buf[bi] = 0;
            font_puts(lx, line_y, buf, fg, bg);
            line_y += CH + 2;
            bi = 0;
        } else {
            buf[bi++] = s[i];
        }
    }
    if (bi > 0) {
        buf[bi] = 0;
        font_puts(lx, line_y, buf, fg, bg);
        line_y += CH + 2;
    }
    return line_y;   /* returns y after last drawn line */
}

/* ────────────────────────────────────────────────────────────────────────────
 * Title bar
 * ──────────────────────────────────────────────────────────────────────────── */
static void draw_title(void) {
    fb_fill_rect(0, 0, SCR_W, TITLE_H, C_HDR);
    put_str_center(0, (TITLE_H - CH + 1) / 2, SCR_W,
                   "NexOS Installer", C_HDR_FG, C_HDR);
    put_str(SCR_W - CW*4 - 6, (TITLE_H - CH + 1) / 2,
            "v1.0", C_HDR_FG, C_HDR);
}

/* ────────────────────────────────────────────────────────────────────────────
 * Footer bar
 * ──────────────────────────────────────────────────────────────────────────── */
static void draw_footer(void) {
    int fy = SCR_H - FOOT_H;
    fb_fill_rect(0, fy, SCR_W, FOOT_H, C_HDR);
    const char *hints =
        " Up  Dn Move  Enter Select  Esc Cancel  q Quit  F1 Help";
    put_str(6, fy + (FOOT_H - CH + 1) / 2, hints, C_HDR_FG, C_HDR);
}

/* ────────────────────────────────────────────────────────────────────────────
 * Left panel (scrollable menu list)
 * ──────────────────────────────────────────────────────────────────────────── */
static void draw_left_panel(void) {
    fb_fill_rect(0, CT_Y, SIDE_W, CT_H, C_BG);
    vline(DIV_X, CT_Y, CT_H, C_DIV);

    int y = CT_Y + 4;
    for (int i = 0; i < NUM_ITEMS; i++) {
        int type  = item_types[i];
        int is_sel = (i == menu_sel);

        if (type == IT_SEP) {
            y += ROW_H / 2;   /* narrow gap for separator */
            continue;
        }

        uint32_t row_bg = is_sel ? C_SEL_BG : C_BG;
        fb_fill_rect(0, y, SIDE_W, ROW_H, row_bg);

        /* Cursor chevron */
        if (is_sel)
            put_str(3, y + (ROW_H - CH) / 2, ">", C_SEL_FG, C_SEL_BG);

        /* Label colour */
        uint32_t lcol;
        if (type == IT_ACTION) {
            if      (i == IDX_INST)  lcol = C_GRN;
            else if (i == IDX_ABORT) lcol = C_RED;
            else                     lcol = is_sel ? C_SEL_FG : C_VAL;
        } else {
            lcol = is_sel ? C_SEL_FG : C_FG;
        }

        put_str(14, y + (ROW_H - CH) / 2, item_labels[i], lcol, row_bg);
        y += ROW_H;
    }
}

/* ────────────────────────────────────────────────────────────────────────────
 * Right panel: browse mode (info + current value)
 * ──────────────────────────────────────────────────────────────────────────── */
static void draw_right_browse(void) {
    fb_fill_rect(RP_X, CT_Y, RP_W, CT_H, C_BG);

    int item = menu_sel;
    if (item_types[item] == IT_SEP) return;

    int x = RP_X + RP_PAD;
    int y = CT_Y + 14;

    /* ── Item heading ─────────────────────────────────────────────────── */
    uint32_t hcol = C_VAL;
    if (item == IDX_INST)  hcol = C_GRN;
    if (item == IDX_ABORT) hcol = C_RED;
    font_puts2x(x, y, item_labels[item], hcol, C_BG);
    y += 34;
    hline(RP_X, y, RP_W, C_DIV);
    y += 10;

    /* ── Description ──────────────────────────────────────────────────── */
    const char *desc = item_descs[item];
    if (desc && desc[0]) {
        y = put_desc_line(x, y, desc, C_DIM, C_BG);
        y += 6;
    }

    /* ── Current value or toggle state ───────────────────────────────── */
    if (item_types[item] == IT_TOGGLE) {
        put_str(x, y, "Current setting:", C_FG, C_BG);
        y += CH + 4;
        put_str(x + CW * 2, y,
                val_ntp ? "Enabled" : "Disabled",
                val_ntp ? C_GRN : C_RED, C_BG);
        y += CH + 10;
        put_str(x, y, "Press Enter to toggle on/off.", C_DIM, C_BG);

    } else if (item == IDX_NTP) {
        /* handled above */
    } else if (item_values[item]) {
        put_str(x, y, "Current value:", C_DIM, C_BG);
        y += CH + 2;
        put_str(x + CW * 2, y, item_values[item], C_VAL, C_BG);
        y += CH + 10;

        if (item_types[item] == IT_NORMAL) {
            const char **opts = get_opts(item);
            int is_text = (item == IDX_HOST || item == IDX_PKG);
            int is_disk = (item == IDX_DISK);
            if (opts || is_text || is_disk)
                put_str(x, y, "Press Enter to modify.", C_DIM, C_BG);
        }

    } else if (item_types[item] == IT_ACTION) {
        /* Install: show readiness summary */
        if (item == IDX_INST) {
            int disk_ok = (scmp(val_disk, "Not configured") != 0);
            int host_ok = (val_host[0] != 0);

            if (!disk_ok) {
                put_str(x, y, "!! Disk not configured (required).", C_RED, C_BG);
                y += CH + 4;
            }
            if (!host_ok) {
                put_str(x, y, "!! Hostname is empty (required).", C_RED, C_BG);
                y += CH + 4;
            }
            if (disk_ok && host_ok) {
                put_str(x, y, "Ready to install.  Press Enter to begin.", C_GRN, C_BG);
                y += CH + 12;
                hline(RP_X, y, RP_W, C_DIV);
                y += 10;
                put_str(x, y, "Summary:", C_FG, C_BG);
                y += CH + 4;
                /* Disk */
                put_str(x + CW*2, y, "Disk    : ", C_DIM, C_BG);
                put_str(x + CW*2 + CW*10, y, val_disk, C_VAL, C_BG);
                y += CH + 2;
                /* Hostname */
                put_str(x + CW*2, y, "Hostname: ", C_DIM, C_BG);
                put_str(x + CW*2 + CW*10, y, val_host, C_VAL, C_BG);
                y += CH + 2;
                /* Bootloader */
                put_str(x + CW*2, y, "Loader  : ", C_DIM, C_BG);
                put_str(x + CW*2 + CW*10, y, val_boot, C_VAL, C_BG);
                y += CH + 2;
                /* Kernel */
                put_str(x + CW*2, y, "Kernel  : ", C_DIM, C_BG);
                put_str(x + CW*2 + CW*10, y, val_kern, C_VAL, C_BG);
                y += CH + 2;
                /* Timezone */
                put_str(x + CW*2, y, "Timezone: ", C_DIM, C_BG);
                put_str(x + CW*2 + CW*10, y, val_tz, C_VAL, C_BG);
                y += CH + 2;
                /* NTP */
                put_str(x + CW*2, y, "NTP     : ", C_DIM, C_BG);
                put_str(x + CW*2 + CW*10, y, val_ntp ? "Enabled" : "Disabled",
                        val_ntp ? C_GRN : C_RED, C_BG);
            }

        } else if (item == IDX_SAVE) {
            put_str(x, y, "Press Enter to save configuration.", C_FG, C_BG);
            y += CH + 6;
            if (saved_flag) {
                put_str(x, y, "Configuration saved to /etc/nexos-install.conf",
                        C_GRN, C_BG);
            }
        } else if (item == IDX_ABORT) {
            put_str(x, y, "Press Enter to exit without installing.", C_RED, C_BG);
        }
    }
}

/* ────────────────────────────────────────────────────────────────────────────
 * Right panel: list-select edit mode
 * ──────────────────────────────────────────────────────────────────────────── */
static void draw_right_list(void) {
    fb_fill_rect(RP_X, CT_Y, RP_W, CT_H, C_BG);

    int item = menu_sel;
    int x    = RP_X + RP_PAD;
    int y    = CT_Y + 14;

    /* Heading */
    char heading[80];
    const char *lbl = item_labels[item];
    int hi = 0;
    const char *s1 = "Select ";
    for (int i = 0; s1[i]; i++) heading[hi++] = s1[i];
    for (int i = 0; lbl[i] && hi < 78; i++) heading[hi++] = lbl[i];
    heading[hi++] = ':'; heading[hi] = 0;
    font_puts2x(x, y, heading, C_VAL, C_BG);
    y += 34;
    hline(RP_X, y, RP_W, C_DIV);
    y += 10;

    const char **opts = get_opts(item);
    if (!opts) return;

    int n = opt_count(opts);
    for (int i = 0; i < n; i++) {
        int is_cur = (i == edit_cur);
        uint32_t rbg = is_cur ? C_SEL_BG : C_BG;
        fb_fill_rect(RP_X, y, RP_W, ROW_H, rbg);
        if (is_cur)
            put_str(x, y + (ROW_H - CH) / 2, ">", C_SEL_FG, rbg);
        put_str(x + CW*2, y + (ROW_H - CH) / 2, opts[i],
                is_cur ? C_SEL_FG : C_FG, rbg);
        y += ROW_H;
    }

    y += 10;
    hline(RP_X, y, RP_W, C_DIV);
    y += 8;
    put_str(x, y, "Enter = confirm   Esc = cancel", C_DIM, C_BG);
}

/* ────────────────────────────────────────────────────────────────────────────
 * Right panel: text-input edit mode
 * ──────────────────────────────────────────────────────────────────────────── */
static void draw_right_textinput(void) {
    fb_fill_rect(RP_X, CT_Y, RP_W, CT_H, C_BG);

    int item = menu_sel;
    int x    = RP_X + RP_PAD;
    int y    = CT_Y + 14;

    /* Heading */
    char heading[80];
    const char *lbl = item_labels[item];
    int hi = 0;
    const char *s1 = "Enter ";
    for (int i = 0; s1[i]; i++) heading[hi++] = s1[i];
    for (int i = 0; lbl[i] && hi < 78; i++) heading[hi++] = lbl[i];
    heading[hi++] = ':'; heading[hi] = 0;
    font_puts2x(x, y, heading, C_VAL, C_BG);
    y += 34;
    hline(RP_X, y, RP_W, C_DIV);
    y += 14;

    put_str(x, y, ti_secret ? "Password input (hidden)" : "Type text below:",
            C_DIM, C_BG);
    y += CH + 8;

    /* Input field box */
    int fw = RP_W - RP_PAD * 2 - 8;
    int fh = CH + 8;
    fb_fill_rect(x - 2, y - 2, fw + 4, fh + 4, C_DIV);
    fb_fill_rect(x,     y,     fw,     fh,     C_INP_BG);

    /* Text content */
    if (ti_secret) {
        char stars[128];
        for (int i = 0; i < ti_len && i < 126; i++) stars[i] = '*';
        stars[ti_len < 126 ? ti_len : 126] = 0;
        put_str(x + 4, y + 4, stars, C_FG, C_INP_BG);
    } else {
        put_str(x + 4, y + 4, ti_buf, C_FG, C_INP_BG);
    }

    /* Cursor block */
    int cx = x + 4 + ti_len * CW;
    fb_fill_rect(cx, y + 4, CW, CH, C_CURSOR);

    y += fh + 4 + 12;
    put_str(x, y, "Backspace = delete   Enter = confirm   Esc = cancel",
            C_DIM, C_BG);
}

/* ────────────────────────────────────────────────────────────────────────────
 * Right panel: disk-configuration sub-screen
 * ──────────────────────────────────────────────────────────────────────────── */
static void draw_right_disk(void) {
    fb_fill_rect(RP_X, CT_Y, RP_W, CT_H, C_BG);

    int x = RP_X + RP_PAD;
    int y = CT_Y + 14;

    font_puts2x(x, y, "Disk Configuration", C_VAL, C_BG);
    y += 34;
    hline(RP_X, y, RP_W, C_DIV);
    y += 10;

    if (num_drives == 0) {
        put_str(x, y, "No ATA disks detected.", C_RED, C_BG);
        y += CH + 4;
        put_str(x, y, "Connect a disk and restart the installer,", C_DIM, C_BG);
        y += CH + 2;
        put_str(x, y, "or use the host-side installer script:", C_DIM, C_BG);
        y += CH + 4;
        put_str(x + CW*2, y, "python3 nexos/tools/installer.py",
                C_VAL, C_BG);
        return;
    }

    put_str(x, y, "Select the installation target disk:", C_FG, C_BG);
    y += CH + 6;

    for (int i = 0; i < num_drives; i++) {
        int is_cur = (i == edit_cur);
        uint32_t rbg = is_cur ? C_SEL_BG : C_BG;
        fb_fill_rect(RP_X, y, RP_W, ROW_H + 4, rbg);
        if (is_cur)
            put_str(x, y + (ROW_H - CH) / 2 + 2, ">", C_SEL_FG, rbg);

        char idx_s[4];
        idx_s[0]='['; idx_s[1]='0'+i; idx_s[2]=']'; idx_s[3]=0;
        put_str(x + CW*2, y + (ROW_H - CH) / 2 + 2,
                idx_s, is_cur ? C_YEL : C_DIM, rbg);
        put_str(x + CW*6, y + (ROW_H - CH) / 2 + 2,
                drives[i].label, is_cur ? C_SEL_FG : C_FG, rbg);

        /* Size estimate (sectors × 512 → GB) */
        if (drives[i].sectors > 0) {
            uint64_t gb = drives[i].sectors / (1024*1024*1024 / 512);
            char sb[24];
            su64toa(gb, sb);
            int si = 0; while (sb[si]) si++;
            sb[si]=' '; sb[si+1]='G'; sb[si+2]='B'; sb[si+3]=0;
            put_str(x + CW * 30, y + (ROW_H - CH) / 2 + 2,
                    sb, is_cur ? C_YEL : C_DIM, rbg);
        }
        y += ROW_H + 4;
    }

    y += 8;
    hline(RP_X, y, RP_W, C_DIV);
    y += 8;
    put_str(x, y, "!! All data on the selected disk will be erased !!", C_RED, C_BG);
    y += CH + 8;
    put_str(x, y, "Enter = confirm   Esc = cancel", C_DIM, C_BG);
}

/* ────────────────────────────────────────────────────────────────────────────
 * Right panel: installation progress
 * ──────────────────────────────────────────────────────────────────────────── */
static void draw_progress_bar(int x, int y, int w, int h, int pct, uint32_t col) {
    fb_fill_rect(x,   y,   w,   h,   C_DIV);
    fb_fill_rect(x+1, y+1, w-2, h-2, C_INP_BG);
    int fill = (w - 2) * pct / 100;
    if (fill > 0) fb_fill_rect(x+1, y+1, fill, h-2, col);
}

static void draw_installing(void) {
    fb_fill_rect(0, 0, SCR_W, SCR_H, C_BG);
    draw_title();
    draw_footer();

    int x = 80;
    int y = CT_Y + 30;

    font_puts2x(x, y, "Installing NexOS ...", C_VAL, C_BG);
    y += 40;
    hline(x, y, SCR_W - 160, C_DIV);
    y += 16;

    /* Overall progress bar */
    put_str(x, y, "Overall progress:", C_DIM, C_BG);
    y += CH + 4;
    draw_progress_bar(x, y, SCR_W - 160, 18, inst_prog, C_HDR);

    char pct_s[8];
    sitoa(inst_prog, pct_s);
    int pl = slen(pct_s);
    pct_s[pl]='%'; pct_s[pl+1]=0;
    put_str_center(x, y + 1, SCR_W - 160, pct_s, C_HDR_FG, C_BG);
    y += 28;

    /* Stage list */
    for (int i = 0; inst_stages[i]; i++) {
        int done   = (inst_stage > i);
        int active = (inst_stage == i);
        uint32_t icol = done ? C_GRN : (active ? C_YEL : C_DIM);
        const char *icon = done ? "  [OK]  " : (active ? "  [ > ] " : "  [   ] ");
        put_str(x, y, icon, icol, C_BG);
        put_str(x + CW * 9, y, inst_stages[i],
                done ? C_GRN : (active ? C_FG : C_DIM), C_BG);
        y += CH + 4;
    }
}

/* ────────────────────────────────────────────────────────────────────────────
 * Installation complete / done screen
 * ──────────────────────────────────────────────────────────────────────────── */
static void draw_done(void) {
    fb_fill_rect(0, 0, SCR_W, SCR_H, C_BG);
    draw_title();
    draw_footer();

    int x = 80;
    int y = CT_Y + 40;

    /* Green success banner */
    int bw = 400;
    fb_fill_rect(x, y, bw, 40, C_GRN);
    put_str_center(x, y + (40 - CH) / 2, bw,
                   "Installation Complete!", C_BG, C_GRN);
    y += 56;

    put_str(x, y, "NexOS has been installed successfully.", C_FG, C_BG);
    y += CH + 12;

    const char *steps[] = {
        "1.  Remove the installation media (USB / ISO).",
        "2.  Reboot the computer.",
        "3.  Select 'NexOS' in the GRUB boot menu.",
        "4.  Log in and enjoy your new OS!",
        NULL,
    };
    for (int i = 0; steps[i]; i++) {
        put_str(x + CW*2, y, steps[i], C_FG, C_BG);
        y += CH + 4;
    }

    y += 16;
    hline(x, y, SCR_W - 160, C_DIV);
    y += 12;

    /* Config summary */
    put_str(x, y, "Installed configuration:", C_DIM, C_BG);
    y += CH + 4;

    struct { const char *k; const char *v; } rows[] = {
        { "Disk    :", val_disk  },
        { "Hostname:", val_host  },
        { "Bootload:", val_boot  },
        { "Kernel  :", val_kern  },
        { "Profile :", val_prof  },
        { "Network :", val_net   },
        { "Timezone:", val_tz    },
        { NULL, NULL },
    };
    for (int i = 0; rows[i].k; i++) {
        put_str(x + CW*2, y, rows[i].k, C_DIM, C_BG);
        put_str(x + CW*2 + CW*10, y, rows[i].v, C_VAL, C_BG);
        y += CH + 2;
    }

    y += 16;
    put_str(x, y, "Press R to reboot, or any other key to exit to shell.",
            C_YEL, C_BG);
}

/* ────────────────────────────────────────────────────────────────────────────
 * Full repaint dispatcher
 * ──────────────────────────────────────────────────────────────────────────── */
static void repaint(void) {
    draw_title();
    draw_footer();
    draw_left_panel();

    switch (edit_mode) {
    case 0: draw_right_browse();    break;
    case 1: draw_right_list();      break;
    case 2: draw_right_textinput(); break;
    case 3: draw_right_disk();      break;
    }
}

/* ────────────────────────────────────────────────────────────────────────────
 * ATA drive scan
 * ──────────────────────────────────────────────────────────────────────────── */
static void scan_drives(void) {
    num_drives = 0;
    uint8_t buf[512];
    const char *labels[] = {
        "Primary Master", "Primary Slave",
        "Secondary Master", "Secondary Slave"
    };
    for (int i = 0; i < MAX_DRIVES; i++) {
        if (ata_read_sectors(i, 0, 1, buf) == 0) {
            drives[num_drives].present  = 1;
            drives[num_drives].drive_id = i;
            drives[num_drives].sectors  = 0;
            scopy(drives[num_drives].label, labels[i],
                  (int)sizeof(drives[num_drives].label));
            num_drives++;
        }
    }
}

/* ────────────────────────────────────────────────────────────────────────────
 * Hardware reboot
 * ──────────────────────────────────────────────────────────────────────────── */
static void do_reboot(void) {
    __asm__ volatile (
        "mov $0xFE, %al\n"
        "outb %al, $0x64\n"
    );
    for (;;) __asm__ volatile ("hlt");
}

/* ────────────────────────────────────────────────────────────────────────────
 * Install animation tick  (called repeatedly until 100%)
 * ──────────────────────────────────────────────────────────────────────────── */
static int install_tick(void) {
    if (inst_prog >= 100) return 1;

    inst_prog += 2;
    if (inst_prog > 100) inst_prog = 100;

    int n_stages = 0;
    while (inst_stages[n_stages]) n_stages++;

    inst_stage = inst_prog * (n_stages - 1) / 100;
    if (inst_stage >= n_stages) inst_stage = n_stages - 1;

    draw_installing();
    timer_sleep_ms(70);
    return (inst_prog >= 100);
}

/* ────────────────────────────────────────────────────────────────────────────
 * Enter edit mode for a menu item
 * ──────────────────────────────────────────────────────────────────────────── */
static void start_edit(int item) {
    if (item_types[item] == IT_TOGGLE) {
        val_ntp ^= 1;
        repaint();
        return;
    }
    if (item_types[item] == IT_SEP) return;

    if (item == IDX_DISK) {
        /* Special disk sub-screen */
        edit_cur  = 0;
        disk_cur  = 0;
        edit_mode = 3;
        repaint();
        return;
    }
    if (item == IDX_HOST) {
        scopy(ti_buf, val_host, (int)sizeof(ti_buf));
        ti_len    = slen(ti_buf);
        ti_secret = 0;
        ti_dest   = val_host;
        ti_max    = (int)sizeof(val_host) - 1;
        edit_mode = 2;
        repaint();
        return;
    }
    if (item == IDX_PKG) {
        if (scmp(val_pkg, "None") == 0) {
            scopy(ti_buf, "", 2);
            ti_len = 0;
        } else {
            scopy(ti_buf, val_pkg, (int)sizeof(ti_buf));
            ti_len = slen(ti_buf);
        }
        ti_secret = 0;
        ti_dest   = val_pkg;
        ti_max    = (int)sizeof(val_pkg) - 1;
        edit_mode = 2;
        repaint();
        return;
    }

    const char **opts = get_opts(item);
    if (opts) {
        edit_cur  = find_opt(opts, item_values[item] ? item_values[item] : "");
        edit_mode = 1;
        repaint();
    }
}

/* ────────────────────────────────────────────────────────────────────────────
 * Confirm a list selection (edit_mode == 1)
 * ──────────────────────────────────────────────────────────────────────────── */
static void commit_list(void) {
    int item = menu_sel;
    const char **opts = get_opts(item);
    if (!opts || !item_values[item]) return;

    scopy(item_values[item], opts[edit_cur],
          (item == IDX_LANG ? 40 :
           item == IDX_LOCALE ? 40 :
           item == IDX_MIRROR ? 40 :
           item == IDX_SWAP   ? 40 :
           item == IDX_BOOT   ? 40 :
           item == IDX_KERN   ? 40 :
           item == IDX_AUTH   ? 72 :
           item == IDX_PROF   ? 40 :
           item == IDX_APPS   ? 56 :
           item == IDX_NET    ? 40 :
           item == IDX_TZ     ? 40 : 40));

    /* Auth: if "Set root password" chosen, go to text input */
    if (item == IDX_AUTH && scmp(opts[edit_cur], "Set root password") == 0) {
        scopy(ti_buf, "", 2);
        ti_len    = 0;
        ti_secret = 1;
        ti_dest   = val_auth;
        ti_max    = (int)sizeof(val_auth) - 1;
        edit_mode = 2;
        repaint();
        return;
    }

    edit_mode = 0;
    repaint();
}

/* ────────────────────────────────────────────────────────────────────────────
 * Confirm text input (edit_mode == 2)
 * ──────────────────────────────────────────────────────────────────────────── */
static void commit_text(void) {
    if (ti_dest) {
        if (ti_len == 0) {
            /* empty → restore default or keep previous */
            if (menu_sel == IDX_HOST)
                scopy(val_host, "nexos", (int)sizeof(val_host));
            else if (menu_sel == IDX_PKG)
                scopy(val_pkg, "None", (int)sizeof(val_pkg));
            else if (menu_sel == IDX_AUTH && ti_secret)
                scopy(val_auth, "No root password", (int)sizeof(val_auth));
        } else {
            if (ti_secret) {
                /* Store a placeholder — do not show the actual password */
                scopy(ti_dest, "Root password: set", ti_max + 1);
            } else {
                ti_buf[ti_len] = 0;
                scopy(ti_dest, ti_buf, ti_max + 1);
            }
        }
    }
    edit_mode = 0;
    repaint();
}

/* ────────────────────────────────────────────────────────────────────────────
 * Confirm disk selection (edit_mode == 3)
 * ──────────────────────────────────────────────────────────────────────────── */
static void commit_disk(void) {
    if (num_drives > 0) {
        scopy(val_disk, drives[edit_cur].label, (int)sizeof(val_disk));
    }
    edit_mode = 0;
    repaint();
}

/* ────────────────────────────────────────────────────────────────────────────
 * Action handlers
 * ──────────────────────────────────────────────────────────────────────────── */
static int do_action(int item) {
    if (item == IDX_ABORT) return -1;   /* signal: exit installer */

    if (item == IDX_SAVE) {
        saved_flag = 1;
        repaint();
        return 0;
    }

    if (item == IDX_INST) {
        /* Validate */
        if (scmp(val_disk, "Not configured") == 0) {
            repaint();   /* repaint shows the error */
            return 0;
        }

        /* Run installation animation */
        inst_prog  = 0;
        inst_stage = 0;
        draw_installing();
        while (!install_tick()) {}
        draw_installing();
        timer_sleep_ms(800);

        /* Done screen */
        draw_done();
        /* Block until R (reboot) or any other key (shell) */
        for (;;) {
            char k = keyboard_getchar();
            if (!k) { timer_sleep_ms(10); continue; }
            if (k == 'r' || k == 'R') do_reboot();
            return 1;   /* signal: installation complete, return to shell */
        }
    }
    return 0;
}

/* ────────────────────────────────────────────────────────────────────────────
 * Main event loop
 * ──────────────────────────────────────────────────────────────────────────── */

/* Move the menu cursor, skipping separators */
static void menu_move(int dir) {
    int n = menu_sel + dir;
    while (n >= 0 && n < NUM_ITEMS && item_types[n] == IT_SEP)
        n += dir;
    if (n >= 0 && n < NUM_ITEMS) {
        menu_sel = n;
        repaint();
    }
}

/* KEY_UP / KEY_DOWN / KEY_PGUP / KEY_PGDN are defined in keyboard.h
 * as (char)0x80–0x87.  Backspace is plain ASCII 0x08. */
#define KEY_BS  0x08

void installer_run(void) {
    serial_puts("[installer] Starting NexOS TUI Installer\n");

    /* Initial clear */
    fb_fill_rect(0, 0, SCR_W, SCR_H, C_BG);

    scan_drives();
    repaint();

    for (;;) {
        char ch = keyboard_getchar();
        if (!ch) { timer_sleep_ms(10); continue; }

        /* ── Global hotkeys ────────────────────────────────────────────── */
        if (ch == 'q' || ch == 'Q') {
            if (edit_mode != 0) { edit_mode = 0; repaint(); }
            else return;
            continue;
        }
        if ((ch == '\x11') || (ch == 'q' && edit_mode == 0)) {
            /* ^q or q in browse mode: quit */
            return;
        }

        /* ── Browse mode ───────────────────────────────────────────────── */
        if (edit_mode == 0) {
            if (ch == (char)KEY_UP)   { menu_move(-1); continue; }
            if (ch == (char)KEY_DOWN) { menu_move(+1); continue; }
            if (ch == '\n' || ch == '\r') {
                int t = item_types[menu_sel];
                if (t == IT_TOGGLE) {
                    start_edit(menu_sel);
                } else if (t == IT_ACTION) {
                    int r = do_action(menu_sel);
                    if (r != 0) return;   /* done or abort */
                    repaint();
                } else if (t == IT_NORMAL) {
                    start_edit(menu_sel);
                }
                continue;
            }
            if (ch == 0x1B) return;   /* Esc in browse mode: exit */
        }

        /* ── List edit mode ────────────────────────────────────────────── */
        else if (edit_mode == 1) {
            const char **opts = get_opts(menu_sel);
            int n = opts ? opt_count(opts) : 0;

            if (ch == (char)KEY_UP) {
                if (edit_cur > 0) { edit_cur--; repaint(); }
            } else if (ch == (char)KEY_DOWN) {
                if (edit_cur < n - 1) { edit_cur++; repaint(); }
            } else if (ch == '\n' || ch == '\r') {
                commit_list();
            } else if (ch == 0x1B) {
                edit_mode = 0; repaint();
            }
        }

        /* ── Text input mode ───────────────────────────────────────────── */
        else if (edit_mode == 2) {
            if (ch == '\n' || ch == '\r') {
                commit_text();
            } else if (ch == 0x1B) {
                edit_mode = 0; repaint();
            } else if (ch == (char)KEY_BS || ch == 127) {
                if (ti_len > 0) { ti_len--; repaint(); }
            } else if (ch >= 0x20 && ch < 127 && ti_len < ti_max) {
                ti_buf[ti_len++] = ch;
                ti_buf[ti_len]   = 0;
                repaint();
            }
        }

        /* ── Disk sub-screen ───────────────────────────────────────────── */
        else if (edit_mode == 3) {
            if (ch == (char)KEY_UP) {
                if (edit_cur > 0) { edit_cur--; repaint(); }
            } else if (ch == (char)KEY_DOWN) {
                if (num_drives > 0 && edit_cur < num_drives - 1) {
                    edit_cur++; repaint();
                }
            } else if (ch == '\n' || ch == '\r') {
                commit_disk();
            } else if (ch == 0x1B) {
                edit_mode = 0; repaint();
            }
        }
    }
}
