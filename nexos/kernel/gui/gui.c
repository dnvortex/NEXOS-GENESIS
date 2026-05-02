/* NexOS — kernel/gui/gui.c | Main GUI event loop | MIT License */
#include "gui.h"
#include "wm.h"
#include "desktop.h"
#include "taskbar.h"
#include "launcher.h"
#include "term_app.h"
#include "files_app.h"
#include "sysinfo_app.h"
#include "theme_app.h"
#include "notif.h"
#include "../drivers/fb.h"
#include "../drivers/font.h"
#include "../drivers/mouse.h"
#include "../drivers/keyboard.h"
#include "../drivers/timer.h"
#include "../kernel.h"

/* ── Launch helpers (called from launcher.c) ─────────────────────────────── */
static int term_count  = 0;
static int files_count = 0;

void launch_terminal(void) {
    int ox = 60 + (term_count % 4) * 40;
    int oy = 60 + (term_count % 4) * 30;
    term_create(ox, oy);
    term_count++;
}

void launch_filemanager(void) {
    int ox = 560 + (files_count % 3) * 20;
    int oy = 80  + (files_count % 3) * 20;
    files_create(ox, oy);
    files_count++;
}

void launch_sysinfo(void) {
    sysinfo_create(300, 100);
}

void launch_themewin(void) {
    theme_create(380, 200);
}

/* ── Simple pseudo-random for window placement ─────────────────────────── */
static uint32_t g_seed = 12345;
static int prand(int range) {
    g_seed = g_seed * 1664525u + 1013904223u;
    return (int)((g_seed >> 16) % (uint32_t)range);
}

/* ── Main event loop ─────────────────────────────────────────────────────── */
void gui_main(void) {
    if (!fb.initialized) {
        klog(LOG_ERROR, "GUI: framebuffer not initialised — cannot start");
        return;
    }

    /* 1. Init subsystems */
    mouse_init();
    wm_init();
    taskbar_init();
    notif_init();

    /* 2. Draw initial desktop */
    fb_clear(COL_BASE);
    desktop_draw();
    taskbar_draw();

    /* 3. Open startup windows */
    term_create(60,  60);
    files_create(560, 80);

    /* Welcome notification */
    notif_show("NexOS", "Welcome to NexOS 0.1", 4000);

    klog(LOG_INFO, "GUI: entering main loop");

    uint64_t last_frame   = 0;
    uint64_t last_clock   = 0;
    uint64_t last_notif   = 0;
    int      prev_left    = 0;
    int      prev_right   = 0;
    int      prev_mx      = -1;
    int      prev_my      = -1;
    int      ctrl_held    = 0;

    while (1) {
        uint64_t now = timer_get_ticks();

        /* ── Keyboard events ────────────────────────────────────────────── */
        while (keyboard_available()) {
            char key = keyboard_getchar();

            /* Ctrl key combos arrive as ASCII control codes 1-26 */
            if (key >= 1 && key <= 26) {
                ctrl_held = 1;
                switch (key) {
                case 20: /* Ctrl+T — new terminal */
                    term_create(80 + prand(200), 60 + prand(100));
                    term_count++;
                    break;
                case 6:  /* Ctrl+F — new files */
                    launch_filemanager();
                    break;
                case 9:  /* Ctrl+I — system info */
                    launch_sysinfo();
                    break;
                case 23: /* Ctrl+W */
                case 17: /* Ctrl+Q — close focused window */
                    {
                        window_t *fw = wm_focused();
                        if (fw) {
                            if (fw->on_close) fw->on_close(fw);
                            else wm_close(fw);
                        }
                    }
                    break;
                default:
                    wm_handle_key(key);
                    break;
                }
            } else {
                ctrl_held = 0;
                if (key == 27) { /* Escape */
                    if (launcher_is_visible()) launcher_hide();
                } else {
                    wm_handle_key(key);
                    if (launcher_is_visible()) launcher_handle_key(key);
                }
            }
            (void)ctrl_held;
        }

        /* ── Mouse events ───────────────────────────────────────────────── */
        int mx    = mouse_get_x();
        int my    = mouse_get_y();
        int left  = mouse_left();
        int right = mouse_right();
        int tb_y  = taskbar_get_y();

        int left_click  = left  && !prev_left;
        int right_click = right && !prev_right;
        int released    = !left && prev_left;

        if (left_click || right_click || mx != prev_mx || my != prev_my) {
            if (launcher_is_visible() && left_click) {
                launcher_handle_click(mx, my);
            } else if (right_click && my < tb_y) {
                launcher_show(mx, my);
            } else {
                if (my >= tb_y && left_click) {
                    taskbar_handle_click(mx, my);
                } else {
                    wm_handle_mouse(mx, my, left, right);
                }
            }
            prev_mx = mx; prev_my = my;
        }
        if (released) wm_handle_mouse_release(mx, my);
        prev_left  = left;
        prev_right = right;

        /* ── Frame render (~30 fps) ─────────────────────────────────────── */
        if (now - last_frame >= 33) {
            last_frame = now;

            /* Only repaint the desktop gradient when something changed:
               window open/close/minimize/maximize, drag end, or theme switch.
               Skipping this every frame eliminates the full-screen flash that
               caused the glitch — windows repaint themselves in-place fine. */
            if (fb_scene_dirty) {
                desktop_draw();
                fb_scene_dirty = 0;
            }

            wm_render_all();
            taskbar_draw();
            if (launcher_is_visible()) launcher_draw();
            notif_draw();
            cursor_restore();
            cursor_draw(mx, my);
        }

        /* ── Clock update every second ──────────────────────────────────── */
        if (now - last_clock >= 1000) {
            last_clock = now;
            taskbar_update();
        }

        /* ── Notification tick every 33ms ───────────────────────────────── */
        if (now - last_notif >= 33) {
            notif_tick((uint32_t)(now - last_notif));
            last_notif = now;
        }

        /* Yield CPU */
        __asm__ volatile ("sti; hlt");
    }
}
