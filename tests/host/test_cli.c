#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "main.h"
#include "../../App/cli.h"
#include "../../App/nvm.h"

static UART_HandleTypeDef cli_uart;
static const char *rx_text;
static size_t rx_offset;
static char tx_text[1024];
static size_t tx_length;
static bool load_valid;
static bool save_result;
static nvm_blob_t saved_blob;
static bool encoder_valid;
static int32_t encoder_count;
static int16_t motor_speed;
static unsigned int stop_calls;
static uint32_t now_ms;

uint32_t HAL_GetTick(void)
{
    return now_ms;
}

HAL_StatusTypeDef HAL_UART_Receive(UART_HandleTypeDef *huart,
                                   uint8_t *data,
                                   uint16_t size,
                                   uint32_t timeout)
{
    (void)timeout;
    assert(huart == &cli_uart);
    assert(size == 1U);
    if ((rx_text == NULL) || (rx_text[rx_offset] == '\0')) {
        return HAL_TIMEOUT;
    }
    *data = (uint8_t)rx_text[rx_offset++];
    return HAL_OK;
}

HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef *huart,
                                    uint8_t *data,
                                    uint16_t size,
                                    uint32_t timeout)
{
    (void)timeout;
    assert(huart == &cli_uart);
    assert(tx_length + size < sizeof(tx_text));
    memcpy(&tx_text[tx_length], data, size);
    tx_length += size;
    tx_text[tx_length] = '\0';
    return HAL_OK;
}

void nvm_defaults(nvm_blob_t *out)
{
    memset(out, 0, sizeof(*out));
    out->count_b = 10240;
    out->pwm_min_us = 1000U;
    out->pwm_max_us = 2000U;
    out->deadzone = 20;
    out->kp = 500;
    out->vmax = 800;
}

bool nvm_load(nvm_blob_t *out)
{
    if (!load_valid) {
        return false;
    }
    *out = saved_blob;
    return true;
}

bool nvm_save(const nvm_blob_t *in)
{
    saved_blob = *in;
    return save_result;
}

bool encoder_read_count(int32_t *out_count)
{
    if (encoder_valid) {
        *out_count = encoder_count;
    }
    return encoder_valid;
}

uint8_t encoder_fail_streak(void)
{
    return encoder_valid ? 0U : 3U;
}

int servo_bus_set_motor_speed(int16_t speed)
{
    motor_speed = speed;
    return 0;
}

int servo_bus_motor_stop(void)
{
    motor_speed = 0;
    ++stop_calls;
    return 0;
}

static void reset_fixture(void)
{
    rx_text = NULL;
    rx_offset = 0U;
    tx_length = 0U;
    tx_text[0] = '\0';
    load_valid = false;
    save_result = true;
    memset(&saved_blob, 0, sizeof(saved_blob));
    encoder_valid = true;
    encoder_count = 3456;
    motor_speed = 0;
    stop_calls = 0U;
    now_ms = 100U;
    cli_init(&cli_uart);
    tx_length = 0U;
    tx_text[0] = '\0';
}

static void issue(const char *command)
{
    rx_text = command;
    rx_offset = 0U;
    while (rx_text[rx_offset] != '\0') {
        cli_poll();
    }
}

static void test_calibration_captures_both_endpoints_and_saves(void)
{
    reset_fixture();
    issue("cal a\n");
    assert(cli_get_params()->count_a == 3456);
    encoder_count = 8765;
    issue("cal b\r\n");
    assert(cli_get_params()->count_b == 8765);
    issue("cal save\n");
    assert(saved_blob.count_a == 3456);
    assert(saved_blob.count_b == 8765);
    assert(cli_is_calibrated());
    assert(strstr(tx_text, "saved") != NULL);
}

static void test_failed_encoder_does_not_replace_endpoint(void)
{
    reset_fixture();
    encoder_valid = false;
    issue("cal a\n");
    assert(cli_get_params()->count_a == 0);
    assert(strstr(tx_text, "encoder error") != NULL);
}

static void test_motor_hold_and_status_use_hardware_modules(void)
{
    reset_fixture();
    issue("motor -250\n");
    assert(motor_speed == -250);
    assert(cli_manual_override_active(now_ms));
    issue("hold\n");
    assert(motor_speed == 0);
    assert(stop_calls == 1U);
    assert(!cli_manual_override_active(now_ms));
    issue("status\n");
    assert(strstr(tx_text, "count=3456") != NULL);
}

static void test_motor_manual_override_times_out(void)
{
    reset_fixture();
    issue("motor 200\n");
    now_ms += CLI_MANUAL_TIMEOUT_MS - 1U;
    assert(cli_manual_override_active(now_ms));
    now_ms += 1U;
    assert(!cli_manual_override_active(now_ms));
}

static void test_unknown_and_out_of_range_commands_are_rejected(void)
{
    reset_fixture();
    issue("motor 99999\n");
    assert(motor_speed == 0);
    issue("nonsense\n");
    assert(strstr(tx_text, "ERR") != NULL);
}

int main(void)
{
    test_calibration_captures_both_endpoints_and_saves();
    test_failed_encoder_does_not_replace_endpoint();
    test_motor_hold_and_status_use_hardware_modules();
    test_motor_manual_override_times_out();
    test_unknown_and_out_of_range_commands_are_rejected();
    printf("OK\n");
    return 0;
}
