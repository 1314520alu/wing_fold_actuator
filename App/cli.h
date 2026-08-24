#pragma once

#include <stdbool.h>

#include "main.h"
#include "nvm.h"

void cli_init(UART_HandleTypeDef *huart);
void cli_poll(void);

/* Task 7 can consume the calibrated RAM copy without rereading Flash. */
const nvm_blob_t *cli_get_params(void);
bool cli_is_calibrated(void);
