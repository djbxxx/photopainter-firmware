// SD card wiring for the no-OS-FatFS SPI driver (fixed version of Waveshare's file,
// which accidentally overwrote the MOSI/SCK pin numbers with drive-strength values).
#include <string.h>
#include "hw_config.h"
#include "ff.h"
#include "diskio.h"
#include "board.h"

void spi_dma_isr();

static spi_t spis[] = {{
    .hw_inst = SD_SPI,
    .miso_gpio = PIN_SD_MISO,
    .mosi_gpio = PIN_SD_MOSI,
    .sck_gpio = PIN_SD_CLK,
    .set_drive_strength = true,
    .mosi_gpio_drive_strength = GPIO_DRIVE_STRENGTH_2MA,
    .sck_gpio_drive_strength = GPIO_DRIVE_STRENGTH_2MA,
    .baud_rate = 12500 * 1000,
    .dma_isr = spi_dma_isr,
}};

static sd_card_t sd_cards[] = {{
    .pcName = "0:",
    .spi = &spis[0],
    .ss_gpio = PIN_SD_CS,
    .use_card_detect = false,
    .set_drive_strength = true,
    .ss_gpio_drive_strength = GPIO_DRIVE_STRENGTH_2MA,
    .m_Status = STA_NOINIT,
}};

void spi_dma_isr() { spi_irq_handler(&spis[0]); }

size_t sd_get_num() { return count_of(sd_cards); }
sd_card_t *sd_get_by_num(size_t num) { return num < sd_get_num() ? &sd_cards[num] : NULL; }
size_t spi_get_num() { return count_of(spis); }
spi_t *spi_get_by_num(size_t num) { return num < spi_get_num() ? &spis[num] : NULL; }
