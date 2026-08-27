#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "main.h"

void pwm_in_init(TIM_HandleTypeDef *htim);
void pwm_in_set_cmd_range(uint16_t min_us, uint16_t max_us);
bool pwm_in_get_pulse_us(uint16_t *out_us);
bool pwm_in_get_fresh_pulse_us(uint32_t now_ms, uint32_t max_age_ms,
                               uint16_t *out_us);
uint32_t pwm_in_last_edge_ms(void);
uint32_t pwm_in_irq_count(void);
uint32_t pwm_in_last_raw_us(void);
