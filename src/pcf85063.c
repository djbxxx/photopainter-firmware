#include "pcf85063.h"
#include <stdio.h>
#include "board.h"

#define ADDR 0x51
#define REG_CTRL1 0x00
#define REG_CTRL2 0x01
#define REG_SEC   0x04
#define REG_ASEC  0x0B
#define CTRL2_AIE 0x80
#define CTRL2_AF  0x40
#define AEN_OFF   0x80   // set in an alarm register to ignore that field

static bool wr(uint8_t reg, const uint8_t *d, int n) {
    uint8_t buf[8];
    buf[0] = reg;
    for (int i = 0; i < n; i++) buf[1 + i] = d[i];
    return i2c_write_blocking(RTC_I2C, ADDR, buf, n + 1, false) == n + 1;
}
static bool wr1(uint8_t reg, uint8_t v) { return wr(reg, &v, 1); }
static int rd1(uint8_t reg) {
    uint8_t v;
    if (i2c_write_blocking(RTC_I2C, ADDR, &reg, 1, true) != 1) return -1;
    if (i2c_read_blocking(RTC_I2C, ADDR, &v, 1, false) != 1) return -1;
    return v;
}
static uint8_t bcd(int v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }

static bool woke_by_alarm;

bool pcf_init(void) {
    int c2 = rd1(REG_CTRL2);
    if (c2 < 0) { printf("rtc: not responding\n"); return false; }
    woke_by_alarm = (c2 & CTRL2_AF) != 0;
    wr1(REG_CTRL1, 0x58);               // software reset (clears alarm + flag)
    sleep_ms(5);
    int s = rd1(REG_SEC);
    if (s >= 0 && (s & 0x80)) wr1(REG_SEC, s & 0x7F);   // clear oscillator-stop flag
    return true;
}

bool pcf_alarm_fired(void) { return woke_by_alarm; }

void pcf_disarm(void) {
    const uint8_t off[5] = {AEN_OFF, AEN_OFF, AEN_OFF, AEN_OFF, AEN_OFF};
    wr(REG_ASEC, off, 5);
    wr1(REG_CTRL2, 0x00);
}

void pcf_arm_minutes(uint32_t minutes) {
    if (minutes == 0) { pcf_disarm(); return; }
    if (minutes > 28u * 1440u) minutes = 28u * 1440u;
    // Restart the clock at day 1, 00:00:00 and alarm when day/hour/minute match.
    const uint8_t t[7] = {0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00};   // s m h day wday month year
    wr(REG_SEC, t, 7);
    int d = minutes / 1440, h = (minutes % 1440) / 60, m = minutes % 60;
    const uint8_t a[5] = {bcd(0), bcd(m), bcd(h), bcd(1 + d), AEN_OFF};   // sec min hour day weekday
    wr(REG_ASEC, a, 5);
    wr1(REG_CTRL2, CTRL2_AIE);   // enable interrupt, clear flag
    printf("rtc: alarm in %lu min (day %d %02d:%02d)\n", (unsigned long)minutes, 1 + d, h, m);
}
