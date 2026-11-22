#include "board.h"
#include "hardware/adc.h"
#include "hardware/watchdog.h"
#include "usb.h"

// Set the level before enabling the output, so a pin never glitches to the wrong level.
static void out(uint pin, bool v) { gpio_init(pin); gpio_put(pin, v); gpio_set_dir(pin, GPIO_OUT); }
static void in_pullup(uint pin)   { gpio_init(pin); gpio_set_dir(pin, GPIO_IN); gpio_pull_up(pin); }

void board_init(void) {
    // Hold the battery latch first so the board stays on after the wake pulse ends.
    out(PIN_BAT_OFF, 1);
    out(PIN_LED_ACT, 0);
    out(PIN_LED_PWR, 0);
    out(PIN_EPD_PWR, 0);

    spi_init(EPD_SPI, 8 * 1000 * 1000);
    gpio_set_function(PIN_EPD_CLK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_EPD_MOSI, GPIO_FUNC_SPI);
    out(PIN_EPD_CS, 1);
    out(PIN_EPD_DC, 0);
    out(PIN_EPD_RST, 1);
    gpio_init(PIN_EPD_BUSY);
    gpio_set_dir(PIN_EPD_BUSY, GPIO_IN);

    out(PIN_SD_CS, 1);

    i2c_init(RTC_I2C, 100 * 1000);
    gpio_set_function(PIN_RTC_SDA, GPIO_FUNC_I2C);
    gpio_set_function(PIN_RTC_SCL, GPIO_FUNC_I2C);
    in_pullup(PIN_RTC_INT);

    in_pullup(PIN_KEY);
    in_pullup(PIN_CHARGE);
    gpio_init(PIN_VBUS);
    gpio_set_dir(PIN_VBUS, GPIO_IN);

    adc_init();
    adc_gpio_init(PIN_VBAT_ADC);
    adc_select_input(VBAT_ADC_INPUT);
}

float board_vbat(void) {
    uint32_t sum = 0;
    for (int i = 0; i < 16; i++) sum += adc_read();
    return (sum / 16.0f) * 3.3f / 4096.0f * 3.0f;
}

bool board_usb_power(void) { return gpio_get(PIN_VBUS); }
bool board_charging(void)  { return !gpio_get(PIN_CHARGE); }
bool board_key_down(void)  { return !gpio_get(PIN_KEY); }
void board_led_act(bool on) { gpio_put(PIN_LED_ACT, on); }
void board_led_pwr(bool on) { gpio_put(PIN_LED_PWR, on); }
void board_epd_power(bool on) { gpio_put(PIN_EPD_PWR, on); }
void board_power_off(void) { gpio_put(PIN_BAT_OFF, 0); sleep_ms(50); }
uint32_t board_millis(void) { return to_ms_since_boot(get_absolute_time()); }

void keepalive(void) {
    watchdog_update();
    usb_poll();
}

void sleep_ms_alive(uint32_t ms) {
    uint32_t end = board_millis() + ms;
    while ((int32_t)(end - board_millis()) > 0) { keepalive(); sleep_ms(1); }
}
