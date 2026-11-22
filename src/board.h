// Board support for the Waveshare PhotoPainter (RP2040 + 7.3" 7-color ACeP panel).
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/i2c.h"

// e-Paper (SPI1)
#define EPD_SPI        spi1
#define PIN_EPD_CLK    10
#define PIN_EPD_MOSI   11
#define PIN_EPD_CS     9
#define PIN_EPD_DC     8
#define PIN_EPD_RST    12
#define PIN_EPD_BUSY   13
#define PIN_EPD_PWR    16

// microSD (SPI0) - pins are used by hw_config.c
#define SD_SPI         spi0
#define PIN_SD_CLK     2
#define PIN_SD_MOSI    3
#define PIN_SD_MISO    4
#define PIN_SD_CS      5

// RTC PCF85063 (I2C1)
#define RTC_I2C        i2c1
#define PIN_RTC_SDA    14
#define PIN_RTC_SCL    15
#define PIN_RTC_INT    6

// Power / buttons / LEDs
#define PIN_CHARGE     17   // low while charging
#define PIN_BAT_OFF    18   // drive low to cut the battery power latch
#define PIN_KEY        19   // NEXT button, low when pressed
#define PIN_VBUS       24   // high when USB power present
#define PIN_LED_ACT    25   // activity LED
#define PIN_LED_PWR    26   // power/charge LED
#define PIN_VBAT_ADC   29   // battery voltage / 3
#define VBAT_ADC_INPUT 3

void     board_init(void);
float    board_vbat(void);
bool     board_usb_power(void);
bool     board_charging(void);
bool     board_key_down(void);
void     board_led_act(bool on);
void     board_led_pwr(bool on);
void     board_epd_power(bool on);
// Cuts the battery latch. On battery the board dies here; on USB power it returns.
void     board_power_off(void);
uint32_t board_millis(void);

// Called from every long-running loop: feeds the watchdog and services USB.
void keepalive(void);
void sleep_ms_alive(uint32_t ms);
