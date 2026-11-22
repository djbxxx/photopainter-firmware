// PhotoPainter custom firmware.
//
// On battery: wake (timer or NEXT) -> convert any new pictures -> show the next one
//             -> arm the timer -> cut power.
// On USB:     the SD card shows up on the computer as a drive. Eject it (or press
//             NEXT) and the frame converts new pictures and shows the first new one.
//             NEXT = next picture, hold NEXT 2 s = drive mode again.
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/rand.h"
#include "hardware/watchdog.h"
#include "hardware/vreg.h"
#include "hardware/clocks.h"
#include "tusb.h"
#include "board.h"
#include "config.h"
#include "epd.h"
#include "library.h"
#include "pcf85063.h"
#include "ui.h"
#include "usb.h"
#include "ff.h"
#include "pico/stdio/driver.h"

#define FW_VERSION "1.3"

enum { SCR_PICTURE = 0, SCR_NOCARD, SCR_EMPTY, SCR_LOWBAT };

// ------------------------------------------------------------------ battery log
// Everything printed is also kept in RAM that survives a watchdog reset, and on battery
// it is appended to 0:/.cache/boot.log so problems without USB can be read afterwards.
#define RAMLOG_SIZE 6144
#define RAMLOG_MAGIC 0x504C4F47u
static uint32_t __uninitialized_ram(ramlog_magic);
static uint32_t __uninitialized_ram(ramlog_len);
static char __uninitialized_ram(ramlog_buf)[RAMLOG_SIZE];

static void ramlog_out(const char *buf, int len) {
    for (int i = 0; i < len; i++) {
        if (ramlog_len >= RAMLOG_SIZE) {                    // keep the newest text
            memmove(ramlog_buf, ramlog_buf + RAMLOG_SIZE / 2, RAMLOG_SIZE / 2);
            ramlog_len = RAMLOG_SIZE / 2;
        }
        ramlog_buf[ramlog_len++] = buf[i];
    }
}
static stdio_driver_t ramlog_driver = {.out_chars = ramlog_out};

static void ramlog_init(void) {
    if (ramlog_magic != RAMLOG_MAGIC || ramlog_len > RAMLOG_SIZE) { ramlog_magic = RAMLOG_MAGIC; ramlog_len = 0; }
    stdio_set_driver_enabled(&ramlog_driver, true);
}

// Append the RAM log to the card (mounts/unmounts itself) and clear it.
static void ramlog_save(void) {
    if (!ramlog_len || !lib_mount()) return;
    FIL f;
    FILINFO fi;
    BYTE mode = FA_OPEN_APPEND | FA_WRITE;
    if (f_stat("0:/.cache/boot.log", &fi) == FR_OK && fi.fsize > 64 * 1024) mode = FA_CREATE_ALWAYS | FA_WRITE;
    if (f_open(&f, "0:/.cache/boot.log", mode) == FR_OK) {
        UINT bw;
        f_write(&f, ramlog_buf, ramlog_len, &bw);
        f_close(&f);
        ramlog_len = 0;
    }
    lib_unmount();
}

static uint8_t arena[150 * 1024];   // shared work memory for scanning and converting
static config_t cfg;
static int ram_screen = -1;          // last status screen when the card could not be read

// ------------------------------------------------------------------ screens

static void sink(const uint8_t *row, int len) { epd_write(row, len); keepalive(); }

static void show_screen(const ui_screen_t *s, bool wait) {
    epd_begin();
    ui_render(s, sink);
    epd_end(wait);
}

// "IMG_20240518_183245_holiday.jpg" -> "IMG_20240518...holiday.jpg" (max chars)
static void shorten(char *dst, size_t n, const char *src, int max) {
    int len = (int)strlen(src);
    if (len <= max) { snprintf(dst, n, "%s", src); return; }
    int tail = (max - 3) / 2, head = max - 3 - tail;
    snprintf(dst, n, "%.*s...%s", head, src, src + len - tail);
}

static int prog_last = -1;
static char prog_l1[40], prog_l2[64];

static void on_progress(int done, int total, const char *name) {
    if (name) printf("convert %d/%d: %s\n", done + 1, total, name);
    if (total < 2 || done >= total) return;      // one picture: just show it when ready
    if (epd_refreshing()) return;                // panel still drawing the previous update
    if (done == prog_last) return;
    prog_last = done;
    snprintf(prog_l1, sizeof prog_l1, "Picture %d of %d", done + 1, total);
    shorten(prog_l2, sizeof prog_l2, name, 40);
    ui_screen_t s = {.title = "Preparing pictures", .lines = {prog_l1, prog_l2},
                     .progress = done * 100 / total,
                     .footer = "New pictures are converted once - after that they show right away.",
                     .accent = C_BLUE, .rotate = cfg.rotate};
    show_screen(&s, false);   // keep converting while the panel refreshes
}

static void screen_nocard(void) {
    ui_screen_t s = {.title = "No SD card", .lines = {"Insert a microSD card (FAT32)", "with your pictures in a \"pic\" folder,",
                     "then press NEXT."}, .progress = -1, .accent = C_RED, .rotate = cfg.rotate};
    show_screen(&s, true);
}

