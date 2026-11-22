// USB: CDC serial (logs), mass storage (the SD card as a drive), and the
// Raspberry Pi reset interface so picotool can reboot into BOOTSEL.
#pragma once
#include <stdbool.h>

void usb_init(void);
void usb_poll(void);
bool usb_host_connected(void);   // enumerated by a computer (not just a charger)

// Drive mode: the computer gets raw access to the SD card. The firmware must not
// touch the filesystem while it is on.
void usb_drive_enable(void);
void usb_drive_disable(void);
bool usb_drive_enabled(void);
bool usb_drive_ejected(void);    // computer ejected the drive since it was enabled
bool usb_drive_busy_recent(void);// a read/write happened in the last second
