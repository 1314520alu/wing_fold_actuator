#pragma once

#include <stdbool.h>

void led_status_init(bool calibrated);
void led_status_set_calibrated(bool calibrated);
void led_status_poll(void);