static void screen_empty(const lib_summary_t *ls) {
    static char l2[64];
    ui_screen_t s = {.progress = -1, .accent = C_ORANGE, .rotate = cfg.rotate,
                     .footer = "Then eject the drive or press NEXT."};
    if (ls->failed) {
        shorten(l2, sizeof l2, ls->first_error, 44);
        s.title = "Pictures could not be read";
        s.lines[0] = "None of the files in \"pic\" could be used:";
        s.lines[1] = l2;
        s.lines[2] = "";
        s.lines[3] = "Details: photopainter-errors.txt";
        s.lines[4] = "Supported: JPG (not progressive) and BMP.";
    } else {
        s.title = "No pictures yet";
        s.lines[0] = "Copy JPG or BMP pictures into the";
        s.lines[1] = "\"pic\" folder on the SD card.";
        s.lines[2] = "";
        s.lines[3] = "Tip: connect USB to a computer -";
        s.lines[4] = "the frame shows up as a drive.";
    }
    show_screen(&s, true);
}

static void screen_lowbat(void) {
    ui_screen_t s = {.title = "Battery low", .lines = {"Please charge the frame via USB."},
                     .progress = -1, .accent = C_RED, .rotate = cfg.rotate};
    show_screen(&s, true);
}

// ------------------------------------------------------------------ one update

// advance: always show the next picture; otherwise only when the pictures changed.
static void run_cycle(bool advance) {
    board_led_act(true);
    if (!lib_mount()) {
        if (ram_screen != SCR_NOCARD) { screen_nocard(); ram_screen = SCR_NOCARD; }
        board_led_act(false);
        return;
    }
    ram_screen = -1;
    config_load(&cfg);
    lib_summary_t ls;
    prog_last = -1;
    uint32_t t0 = board_millis();
    lib_sync(&cfg, arena, sizeof arena, on_progress, &ls);
    printf("sync: %lu ms\n", (unsigned long)(board_millis() - t0));

    bool progress_on_screen = prog_last >= 0;
    if (ls.ok == 0) {
        if (lib_screen_get() != SCR_EMPTY || ls.changed || progress_on_screen) {
            screen_empty(&ls);
            lib_screen_set(SCR_EMPTY);
        }
    } else if (advance || ls.changed || progress_on_screen || lib_screen_get() != SCR_PICTURE) {
        // New pictures are always shown on their own first; otherwise roll for a collage.
        bool collage = ls.first_new < 0 && ls.ok >= 4 && cfg.collage > 0 && (int)(get_rand_32() % 100) < cfg.collage;
        if (!(collage && lib_show_collage(&cfg))) {
            int k = lib_pick_next(&cfg, &ls);
            if (k >= 0) lib_show(k);
        }
    } else {
        printf("nothing changed, picture stays\n");
    }
    epd_finish();
    lib_unmount();
    board_led_act(false);
}

static void arm_timer(void) { pcf_arm_minutes(cfg.interval_min); }

static void load_config_only(void) {
    if (lib_mount()) { config_load(&cfg); lib_unmount(); }
}

static void power_down(void) {
    epd_finish();
    printf("power off (vbat %.2fV)\n", board_vbat());
    if (!board_usb_power()) ramlog_save();
    sleep_ms_alive(50);
    board_power_off();
    // Still running: NEXT is held, or USB was plugged in.
}

// ------------------------------------------------------------------ USB mode

typedef struct { bool down; uint32_t since; bool long_fired; } button_t;

// Returns 1 on a short press (on release), 2 once when held for 2 s.
static int key_poll(button_t *k) {
    bool d = board_key_down();
    uint32_t now = board_millis();
    int ev = 0;
    if (d && !k->down) { k->down = true; k->since = now; k->long_fired = false; }
    else if (d && k->down && !k->long_fired && now - k->since >= 2000) { k->long_fired = true; ev = 2; }
    else if (!d && k->down) {
        k->down = false;
        if (!k->long_fired && now - k->since >= 30) ev = 1;
    }
    return ev;
}

static bool in_drive;   // drive mode as seen by the main loop (the USB side may switch off on eject)

static void drive_on(bool reconnect) {
    if (reconnect) {   // make the computer notice the drive again
        tud_disconnect();
        sleep_ms_alive(300);
        tud_connect();
    }
    usb_drive_enable();
    in_drive = true;
    printf("drive mode on\n");
}

static void battery_still_on(void);

