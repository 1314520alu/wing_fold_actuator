#include "servo_bus.h"

#include <stdbool.h>

#define LOBOT_FRAME_HEADER              0x55U
#define LOBOT_CMD_OR_MOTOR_MODE_WRITE   29U
#define LOBOT_MOTOR_MODE                1U
#define LOBOT_SET_MODE_FRAME_LEN        10U
#define LOBOT_TX_POST_DELAY_MS          0U

/* Closed-loop slew per ~10 ms tick (0↔1000 ≈ 250 ms). */
#ifndef SERVO_BUS_RAMP_STEP
#define SERVO_BUS_RAMP_STEP             40
#endif
#ifndef SERVO_BUS_KEEPALIVE_MS
#define SERVO_BUS_KEEPALIVE_MS          80U
#endif

static UART_HandleTypeDef *s_huart;
static uint8_t s_servo_id = 1U;
static int16_t s_speed_target;
static int16_t s_speed_out;
static bool s_out_valid;
static uint32_t s_last_tx_ms;

static uint8_t lobot_checksum(const uint8_t *buf)
{
    uint16_t sum = 0U;
    const uint8_t payload_len = buf[3];

    for (uint8_t i = 2U; i < payload_len + 2U; i++) {
        sum = (uint16_t)(sum + buf[i]);
    }

    return (uint8_t)(~sum);
}

static int16_t clamp_speed(int16_t speed)
{
    if (speed > SERVO_BUS_SPEED_MAX) {
        return SERVO_BUS_SPEED_MAX;
    }
    if (speed < -SERVO_BUS_SPEED_MAX) {
        return -SERVO_BUS_SPEED_MAX;
    }
    return speed;
}

static int8_t speed_sign(int16_t speed)
{
    if (speed > 0) {
        return 1;
    }
    if (speed < 0) {
        return -1;
    }
    return 0;
}

static int servo_bus_transmit(const uint8_t *frame, uint16_t len)
{
    if (s_huart == NULL) {
        return -1;
    }

    if (HAL_UART_Transmit(s_huart, (uint8_t *)frame, len, 20U) != HAL_OK) {
        return -1;
    }

#if LOBOT_TX_POST_DELAY_MS > 0U
    HAL_Delay(LOBOT_TX_POST_DELAY_MS);
#endif
    s_last_tx_ms = HAL_GetTick();
    return 0;
}

static int transmit_speed(int16_t speed)
{
    uint8_t buf[LOBOT_SET_MODE_FRAME_LEN];
    const uint16_t speed_u = (uint16_t)speed;

    buf[0] = LOBOT_FRAME_HEADER;
    buf[1] = LOBOT_FRAME_HEADER;
    buf[2] = s_servo_id;
    buf[3] = 7U;
    buf[4] = LOBOT_CMD_OR_MOTOR_MODE_WRITE;
    buf[5] = LOBOT_MOTOR_MODE;
    buf[6] = 0U;
    buf[7] = (uint8_t)(speed_u & 0xFFU);
    buf[8] = (uint8_t)((speed_u >> 8) & 0xFFU);
    buf[9] = lobot_checksum(buf);

    return servo_bus_transmit(buf, LOBOT_SET_MODE_FRAME_LEN);
}

void servo_bus_init(UART_HandleTypeDef *huart, uint8_t servo_id)
{
    s_huart = huart;
    s_servo_id = servo_id;
    s_speed_target = 0;
    s_speed_out = 0;
    s_out_valid = false;
    s_last_tx_ms = 0U;
}

int servo_bus_set_motor_speed(int16_t speed)
{
    if (s_huart == NULL) {
        return -1;
    }
    s_speed_target = clamp_speed(speed);
    return 0;
}

int servo_bus_set_motor_speed_immediate(int16_t speed)
{
    const int16_t clamped = clamp_speed(speed);

    if (s_huart == NULL) {
        return -1;
    }

    s_speed_target = clamped;

    /* Opposite non-zero signs: ramp through zero in ramp_update(). */
    if ((speed_sign(s_speed_out) != 0)
        && (speed_sign(clamped) != 0)
        && (speed_sign(s_speed_out) != speed_sign(clamped))) {
        return 0;
    }

    s_speed_out = clamped;
    s_out_valid = true;
    return transmit_speed(s_speed_out);
}

int servo_bus_motor_stop(void)
{
    s_speed_target = 0;
    s_speed_out = 0;
    s_out_valid = true;
    return transmit_speed(0);
}

int servo_bus_ramp_update(void)
{
    int16_t next = s_speed_out;
    const int16_t step = (int16_t)SERVO_BUS_RAMP_STEP;
    const uint32_t now_ms = HAL_GetTick();

    if (s_huart == NULL) {
        return -1;
    }

    if (s_speed_out < s_speed_target) {
        next = (int16_t)(s_speed_out + step);
        if (next > s_speed_target) {
            next = s_speed_target;
        }
    } else if (s_speed_out > s_speed_target) {
        next = (int16_t)(s_speed_out - step);
        if (next < s_speed_target) {
            next = s_speed_target;
        }
    } else if (s_out_valid) {
        if ((s_speed_out != 0)
            && ((uint32_t)(now_ms - s_last_tx_ms) >= SERVO_BUS_KEEPALIVE_MS)) {
            if (transmit_speed(s_speed_out) != 0) {
                return -1;
            }
            return 1;
        }
        return 0;
    }

    s_speed_out = next;
    s_out_valid = true;
    if (transmit_speed(s_speed_out) != 0) {
        return -1;
    }
    return 1;
}

int16_t servo_bus_get_output_speed(void)
{
    return s_speed_out;
}
