#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "main.h"

void encoder_init(UART_HandleTypeDef *huart);

/* Reads turns (0x0002) and single-turn position (0x0003) in one FC 0x03
 * transaction. On success, writes turns * 1024 + single to out_count. */
bool encoder_read_count(int32_t *out_count);

uint8_t encoder_fail_streak(void);
