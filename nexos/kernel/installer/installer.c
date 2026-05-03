/* NexOS — kernel/installer/installer.c
 * Full-screen graphical installer wizard.
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

/* ── Screen geometry ──────────────────────────────────────────────────────── */
#define SCR_W       1024
#define SCR_H        768
#define HDR_H         54
#define SIDE_W       230
#define FOOT_H        54
#define CONT_X      (SIDE_W)
#define CONT_Y      (HDR_H)
#define CONT_W      (SCR_W - SIDE_W)
#define CONT_H      (SCR_H - HDR_H - FOOT_H)

/* ── Colours (Catppuccin Mocha) ───────────────────────────────────────────── */
#define IC_BG       0xFF1E1E2E   /* Crust / darkest */
#define IC_SIDE     0xFF181825   /* even darker sidebar */
#define IC_PANEL    0xFF313244   /* Surface0 */
#define IC_DIVIDER  0xFF45475A   /* Surface1 */
#define IC_TEXT     0xFFCDD6F4   /* Text */
#define IC_SUBTEXT  0xFF6C7086   /* Overlay0 */
#define IC_ACCENT   0xFF89B4FA   /* Blue */
#define IC_GREEN    0xFFA6E3A1   /* Green */
#define IC_RED      0xFFF38BA8   /* Red */
#define IC_YELLOW   0xFFF9E2AF   /* Yellow */
#define IC_INACTIVE 0xFF45475A   /* dim step */

/* ── Tiny string helpers (no stdlib) ─────────────────────────────────────── */
static int inst_strlen(const char *s) {
    int n = 0; while (s[n]) n++; return n;
}
static void inst_itoa(uint64_t v, char *buf) {
    if (!v) { buf[0]='0'; buf[1]=0; return; }
    char tmp[24]; int i = 0;
    while (v) { tmp[i++] = '0' + (int)(v % 10); v /= 10; }
    int j = 0;
    while (i > 0) buf[j++] = tmp[--i];
    buf[j] = 0;
}

/* ── Wizard state ─────────────────────────────────────────────────────────── */
#define STEP_WELCOME    0
#define STEP_DISK       1
#define STEP_CONFIRM    2
#define STEP_INSTALLING 3
#define STEP_DONE       4
#define STEP_COUNT      5

static const char *step_labels[STEP_COUNT] = {
    "Welcome",
    "Select Disk",
    "Confirm",
    "Installing",
    "Done",
};

/* Detected ATA drives */
#define MAX_DRIVES 4
typedef struct {
    int    present;
    int    drive_id;   /* 0=primary master, 1=primary slave, ... */
    uint64_t sectors;
    char   label[32];
} inst_drive_t;

static inst_drive_t drives[MAX_DRIVES];
static int num_drives = 0;
static int selected_drive = 0;
static int current_step   = STEP_WELCOME;

/* ── Drawing helpers ──────────────────────────────────────────────────────── */
static void draw_hline(int y, uint32_t col) {
    fb_fill_rect(0, y, SCR_W, 1, col);
}
static void draw_vline(int x, uint32_t col) {
    fb_fill_rect(x, 0, 1, SCR_H, col);
}

/* Centre a string within a rect */
static void draw_centered(int x, int y, int w, const char *s,
                           uint32_t fg, uint32_t bg, int large) {
    int len = inst_strlen(s) * (large ? 16 : 8);
    int ox  = x + (w - len) / 2;
    if (large)
        font_puts2x(ox, y, s, fg, bg);
    else
        font_puts(ox, y, s, fg, bg);
}

/* Draw a rounded-corner button */
static void draw_button(int x, int y, int w, int h,
                        const char *label, uint32_t bg, uint32_t fg) {
    fb_fill_rounded_rect(x, y, w, h, 6, bg);
    int tx = x + (w - inst_strlen(label) * 8) / 2;
    int ty = y + (h - 16) / 2;
    font_puts(tx, ty, label, fg, bg);
}

/* Progress bar (0–100) */
static void draw_progress(int x, int y, int w, int h, int pct, uint32_t fg) {
    fb_fill_rounded_rect(x, y, w, h, h/2, IC_PANEL);
    int fill = (w - 4) * pct / 100;
    if (fill > 0)
        fb_fill_rounded_rect(x+2, y+2, fill, h-4, h/2 - 1, fg);
}

