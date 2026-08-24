#pragma once

#include <stdint.h>

#define APP_PWM_TIMEOUT_MS  150U

void app_init(void);
void app_tick(uint32_t now_ms);
