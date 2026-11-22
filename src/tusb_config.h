#pragma once

// The SDK's reset interface takes its defaults from here (its own tusb_config.h does the same).
#include "pico/stdio_usb.h"

#define CFG_TUSB_RHPORT0_MODE   OPT_MODE_DEVICE
#define CFG_TUD_ENDPOINT0_SIZE  64

#define CFG_TUD_CDC             1
#define CFG_TUD_MSC             1
#define CFG_TUD_HID             0
#define CFG_TUD_MIDI            0
#define CFG_TUD_VENDOR          0

#define CFG_TUD_CDC_RX_BUFSIZE  64
#define CFG_TUD_CDC_TX_BUFSIZE  1024
#define CFG_TUD_CDC_EP_BUFSIZE  64

// Bigger buffer = fewer, larger SD transfers when copying files over USB.
#define CFG_TUD_MSC_EP_BUFSIZE  4096
