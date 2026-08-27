#pragma once

#include <stdint.h>

#include "main.h"

/* HTD-85H motor mode: manual says speed -1000..1000. */
#define SERVO_BUS_SPEED_MAX 1000

void servo_bus_init(UART_HandleTypeDef *huart, uint8_t servo_id);

/* Closed-loop: set target only; slewed in servo_bus_ramp_update(). */
int servo_bus_set_motor_speed(int16_t speed);

/* CLI jog: apply immediately (soft-reverse still ramps via opposite target). */
int servo_bus_set_motor_speed_immediate(int16_t speed);

/* Immediate zero (hold / fault). */
int servo_bus_motor_stop(void);

/* Call every control tick (~10 ms). */
int servo_bus_ramp_update(void);

int16_t servo_bus_get_output_speed(void);
