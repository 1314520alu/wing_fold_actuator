#pragma once

#include <stdbool.h>

#include "main.h"
#include "nvm.h"

/* 0 = run until hold (bench / cal). Non-zero = auto-hold after that many ms. */
#define CLI_MANUAL_TIMEOUT_MS  0U

void cli_init(UART_HandleTypeDef *huart);
void cli_poll(void);
/* Called from USART3_IRQHandler — keep tiny. */
void cli_uart_rx_irq_byte(uint8_t byte);

/* Task 7 can consume the calibrated RAM copy without rereading Flash. */
const nvm_blob_t *cli_get_params(void);
bool cli_is_calibrated(void);
bool cli_manual_override_active(uint32_t now_ms);
int16_t cli_manual_speed(void);
void cli_telem_tick(uint32_t now_ms);