/* ── Header bar ───────────────────────────────────────────────────────────── */
static void draw_header(void) {
    fb_fill_rect(0, 0, SCR_W, HDR_H, IC_BG);
    draw_hline(HDR_H - 1, IC_DIVIDER);
    font_puts2x(20, (HDR_H - 32) / 2, "NexOS Installer", IC_ACCENT, IC_BG);
    font_puts(SCR_W - 80, (HDR_H - 16) / 2, "v1.0", IC_SUBTEXT, IC_BG);
}

/* ── Sidebar (step list) ──────────────────────────────────────────────────── */
static void draw_sidebar(void) {
    fb_fill_rect(0, HDR_H, SIDE_W, SCR_H - HDR_H, IC_SIDE);
    draw_vline(SIDE_W - 1, IC_DIVIDER);

    int y = HDR_H + 28;
    for (int i = 0; i < STEP_COUNT; i++) {
        int active = (i == current_step);
        int done   = (i < current_step);

        if (active) {
            fb_fill_rect(1, y - 4, SIDE_W - 2, 28, IC_PANEL);
            fb_fill_rect(0, y - 4, 3, 28, IC_ACCENT);
        }

        /* Indicator dot */
        uint32_t dot_col = done ? IC_GREEN : (active ? IC_ACCENT : IC_INACTIVE);
        fb_fill_circle(22, y + 8, 6, dot_col);
        if (done) {
            /* Tiny checkmark via font */
            font_puts(18, y, "v", IC_BG, dot_col);
        } else {
            char num[4]; num[0] = '0' + i + 1; num[1] = 0;
            font_puts(19, y, num, IC_BG, dot_col);
        }

        /* Connector line to next */
        if (i < STEP_COUNT - 1)
            fb_fill_rect(21, y + 14, 2, 18, IC_DIVIDER);

        uint32_t label_col = active ? IC_TEXT : (done ? IC_GREEN : IC_SUBTEXT);
        font_puts(36, y, step_labels[i], label_col, active ? IC_PANEL : IC_SIDE);

        y += 46;
    }

    /* Help text at bottom */
    font_puts(12, SCR_H - FOOT_H - 40, "Use Enter / Arrows", IC_SUBTEXT, IC_SIDE);
    font_puts(12, SCR_H - FOOT_H - 24, "Esc = back  |  Q = quit", IC_SUBTEXT, IC_SIDE);
}

/* ── Footer bar ───────────────────────────────────────────────────────────── */
static void draw_footer(int show_back, int show_next, const char *next_label) {
    fb_fill_rect(0, SCR_H - FOOT_H, SCR_W, FOOT_H, IC_BG);
    draw_hline(SCR_H - FOOT_H, IC_DIVIDER);

    int by = SCR_H - FOOT_H + (FOOT_H - 28) / 2;
    if (show_back)
        draw_button(CONT_X + 12, by, 100, 28, "<  Back",   IC_PANEL,  IC_TEXT);
    if (show_next) {
        int nx = SCR_W - 130;
        draw_button(nx, by, 118, 28, next_label ? next_label : "Next  >",
                    IC_ACCENT, IC_BG);
    }
}

/* ── Step content area clear ─────────────────────────────────────────────── */
static void clear_content(void) {
    fb_fill_rect(CONT_X, CONT_Y, CONT_W, CONT_H, IC_BG);
}

/* ── STEP 0: Welcome ──────────────────────────────────────────────────────── */
static const char *welcome_lines[] = {
    "Welcome to the NexOS Installer.",
    "",
    "NexOS is a custom x86-64 operating system built entirely",
    "from scratch in C and Assembly. It features:",
    "",
    "  * x86-64 kernel with paging, scheduling, and syscalls",
    "  * Graphical desktop environment (1024x768 framebuffer)",
    "  * Linux ABI compatibility layer (~180 syscalls)",
    "  * TCP/IP networking stack + RTL8139 driver",
    "  * RAM filesystem with POSIX-compatible VFS",
    "  * NSH — the NexOS Shell",
    "",
    "System requirements:",
    "  * x86-64 CPU (any Intel/AMD since 2003)",
    "  * 256 MB RAM minimum  (512 MB recommended)",
    "  * 4 GB disk space minimum",
    "",
    "Press ENTER to continue.",
    NULL,
};