static void usb_loop(void) {
    button_t key = {0};
    load_config_only();
    arm_timer();

    drive_on(false);
    uint32_t t0 = board_millis();
    while (!usb_host_connected() && board_millis() - t0 < 4000) { keepalive(); sleep_ms(5); }
    bool host = usb_host_connected();
    if (!host) { usb_drive_disable(); in_drive = false; printf("no computer, charging only\n"); }
    uint32_t host_lost_since = 0;

    for (;;) {
        keepalive();
        sleep_ms(2);
        uint32_t now = board_millis();
        int ev = key_poll(&key);
        board_led_pwr(board_charging());
        usb_trace_dump();

        // Serial commands for scripting from the computer. Control characters, so a terminal
        // echoing our own log text back can never trigger them: ^N = NEXT, ^D = drive mode, ^S = status
        int c = getchar_timeout_us(0);
        if (c == 0x0E) ev = 1;
        else if (c == 0x04) ev = 2;
        else if (c == 0x13)
            printf("status: drive=%d host=%d vbat=%.2fV charging=%d interval=%lu\n", in_drive, usb_host_connected(),
                   board_vbat(), board_charging(), (unsigned long)cfg.interval_min);

        if (!board_usb_power()) {          // unplugged: we are on battery now
            usb_drive_disable();
            in_drive = false;
            printf("usb unplugged\n");
            arm_timer();
            power_down();
            battery_still_on();
            continue;
        }

        bool connected = usb_host_connected();
        if (in_drive) {
            board_led_act((now / 500) & 1);    // slow blink = drive mode
            if (!connected) { if (!host_lost_since) host_lost_since = now; }
            else host_lost_since = 0;
            bool lost = host_lost_since && now - host_lost_since > 1500;
            if (usb_drive_ejected() || ev == 1 || lost) {
                usb_drive_disable();
                in_drive = false;
                board_led_act(false);
                printf("drive mode off (%s)\n", usb_drive_ejected() ? "ejected" : ev == 1 ? "NEXT" : "disconnected");
                sleep_ms_alive(300);
                run_cycle(ev == 1);
                arm_timer();
            }
        } else {
            board_led_act(false);
            if (ev == 1) { run_cycle(true); arm_timer(); }
            else if (ev == 2) {
                if (connected) { board_led_act(true); drive_on(true); }
                else printf("hold NEXT: no computer connected\n");
            } else if (!gpio_get(PIN_RTC_INT)) {
                printf("timer\n");
                run_cycle(true);
                arm_timer();
            } else if (connected && !host) {   // a computer was plugged in later
                drive_on(false);
            }
        }
        host = connected;
    }
}

// After power_down() on battery we should be dead. If we are not (NEXT still held, or the
// power latch did not let go), keep working on battery instead of hanging: NEXT and the
// timer still change the picture, plugging in USB switches to USB mode.
static void battery_still_on(void) {
    uint32_t t0 = board_millis();
    while (board_key_down() && board_millis() - t0 < 10000) { keepalive(); sleep_ms(10); }
    sleep_ms_alive(500);
    if (board_usb_power()) return;
    printf("still running on battery %lu ms after power off - latch did not cut power\n",
           (unsigned long)(board_millis() - t0));
    ramlog_save();
    button_t key = {0};
    for (;;) {
        keepalive();
        sleep_ms(5);
        if (board_usb_power()) { printf("usb plugged in\n"); return; }
        int ev = key_poll(&key);
        bool timer = !gpio_get(PIN_RTC_INT);
        if (ev || timer) {
            printf("battery: %s\n", timer ? "timer" : "NEXT");
            run_cycle(true);
            arm_timer();
            power_down();
            t0 = board_millis();
            while (board_key_down() && board_millis() - t0 < 10000) { keepalive(); sleep_ms(10); }
            sleep_ms_alive(500);
        }
        board_led_act((board_millis() / 1000) % 10 == 0);   // short blink every 10 s: alive
    }
}

// ------------------------------------------------------------------ main

int main(void) {
    // 200 MHz is within the RP2040's rated range (needs 1.15 V core); makes converting ~1.6x faster.
    vreg_set_voltage(VREG_VOLTAGE_1_15);
    sleep_ms(2);
    set_sys_clock_khz(200000, true);
    board_init();
    board_led_act(true);   // first sign of life
    bool usb_power = board_usb_power();
    bool key_wake = board_key_down();
    usb_init();
    stdio_init_all();
    ramlog_init();
    bool wd = watchdog_caused_reboot();
    watchdog_enable(8000, true);
    pcf_init();
    bool alarm = pcf_alarm_fired();
    config_defaults(&cfg);

    if (usb_power) {   // give the computer a moment to open the serial port
        uint32_t t = board_millis();
        while (board_millis() - t < 1500) { keepalive(); sleep_ms(5); }
    }
    float v = board_vbat();
    printf("\nPhotoPainter firmware %s  usb=%d key=%d alarm=%d watchdog=%d vbat=%.2fV\n", FW_VERSION, usb_power,
           key_wake, alarm, wd, v);

    if (!usb_power) {
        if (v < 3.1f) {
            printf("battery low\n");
            if (lib_mount()) {
                config_load(&cfg);
                if (lib_screen_get() != SCR_LOWBAT) { screen_lowbat(); lib_screen_set(SCR_LOWBAT); }
                lib_unmount();
            }
            pcf_disarm();   // stay off until charged
            power_down();
        } else {
            run_cycle(true);
            arm_timer();
            power_down();
        }
        battery_still_on();
    }
    usb_loop();
}
