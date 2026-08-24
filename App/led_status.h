#pragma once

#include <stdbool.h>

void led_status_init(bool calibrated);
void led_status_set_calibrated(bool calibrated);
void led_status_run(void);
void led_status_hold(void);
void led_status_fault(void);
void led_status_poll(void);
