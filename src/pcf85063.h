// PCF85063 real-time clock: used only as a wake-up alarm timer.
#pragma once
#include <stdbool.h>
#include <stdint.h>

bool pcf_init(void);                       // software-reset the chip and start the oscillator
bool pcf_alarm_fired(void);                // alarm flag set (we were woken by the timer)
void pcf_arm_minutes(uint32_t minutes);    // alarm N minutes from now (1 .. 28 days); 0 disables
void pcf_disarm(void);
