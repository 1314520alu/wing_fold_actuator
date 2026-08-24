#include "encoder.h"

#include <stddef.h>

#define ENCODER_SLAVE_ID            1U
#define ENCODER_READ_HOLDING_REGS   0x03U
#define ENCODER_POSITION_REG        0x0002U
#define ENCODER_POSITION_REG_COUNT  2U
#define ENCODER_SINGLE_TURN_COUNTS  1024U
#define ENCODER_REQUEST_SIZE        8U
#define ENCODER_RESPONSE_SIZE       9U
#define ENCODER_UART_TIMEOUT_MS     100U

static UART_HandleTypeDef *s_huart;
static uint8_t s_fail_streak;

static uint16_t modbus_crc16(const uint8_t *data, uint16_t length)
{
    uint16_t crc = 0xFFFFU;

    for (uint16_t i = 0U; i < length; ++i) {
        crc ^= data[i];
        for (uint8_t bit = 0U; bit < 8U; ++bit) {
            if ((crc & 1U) != 0U) {
                crc = (uint16_t)((crc >> 1U) ^ 0xA001U);
            } else {
                crc = (uint16_t)(crc >> 1U);
            }
        }
    }
    return crc;
}

static bool read_failed(void)
{
    if (s_fail_streak < UINT8_MAX) {
        ++s_fail_streak;
    }
    return false;
}

void encoder_init(UART_HandleTypeDef *huart)
{
    s_huart = huart;
    s_fail_streak = 0U;
}

bool encoder_read_count(int32_t *out_count)
{
    uint8_t request[ENCODER_REQUEST_SIZE] = {
        ENCODER_SLAVE_ID,
        ENCODER_READ_HOLDING_REGS,
        (uint8_t)(ENCODER_POSITION_REG >> 8U),
        (uint8_t)ENCODER_POSITION_REG,
        (uint8_t)(ENCODER_POSITION_REG_COUNT >> 8U),
        (uint8_t)ENCODER_POSITION_REG_COUNT,
        0U,
        0U
    };
    uint8_t response[ENCODER_RESPONSE_SIZE];
    uint16_t crc;
    uint16_t received_crc;
    uint16_t turns;
    uint16_t single;

    if ((s_huart == NULL) || (out_count == NULL)) {
        return read_failed();
    }

    crc = modbus_crc16(request, ENCODER_REQUEST_SIZE - 2U);
    request[6] = (uint8_t)crc;
    request[7] = (uint8_t)(crc >> 8U);

    if (HAL_UART_Transmit(s_huart, request, ENCODER_REQUEST_SIZE,
                          ENCODER_UART_TIMEOUT_MS) != HAL_OK) {
        return read_failed();
    }
    if (HAL_UART_Receive(s_huart, response, ENCODER_RESPONSE_SIZE,
                         ENCODER_UART_TIMEOUT_MS) != HAL_OK) {
        return read_failed();
    }

    crc = modbus_crc16(response, ENCODER_RESPONSE_SIZE - 2U);
    received_crc = (uint16_t)response[7]
                 | (uint16_t)((uint16_t)response[8] << 8U);
    if ((crc != received_crc)
        || (response[0] != ENCODER_SLAVE_ID)
        || (response[1] != ENCODER_READ_HOLDING_REGS)
        || (response[2] != 4U)) {
        return read_failed();
    }

    turns = (uint16_t)((uint16_t)response[3] << 8U) | response[4];
    single = (uint16_t)((uint16_t)response[5] << 8U) | response[6];
    if (single >= ENCODER_SINGLE_TURN_COUNTS) {
        return read_failed();
    }

    *out_count = (int32_t)turns * (int32_t)ENCODER_SINGLE_TURN_COUNTS
               + (int32_t)single;
    s_fail_streak = 0U;
    return true;
}

uint8_t encoder_fail_streak(void)
{
    return s_fail_streak;
}