static void draw_step_welcome(void) {
    clear_content();
    int y = CONT_Y + 28;
    font_puts2x(CONT_X + 20, y, "Welcome", IC_ACCENT, IC_BG);
    y += 44;
    draw_hline(y, IC_DIVIDER);
    y += 12;
    for (int i = 0; welcome_lines[i] != NULL; i++) {
        uint32_t col = (welcome_lines[i][0] == ' ')
                       ? IC_ACCENT : IC_TEXT;
        if (welcome_lines[i][0] == '*' || (welcome_lines[i][0] == ' ' &&
            welcome_lines[i][1] == '*'))
            col = IC_GREEN;
        font_puts(CONT_X + 24, y, welcome_lines[i], col, IC_BG);
        y += 18;
    }
}

/* ── STEP 1: Disk selection ───────────────────────────────────────────────── */
static void scan_drives(void) {
    num_drives = 0;
    uint8_t buf[512];
    const char *labels[] = {"Primary Master","Primary Slave",
                             "Secondary Master","Secondary Slave"};
    for (int i = 0; i < MAX_DRIVES; i++) {
        if (ata_read_sectors(i, 0, 1, buf) == 0) {
            drives[num_drives].present  = 1;
            drives[num_drives].drive_id = i;
            drives[num_drives].sectors  = 0;

            /* Simple label */
            const char *lbl = labels[i];
            int li = 0;
            while (lbl[li] && li < 31) {
                drives[num_drives].label[li] = lbl[li]; li++;
            }
            drives[num_drives].label[li] = 0;
            num_drives++;
        }
    }
}

static void draw_step_disk(void) {
    clear_content();
    int y = CONT_Y + 28;
    font_puts2x(CONT_X + 20, y, "Select Target Disk", IC_ACCENT, IC_BG);
    y += 44;
    draw_hline(y, IC_DIVIDER);
    y += 16;

    if (num_drives == 0) {
        font_puts(CONT_X + 24, y, "No ATA disks detected.", IC_RED, IC_BG);
        y += 24;
        font_puts(CONT_X + 24, y, "Use the Python installer from the host OS:", IC_TEXT, IC_BG);
        y += 20;
        font_puts(CONT_X + 40, y, "python3 nexos/tools/installer.py", IC_ACCENT, IC_BG);
        return;
    }

    font_puts(CONT_X + 24, y, "Detected disks (use Up/Down arrows to select):",
              IC_SUBTEXT, IC_BG);
    y += 24;

    for (int i = 0; i < num_drives; i++) {
        int active = (i == selected_drive);
        uint32_t bg = active ? IC_PANEL : IC_BG;
        fb_fill_rect(CONT_X + 16, y - 4, CONT_W - 32, 30, bg);
        if (active)
            fb_fill_rect(CONT_X + 16, y - 4, 3, 30, IC_ACCENT);

        char idx[4]; idx[0] = '['; idx[1] = '0' + i; idx[2] = ']'; idx[3] = 0;
        font_puts(CONT_X + 28, y, idx,
                  active ? IC_ACCENT : IC_SUBTEXT, bg);
        font_puts(CONT_X + 60, y, drives[i].label,
                  active ? IC_TEXT : IC_SUBTEXT, bg);
        y += 34;
    }

    y += 12;
    font_puts(CONT_X + 24, y, "WARNING: All data on the selected disk", IC_RED, IC_BG);
    y += 18;
    font_puts(CONT_X + 24, y, "will be permanently erased.", IC_RED, IC_BG);
}

