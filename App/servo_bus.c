#include "servo_bus.h"

#define LOBOT_FRAME_HEADER              0x55U
#define LOBOT_CMD_OR_MOTOR_MODE_WRITE   29U
#define LOBOT_MOTOR_MODE                1U
#define LOBOT_SET_MODE_FRAME_LEN        10U
#define LOBOT_TX_POST_DELAY_MS          1U

static UART_HandleTypeDef *s_huart;
static uint8_t s_servo_id = 1U;

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

static int servo_bus_transmit(const uint8_t *frame, uint16_t len)
{
    if (s_huart == NULL) {
        return -1;
    }

    if (HAL_UART_Transmit(s_huart, (uint8_t *)frame, len, 100U) != HAL_OK) {
        return -1;
    }

    /* Half-duplex bus: allow line turnaround before any future RX. */
    HAL_Delay(LOBOT_TX_POST_DELAY_MS);
    return 0;
}

void servo_bus_init(UART_HandleTypeDef *huart, uint8_t servo_id)
{
    s_huart = huart;
    s_servo_id = servo_id;
}

int servo_bus_set_motor_speed(int16_t speed)
{
    uint8_t buf[LOBOT_SET_MODE_FRAME_LEN];
    const int16_t clamped = clamp_speed(speed);
    const uint16_t speed_u = (uint16_t)clamped;

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

int servo_bus_motor_stop(void)
{
    return servo_bus_set_motor_speed(0);
}
