#include "usb.h"
#include <string.h>
#include "tusb.h"
#include "pico/unique_id.h"
#include "pico/usb_reset_interface.h"
#include "sd_card.h"
#include "hw_config.h"
#include "board.h"

// ---------------------------------------------------------------- descriptors

enum { ITF_CDC = 0, ITF_CDC_DATA, ITF_MSC, ITF_RESET, ITF_TOTAL };
enum { STR_LANG = 0, STR_MANUF, STR_PRODUCT, STR_SERIAL, STR_CDC, STR_MSC, STR_RESET };

#define EP_CDC_NOTIF 0x81
#define EP_CDC_OUT   0x02
#define EP_CDC_IN    0x82
#define EP_MSC_OUT   0x03
#define EP_MSC_IN    0x83

#define RESET_DESC_LEN 9
#define CONFIG_LEN (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN + TUD_MSC_DESC_LEN + RESET_DESC_LEN)

static const tusb_desc_device_t desc_device = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = 0x2E8A,      // Raspberry Pi
    .idProduct = 0x000A,     // Pico SDK CDC device
    .bcdDevice = 0x0201,
    .iManufacturer = STR_MANUF,
    .iProduct = STR_PRODUCT,
    .iSerialNumber = STR_SERIAL,
    .bNumConfigurations = 1,
};

static const uint8_t desc_config[CONFIG_LEN] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_TOTAL, 0, CONFIG_LEN, 0, 250),
    TUD_CDC_DESCRIPTOR(ITF_CDC, STR_CDC, EP_CDC_NOTIF, 8, EP_CDC_OUT, EP_CDC_IN, 64),
    TUD_MSC_DESCRIPTOR(ITF_MSC, STR_MSC, EP_MSC_OUT, EP_MSC_IN, 64),
    9, TUSB_DESC_INTERFACE, ITF_RESET, 0, 0, TUSB_CLASS_VENDOR_SPECIFIC,
       RESET_INTERFACE_SUBCLASS, RESET_INTERFACE_PROTOCOL, STR_RESET,
};

const uint8_t *tud_descriptor_device_cb(void) { return (const uint8_t *)&desc_device; }
const uint8_t *tud_descriptor_configuration_cb(uint8_t index) { (void)index; return desc_config; }

const uint16_t *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void)langid;
    static uint16_t buf[40];
    static char serial[2 * PICO_UNIQUE_BOARD_ID_SIZE_BYTES + 1];
    const char *s;
    switch (index) {
        case STR_LANG: buf[1] = 0x0409; buf[0] = (TUSB_DESC_STRING << 8) | 4; return buf;
        case STR_MANUF:   s = "Waveshare (custom firmware)"; break;
        case STR_PRODUCT: s = "PhotoPainter"; break;
        case STR_SERIAL:  pico_get_unique_board_id_string(serial, sizeof serial); s = serial; break;
        case STR_CDC:     s = "PhotoPainter Serial"; break;
        case STR_MSC:     s = "PhotoPainter SD Card"; break;
        case STR_RESET:   s = "Reset"; break;
        default: return NULL;
    }
    int n = strlen(s);
    if (n > 39) n = 39;
    for (int i = 0; i < n; i++) buf[1 + i] = (uint8_t)s[i];
    buf[0] = (TUSB_DESC_STRING << 8) | (2 * n + 2);
    return buf;
}

// ---------------------------------------------------------------- state

static volatile bool drive_on, drive_ejected;
static uint32_t last_io_ms;
static bool in_poll;

void usb_init(void) { tud_init(0); }

void usb_poll(void) {
    if (in_poll) return;
    in_poll = true;
    tud_task();
    in_poll = false;
}

bool usb_host_connected(void) { return tud_mounted(); }

void usb_drive_enable(void) {
    sd_card_t *sd = sd_get_by_num(0);
    sd_init_card(sd);
    drive_ejected = false;
    drive_on = true;
}
void usb_drive_disable(void) { drive_on = false; }
bool usb_drive_enabled(void) { return drive_on; }
bool usb_drive_ejected(void) { return drive_ejected; }
bool usb_drive_busy_recent(void) { return board_millis() - last_io_ms < 1000; }

// ---------------------------------------------------------------- MSC callbacks
// No printf in here: printf services USB itself and would re-enter tud_task().

void tud_msc_inquiry_cb(uint8_t lun, uint8_t vendor_id[8], uint8_t product_id[16], uint8_t product_rev[4]) {
    (void)lun;
    memcpy(vendor_id, "PhotoPtr", 8);
    memcpy(product_id, "Picture Frame   ", 16);
    memcpy(product_rev, "1.0 ", 4);
}

bool tud_msc_test_unit_ready_cb(uint8_t lun) {
    if (!drive_on) {
        tud_msc_set_sense(lun, SCSI_SENSE_NOT_READY, 0x3A, 0x00);   // medium not present
        return false;
    }
    return true;
}

void tud_msc_capacity_cb(uint8_t lun, uint32_t *block_count, uint16_t *block_size) {
    (void)lun;
    sd_card_t *sd = sd_get_by_num(0);
    *block_count = drive_on ? (uint32_t)sd->sectors : 0;
    *block_size = 512;
}

bool tud_msc_start_stop_cb(uint8_t lun, uint8_t power_condition, bool start, bool load_eject) {
    (void)lun; (void)power_condition;
    if (load_eject && !start) { drive_on = false; drive_ejected = true; }
    return true;
}

bool tud_msc_is_writable_cb(uint8_t lun) { (void)lun; return true; }

int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize) {
    (void)lun;
    if (!drive_on || (offset % 512) || (bufsize % 512)) return -1;
    last_io_ms = board_millis();
    if (sd_read_blocks(sd_get_by_num(0), buffer, lba + offset / 512, bufsize / 512) != 0) return -1;
    return (int32_t)bufsize;
}

int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bufsize) {
    (void)lun;
    if (!drive_on || (offset % 512) || (bufsize % 512)) return -1;
    last_io_ms = board_millis();
    if (sd_write_blocks(sd_get_by_num(0), buffer, lba + offset / 512, bufsize / 512) != 0) return -1;
    return (int32_t)bufsize;
}

int32_t tud_msc_scsi_cb(uint8_t lun, uint8_t const scsi_cmd[16], void *buffer, uint16_t bufsize) {
    (void)buffer; (void)bufsize;
    switch (scsi_cmd[0]) {
        case 0x1E:   // PREVENT ALLOW MEDIUM REMOVAL
        case 0x35:   // SYNCHRONIZE CACHE (writes are already synchronous)
            return 0;
        default:
            tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x20, 0x00);
            return -1;
    }
}