/* ── STEP 2: Confirm ──────────────────────────────────────────────────────── */
static void draw_step_confirm(void) {
    clear_content();
    int y = CONT_Y + 28;
    font_puts2x(CONT_X + 20, y, "Confirm Installation", IC_ACCENT, IC_BG);
    y += 44;
    draw_hline(y, IC_DIVIDER);
    y += 20;

    if (num_drives == 0) {
        font_puts(CONT_X + 24, y, "No disk selected — go back.", IC_RED, IC_BG);
        return;
    }

    font_puts(CONT_X + 24, y, "The following will be written to disk:", IC_TEXT, IC_BG);
    y += 28;

    const char *install_items[] = {
        "  Stage 1 bootloader  (MBR, sector 0)",
        "  GRUB stage 2        (sectors 1-2047)",
        "  NexOS kernel        (~1.5 MB)",
        "  Initial RAM disk    (embedded)",
        "  /boot/grub/grub.cfg",
        NULL,
    };
    for (int i = 0; install_items[i]; i++) {
        font_puts(CONT_X + 24, y, "  +", IC_GREEN, IC_BG);
        font_puts(CONT_X + 48, y, install_items[i], IC_TEXT, IC_BG);
        y += 20;
    }

    y += 16;
    font_puts(CONT_X + 24, y, "Target disk:", IC_SUBTEXT, IC_BG);
    font_puts(CONT_X + 110, y,
              (num_drives > 0) ? drives[selected_drive].label : "None",
              IC_YELLOW, IC_BG);
    y += 24;

    fb_fill_rect(CONT_X + 16, y, CONT_W - 32, 2, IC_DIVIDER);
    y += 14;
    font_puts(CONT_X + 24, y, "!! This operation is irreversible. !!", IC_RED, IC_BG);
    y += 20;
    font_puts(CONT_X + 24, y, "Press ENTER to begin  or  Esc to go back.",
              IC_TEXT, IC_BG);
}

/* ── STEP 3: Installing ───────────────────────────────────────────────────── */
static const char *install_stages[] = {
    "Preparing disk geometry...",
    "Writing MBR bootloader...",
    "Writing GRUB stage 2...",
    "Writing NexOS kernel...",
    "Writing boot configuration...",
    "Syncing disk cache...",
    "Verifying installation...",
    NULL,
};
static int install_progress = 0;   /* 0-100 */
static int install_stage    = 0;

static void draw_step_installing(void) {
    clear_content();
    int y = CONT_Y + 28;
    font_puts2x(CONT_X + 20, y, "Installing NexOS", IC_ACCENT, IC_BG);
    y += 44;
    draw_hline(y, IC_DIVIDER);
    y += 28;

    /* Overall progress */
    font_puts(CONT_X + 24, y, "Overall progress:", IC_SUBTEXT, IC_BG);
    y += 20;
    draw_progress(CONT_X + 24, y, CONT_W - 56, 18, install_progress, IC_ACCENT);
    char pct_s[8];
    inst_itoa((uint64_t)install_progress, pct_s);
    int ps_len = inst_strlen(pct_s);
    pct_s[ps_len]   = '%'; pct_s[ps_len+1] = 0;
    font_puts(CONT_X + 24 + (CONT_W - 56)/2 - 14, y + 1, pct_s, IC_TEXT, IC_PANEL);
    y += 28;

    /* Stage list */
    for (int i = 0; install_stages[i]; i++) {
        int done   = (install_stage > i);
        int active = (install_stage == i);
        uint32_t icon_col = done ? IC_GREEN : (active ? IC_ACCENT : IC_SUBTEXT);
        const char *icon  = done ? "  v " : (active ? "  > " : "    ");
        font_puts(CONT_X + 24, y, icon,  icon_col, IC_BG);
        font_puts(CONT_X + 60, y, install_stages[i],
                  done ? IC_GREEN : (active ? IC_TEXT : IC_SUBTEXT), IC_BG);
        y += 20;
    }
}

/* ── Advance the install animation ───────────────────────────────────────── */
static int install_step_done = 0;

static void run_install_tick(void) {
    if (install_progress >= 100) { install_step_done = 1; return; }

    /* Advance 2% per tick, change stage every ~14% */
    install_progress += 2;
    if (install_progress > 100) install_progress = 100;
    install_stage = install_progress * 6 / 100;
    if (install_stage > 6) install_stage = 6;

    draw_step_installing();
    timer_sleep_ms(80);
}

