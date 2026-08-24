#pragma once

#include <stdint.h>

#include "main.h"

/* HTD-85H / Lobot bus servo motor-mode speed (vendor typical ±500). */
#define SERVO_BUS_SPEED_MAX 500

void servo_bus_init(UART_HandleTypeDef *huart, uint8_t servo_id);
int servo_bus_set_motor_speed(int16_t speed);
int servo_bus_motor_stop(void);
