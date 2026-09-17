#pragma once

#include <stdint.h>

#include "main.h"

/* Host speed units (-1000..1000). HTD maps host 100 → Lobot ±1000;
 * AK70 maps host 100 → 2 shaft rev/s. See servo_bus_*.c. */
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

/* AK70 link probe. HTD returns -1. Called from status, not the 10 ms loop. */
typedef struct {
    int16_t vin_x10;
    int32_t rpm;
    int16_t mos_x10;
    uint8_t fault;
    uint8_t has_values;
} servo_bus_feedback_t;

int servo_bus_read_feedback(servo_bus_feedback_t *out);
