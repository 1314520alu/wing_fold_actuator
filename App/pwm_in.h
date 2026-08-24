#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "main.h"

void pwm_in_init(TIM_HandleTypeDef *htim);
bool pwm_in_get_pulse_us(uint16_t *out_us);
uint32_t pwm_in_last_edge_ms(void);
