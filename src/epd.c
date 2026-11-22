#include "epd.h"
#include <stdio.h>
#include "board.h"

static void cmd(uint8_t c) {
    gpio_put(PIN_EPD_DC, 0);
    gpio_put(PIN_EPD_CS, 0);
    spi_write_blocking(EPD_SPI, &c, 1);
    gpio_put(PIN_EPD_CS, 1);
}

static void data(uint8_t d) {
    gpio_put(PIN_EPD_DC, 1);
    gpio_put(PIN_EPD_CS, 0);
    spi_write_blocking(EPD_SPI, &d, 1);
    gpio_put(PIN_EPD_CS, 1);
}

static void cmd_data(uint8_t c, const uint8_t *d, int n) {
    cmd(c);
    for (int i = 0; i < n; i++) data(d[i]);
}
#define CMD(c, ...) do { static const uint8_t _d[] = {__VA_ARGS__}; cmd_data(c, _d, sizeof _d); } while (0)

// BUSY is low while the controller works.
static void wait_idle(const char *what) {
    uint32_t t0 = board_millis();
    while (!gpio_get(PIN_EPD_BUSY)) {
        keepalive();
        sleep_ms(10);
        if (board_millis() - t0 > 60000) { printf("epd: %s timeout\n", what); return; }
    }
    printf("epd: %s %lu ms\n", what, (unsigned long)(board_millis() - t0));
}

static bool refreshing;

bool epd_refreshing(void) {
    return refreshing && !gpio_get(PIN_EPD_BUSY);
}

void epd_finish(void) {
    if (!refreshing) return;
    wait_idle("refresh");
    CMD(0x02, 0x00); wait_idle("power off");   // POWER_OFF
    CMD(0x07, 0xA5);                           // DEEP_SLEEP
    sleep_ms_alive(10);
    board_epd_power(false);
    refreshing = false;
}

void epd_begin(void) {
    epd_finish();
    board_epd_power(true);
    sleep_ms_alive(20);
    gpio_put(PIN_EPD_RST, 1); sleep_ms_alive(20);
    gpio_put(PIN_EPD_RST, 0); sleep_ms_alive(5);
    gpio_put(PIN_EPD_RST, 1); sleep_ms_alive(20);
    wait_idle("reset");
    sleep_ms_alive(30);

    // Init sequence from Waveshare's EPD_7in3f.c
    CMD(0xAA, 0x49, 0x55, 0x20, 0x08, 0x09, 0x18);
    CMD(0x01, 0x3F, 0x00, 0x32, 0x2A, 0x0E, 0x2A);
    CMD(0x00, 0x5F, 0x69);
    CMD(0x03, 0x00, 0x54, 0x00, 0x44);
    CMD(0x05, 0x40, 0x1F, 0x1F, 0x2C);
    CMD(0x06, 0x6F, 0x1F, 0x1F, 0x22);
    CMD(0x08, 0x6F, 0x1F, 0x1F, 0x22);
    CMD(0x13, 0x00, 0x04);
    CMD(0x30, 0x3C);
    CMD(0x41, 0x00);
    CMD(0x50, 0x3F);
    CMD(0x60, 0x02, 0x00);
    CMD(0x61, 0x03, 0x20, 0x01, 0xE0);
    CMD(0x82, 0x1E);
    CMD(0x84, 0x00);
    CMD(0x86, 0x00);
    CMD(0xE3, 0x2F);
    CMD(0xE0, 0x00);
    CMD(0xE6, 0x00);

    cmd(0x10);   // start data transmission
}

void epd_write(const uint8_t *d, size_t n) {
    gpio_put(PIN_EPD_DC, 1);
    for (size_t i = 0; i < n; i++) {
        gpio_put(PIN_EPD_CS, 0);
        spi_write_blocking(EPD_SPI, &d[i], 1);
        gpio_put(PIN_EPD_CS, 1);
        if ((i & 4095) == 4095) keepalive();
    }
}

void epd_end(bool wait) {
    cmd(0x04); wait_idle("power on");          // POWER_ON
    CMD(0x12, 0x00);                           // DISPLAY_REFRESH
    refreshing = true;
    sleep_ms_alive(5);                         // let BUSY go low before anyone polls it
    if (wait) epd_finish();
}
