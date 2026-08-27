#include "encoder.h"

#include <stddef.h>

#define ENCODER_SLAVE_ID            1U
#define ENCODER_READ_HOLDING_REGS   0x03U
#define ENCODER_WRITE_SINGLE_REG    0x06U
#define ENCODER_POSITION_REG        0x0002U
#define ENCODER_POSITION_REG_COUNT  2U
#define ENCODER_BAUD_REG            0x0005U
#define ENCODER_BAUD_115200_CODE    0x0004U
#define ENCODER_SINGLE_TURN_COUNTS  1024U
#define ENCODER_REQUEST_SIZE        8U
#define ENCODER_RESPONSE_SIZE       9U
#define ENCODER_WRITE_RESPONSE_SIZE 8U
#define ENCODER_UART_TIMEOUT_MS     10U
#define ENCODER_FAIL_BACKOFF_MS     100U
#define ENCODER_BAUD_DEFAULT        9600U
#define ENCODER_BAUD_FAST           115200U

static UART_HandleTypeDef *s_huart;
static uint8_t s_fail_streak;
static uint32_t s_next_attempt_ms;

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

static void uart_flush_rx(void)
{
    uint8_t dump;
    uint32_t guard = 0U;

    if ((s_huart == NULL) || (s_huart->Instance == NULL)) {
        return;
    }
    while ((__HAL_UART_GET_FLAG(s_huart, UART_FLAG_RXNE) != RESET)
           && (guard < 64U)) {
        dump = (uint8_t)(s_huart->Instance->DR & 0xFFU);
        (void)dump;
        ++guard;
    }
}

static bool uart_set_baud(uint32_t baud)
{
    if (s_huart == NULL) {
        return false;
    }
    if (HAL_UART_DeInit(s_huart) != HAL_OK) {
        return false;
    }
    s_huart->Init.BaudRate = baud;
    return HAL_UART_Init(s_huart) == HAL_OK;
}

/* Factory default is 9600. Write holding reg 0x0005 = 4 → 115200 (persistent). */
static void encoder_write_baud_115200_at_9600(void)
{
    uint8_t request[ENCODER_REQUEST_SIZE] = {
        ENCODER_SLAVE_ID,
        ENCODER_WRITE_SINGLE_REG,
        (uint8_t)(ENCODER_BAUD_REG >> 8U),
        (uint8_t)ENCODER_BAUD_REG,
        (uint8_t)(ENCODER_BAUD_115200_CODE >> 8U),
        (uint8_t)ENCODER_BAUD_115200_CODE,
        0U,
        0U
    };
    uint8_t response[ENCODER_WRITE_RESPONSE_SIZE];
    uint16_t crc;

    crc = modbus_crc16(request, ENCODER_REQUEST_SIZE - 2U);
    request[6] = (uint8_t)crc;
    request[7] = (uint8_t)(crc >> 8U);

    uart_flush_rx();
    (void)HAL_UART_Transmit(s_huart, request, ENCODER_REQUEST_SIZE,
                            ENCODER_UART_TIMEOUT_MS);
    /* Encoder may ACK then switch baud; ignore RX errors. */
    (void)HAL_UART_Receive(s_huart, response, ENCODER_WRITE_RESPONSE_SIZE,
                           ENCODER_UART_TIMEOUT_MS);
}

static bool encoder_probe_once(void)
{
    int32_t count;

    s_fail_streak = 0U;
    s_next_attempt_ms = 0U;
    return encoder_read_count(&count);
}

static void encoder_negotiate_fast_baud(void)
{
    if (s_huart == NULL) {
        return;
    }

    /* Boot MX leaves USART2 at 9600. */
    (void)uart_set_baud(ENCODER_BAUD_DEFAULT);
    HAL_Delay(2);
    encoder_write_baud_115200_at_9600();
    HAL_Delay(20);

    if (!uart_set_baud(ENCODER_BAUD_FAST)) {
        return;
    }
    HAL_Delay(5);
    if (encoder_probe_once()) {
        return;
    }

    /* Encoder may still be at 9600 if write failed. */
    if (!uart_set_baud(ENCODER_BAUD_DEFAULT)) {
        return;
    }
    HAL_Delay(5);
    if (encoder_probe_once()) {
        return;
    }

    /* Prefer fast baud for subsequent retries after cabling settles. */
    (void)uart_set_baud(ENCODER_BAUD_FAST);
}

void encoder_init(UART_HandleTypeDef *huart)
{
    s_huart = huart;
    s_fail_streak = 0U;
    s_next_attempt_ms = 0U;
    encoder_negotiate_fast_baud();
    s_fail_streak = 0U;
    s_next_attempt_ms = 0U;
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
    const uint32_t now_ms = HAL_GetTick();

    if ((s_huart == NULL) || (out_count == NULL)) {
        return read_failed();
    }

    if ((s_fail_streak > 0U) && ((int32_t)(now_ms - s_next_attempt_ms) < 0)) {
        return false;
    }

    crc = modbus_crc16(request, ENCODER_REQUEST_SIZE - 2U);
    request[6] = (uint8_t)crc;
    request[7] = (uint8_t)(crc >> 8U);

    uart_flush_rx();
    if (HAL_UART_Transmit(s_huart, request, ENCODER_REQUEST_SIZE,
                          ENCODER_UART_TIMEOUT_MS) != HAL_OK) {
        s_next_attempt_ms = now_ms + ENCODER_FAIL_BACKOFF_MS;
        return read_failed();
    }
    if (HAL_UART_Receive(s_huart, response, ENCODER_RESPONSE_SIZE,
                         ENCODER_UART_TIMEOUT_MS) != HAL_OK) {
        s_next_attempt_ms = now_ms + ENCODER_FAIL_BACKOFF_MS;
        return read_failed();
    }

    crc = modbus_crc16(response, ENCODER_RESPONSE_SIZE - 2U);
    received_crc = (uint16_t)response[7]
                 | (uint16_t)((uint16_t)response[8] << 8U);
    if ((crc != received_crc)
        || (response[0] != ENCODER_SLAVE_ID)
        || (response[1] != ENCODER_READ_HOLDING_REGS)
        || (response[2] != 4U)) {
        s_next_attempt_ms = now_ms + ENCODER_FAIL_BACKOFF_MS;
        return read_failed();
    }

    turns = (uint16_t)((uint16_t)response[3] << 8U) | response[4];
    single = (uint16_t)((uint16_t)response[5] << 8U) | response[6];
    if (single >= ENCODER_SINGLE_TURN_COUNTS) {
        s_next_attempt_ms = now_ms + ENCODER_FAIL_BACKOFF_MS;
        return read_failed();
    }

    *out_count = (int32_t)turns * (int32_t)ENCODER_SINGLE_TURN_COUNTS
               + (int32_t)single;
    s_fail_streak = 0U;
    s_next_attempt_ms = now_ms;
    return true;
}

uint8_t encoder_fail_streak(void)
{
    return s_fail_streak;
}
