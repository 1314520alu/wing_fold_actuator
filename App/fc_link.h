#pragma once

#include "main.h"

#include <stdint.h>

void fc_link_init(UART_HandleTypeDef *huart);
void fc_link_tick(uint32_t now_ms);

