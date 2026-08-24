#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../../App/encoder.h"

static UART_HandleTypeDef uart;
static uint8_t transmitted[8];
static uint8_t response[9];
static HAL_StatusTypeDef tx_status;
static HAL_StatusTypeDef rx_status;

static uint16_t modbus_crc16(const uint8_t *data, uint16_t length)
{
    uint16_t crc = 0xFFFFU;

    for (uint16_t i = 0U; i < length; ++i) {
        crc ^= data[i];
        for (uint8_t bit = 0U; bit < 8U; ++bit) {
            crc = (crc & 1U) != 0U ? (uint16_t)((crc >> 1U) ^ 0xA001U)
                                   : (uint16_t)(crc >> 1U);
        }
    }
    return crc;
}

static void set_response(uint8_t slave, uint16_t turns, uint16_t single)
{
    response[0] = slave;
    response[1] = 0x03U;
    response[2] = 0x04U;
    response[3] = (uint8_t)(turns >> 8U);
    response[4] = (uint8_t)turns;
    response[5] = (uint8_t)(single >> 8U);
    response[6] = (uint8_t)single;
    const uint16_t crc = modbus_crc16(response, 7U);
    response[7] = (uint8_t)crc;
    response[8] = (uint8_t)(crc >> 8U);
}

static void reset_fixture(void)
{
    memset(transmitted, 0, sizeof(transmitted));
    tx_status = HAL_OK;
    rx_status = HAL_OK;
    set_response(1U, 2U, 513U);
    encoder_init(&uart);
}

HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef *huart,
                                    uint8_t *data,
                                    uint16_t size,
                                    uint32_t timeout)
{
    (void)timeout;
    assert(huart == &uart);
    assert(size == sizeof(transmitted));
    memcpy(transmitted, data, size);
    return tx_status;
}

HAL_StatusTypeDef HAL_UART_Receive(UART_HandleTypeDef *huart,
                                   uint8_t *data,
                                   uint16_t size,
                                   uint32_t timeout)
{
    (void)timeout;
    assert(huart == &uart);
    assert(size == sizeof(response));
    memcpy(data, response, size);
    return rx_status;
}

static void test_reads_two_registers_and_combines_count(void)
{
    static const uint8_t expected_request[8] =
        {0x01U, 0x03U, 0x00U, 0x02U, 0x00U, 0x02U, 0x65U, 0xCBU};
    int32_t count = -1;

    reset_fixture();
    assert(encoder_read_count(&count));
    assert(count == 2561);
    assert(memcmp(transmitted, expected_request, sizeof(expected_request)) == 0);
    assert(encoder_fail_streak() == 0U);
}

static void test_rejects_bad_crc_and_tracks_failures(void)
{
    int32_t count = 123;

    reset_fixture();
    response[8] ^= 0x01U;
    assert(!encoder_read_count(&count));
    assert(count == 123);
    assert(encoder_fail_streak() == 1U);

    set_response(1U, 4U, 7U);
    assert(encoder_read_count(&count));
    assert(count == 4103);
    assert(encoder_fail_streak() == 0U);
}

static void test_rejects_invalid_protocol_fields(void)
{
    int32_t count = 0;

    reset_fixture();
    set_response(2U, 1U, 10U);
    assert(!encoder_read_count(&count));

    set_response(1U, 1U, 1024U);
    assert(!encoder_read_count(&count));
    assert(encoder_fail_streak() == 2U);
}

static void test_reports_uart_failures(void)
{
    int32_t count = 0;

    reset_fixture();
    tx_status = HAL_TIMEOUT;
    assert(!encoder_read_count(&count));

    tx_status = HAL_OK;
    rx_status = HAL_ERROR;
    assert(!encoder_read_count(&count));
    assert(encoder_fail_streak() == 2U);
}

int main(void)
{
    test_reads_two_registers_and_combines_count();
    test_rejects_bad_crc_and_tracks_failures();
    test_rejects_invalid_protocol_fields();
    test_reports_uart_failures();
    printf("OK\n");
    return 0;
}