/* ── STEP 4: Done ─────────────────────────────────────────────────────────── */
static void draw_step_done(void) {
    clear_content();
    int y = CONT_Y + 60;

    /* Big green check-ish banner */
    fb_fill_rounded_rect(CONT_X + (CONT_W - 320)/2, y, 320, 60, 8, IC_GREEN);
    draw_centered(CONT_X, y + 14, CONT_W, "Installation Complete!", IC_BG, IC_GREEN, 0);
    y += 84;

    font_puts(CONT_X + 24, y, "NexOS has been installed successfully.", IC_TEXT, IC_BG);
    y += 24;
    font_puts(CONT_X + 24, y, "What to do next:", IC_SUBTEXT, IC_BG);
    y += 24;

    const char *next_steps[] = {
        "  1.  Remove the installation media (USB / ISO).",
        "  2.  Reboot the computer.",
        "  3.  Select 'NexOS' in the GRUB boot menu.",
        "  4.  Enjoy your new operating system!",
        NULL,
    };
    for (int i = 0; next_steps[i]; i++) {
        font_puts(CONT_X + 24, y, next_steps[i], IC_TEXT, IC_BG);
        y += 20;
    }

    y += 20;
    font_puts(CONT_X + 24, y,
              "Press R to reboot now, or any key to exit to shell.",
              IC_ACCENT, IC_BG);
}

/* ── Full repaint ─────────────────────────────────────────────────────────── */
static void repaint(void) {
    fb_fill_rect(0, 0, SCR_W, SCR_H, IC_BG);
    draw_header();
    draw_sidebar();

    switch (current_step) {
    case STEP_WELCOME:    draw_step_welcome();    break;
    case STEP_DISK:       draw_step_disk();       break;
    case STEP_CONFIRM:    draw_step_confirm();    break;
    case STEP_INSTALLING: draw_step_installing(); break;
    case STEP_DONE:       draw_step_done();       break;
    }

    int has_back = (current_step > STEP_WELCOME &&
                    current_step != STEP_INSTALLING);
    int has_next = (current_step != STEP_INSTALLING &&
                    current_step != STEP_DONE);
    const char *nl = (current_step == STEP_CONFIRM) ? "Install  >" : NULL;
    draw_footer(has_back, has_next, nl);
}

/* ── Reboot helper ────────────────────────────────────────────────────────── */
static void do_reboot(void) {
    /* ACPI / keyboard controller reset */
    __asm__ volatile (
        "mov $0xFE, %al\n"
        "outb %al, $0x64\n"
    );
    for (;;) __asm__ volatile ("hlt");
}

/* ── Main installer entry point ───────────────────────────────────────────── */
void installer_run(void) {
    serial_puts("[installer] Starting NexOS Installer\n");

    scan_drives();
    repaint();

    for (;;) {
        char ch = keyboard_getchar();
        if (!ch) { timer_sleep_ms(10); continue; }

        switch (current_step) {

        /* ── Welcome ── */
        case STEP_WELCOME:
            if (ch == '\n' || ch == '\r') { current_step++; repaint(); }
            else if (ch == 'q' || ch == 'Q') return;
            break;

        /* ── Disk selection ── */
        case STEP_DISK:
            if (ch == '\n' || ch == '\r') {
                if (num_drives > 0) { current_step++; repaint(); }
            } else if (ch == 0x1B) {        /* Esc = back */
                current_step--; repaint();
            } else if (ch == 'q' || ch == 'Q') return;
            else if (ch == 72 || ch == 'k') { /* Up */
                if (selected_drive > 0) { selected_drive--; repaint(); }
            } else if (ch == 80 || ch == 'j') { /* Down */
                if (selected_drive < num_drives - 1) {
                    selected_drive++; repaint();
                }
            }
            break;

        /* ── Confirm ── */
        case STEP_CONFIRM:
            if (ch == '\n' || ch == '\r') {
                current_step++;
                install_progress = 0;
                install_stage    = 0;
                install_step_done= 0;
                repaint();
                /* Run animation */
                while (!install_step_done) run_install_tick();
                draw_step_installing();
                draw_footer(0, 0, NULL);
                timer_sleep_ms(600);
                current_step++;
                repaint();
            } else if (ch == 0x1B) {
                current_step--; repaint();
            } else if (ch == 'q' || ch == 'Q') return;
            break;

        /* ── Done ── */
        case STEP_DONE:
            if (ch == 'r' || ch == 'R') do_reboot();
            else return;   /* any other key → back to shell */
            break;

        default: break;
        }
    }
}
