#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "main.h"
#include "../../App/app.h"
#include "../../App/cli.h"
#include "../../App/nvm.h"

static UART_HandleTypeDef cli_uart;
static const char *rx_text;
static size_t rx_offset;
static char tx_text[1024];
static size_t tx_length;
static unsigned int blocking_tx_calls;
static unsigned int interrupt_tx_calls;
static bool load_valid;
static bool save_result;
static nvm_blob_t saved_blob;
static bool encoder_valid;
static int32_t encoder_count;
static int16_t motor_speed;
static unsigned int stop_calls;
static unsigned int reload_calls;
static uint32_t now_ms;
static bool status_pwm_ok;
static uint16_t status_pwm_us;
static bool status_hold;

void app_reload_params(void)
{
    ++reload_calls;
}

uint32_t HAL_GetTick(void)
{
    return now_ms;
}

void HAL_NVIC_SetPriority(int irqn, uint32_t priority, uint32_t subpriority)
{
    (void)irqn;
    (void)priority;
    (void)subpriority;
}

void HAL_NVIC_EnableIRQ(int irqn)
{
    (void)irqn;
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
    ++blocking_tx_calls;
    assert(tx_length + size < sizeof(tx_text));
    memcpy(&tx_text[tx_length], data, size);
    tx_length += size;
    tx_text[tx_length] = '\0';
    return HAL_OK;
}

HAL_StatusTypeDef HAL_UART_Transmit_IT(UART_HandleTypeDef *huart,
                                       uint8_t *data,
                                       uint16_t size)
{
    assert(huart == &cli_uart);
    ++interrupt_tx_calls;
    assert(tx_length + size < sizeof(tx_text));
    huart->gState = HAL_UART_STATE_BUSY;
    memcpy(&tx_text[tx_length], data, size);
    tx_length += size;
    tx_text[tx_length] = '\0';
    huart->gState = HAL_UART_STATE_READY;
    return HAL_OK;
}

void nvm_defaults(nvm_blob_t *out)
{
    memset(out, 0, sizeof(*out));
    out->count_b = 24000;
    out->pwm_min_us = 1000U;
    out->pwm_max_us = 2000U;
    out->deadzone = 150;
    out->kp = 250;
    out->vmax = 1000;
    out->cruise_err = 1000;
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

int servo_bus_set_motor_speed_immediate(int16_t speed)
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

int servo_bus_ramp_update(void)
{
    return 0;
}

int16_t servo_bus_get_output_speed(void)
{
    return motor_speed;
}

void app_get_status(app_status_t *out)
{
    if (out == NULL) {
        return;
    }
    out->pwm_ok = status_pwm_ok;
    out->pwm_us = status_pwm_us;
    out->pwm_age_ms = 0xFFFFFFFFU;
    out->pwm_irq = 0U;
    out->pwm_raw_us = 0U;
    out->hold = status_hold;
    out->servo_fault = false;
    out->target = 0;
    out->current = encoder_count;
    out->speed_cmd = 0;
    out->speed_out = motor_speed;
    out->settled = true;
    out->last_dir = 0;
}

static void reset_fixture(void)
{
    rx_text = NULL;
    rx_offset = 0U;
    tx_length = 0U;
    tx_text[0] = '\0';
    blocking_tx_calls = 0U;
    interrupt_tx_calls = 0U;
    load_valid = false;
    save_result = true;
    memset(&saved_blob, 0, sizeof(saved_blob));
    encoder_valid = true;
    encoder_count = 3456;
    motor_speed = 0;
    stop_calls = 0U;
    reload_calls = 0U;
    now_ms = 100U;
    status_pwm_ok = false;
    status_pwm_us = 1600U;
    status_hold = true;
    cli_uart.gState = HAL_UART_STATE_READY;
    cli_init(&cli_uart);
    tx_length = 0U;
    tx_text[0] = '\0';
    blocking_tx_calls = 0U;
    interrupt_tx_calls = 0U;
}

static void issue(const char *command)
{
    for (const char *p = command; *p != '\0'; ++p) {
        cli_uart_rx_irq_byte((uint8_t)*p);
        cli_poll();
    }
}

static void test_boot_defaults_are_calibrated(void)
{
    reset_fixture();
    assert(cli_is_calibrated());
    assert(cli_get_params()->count_a == 0);
    assert(cli_get_params()->count_b == 24000);

    cli_init(&cli_uart);
    assert(cli_is_calibrated());
    assert(strstr(tx_text, "default a=0 b=24000") != NULL);
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
    assert(reload_calls == 1U);
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

static void test_motor_manual_override_persists_until_hold(void)
{
    reset_fixture();
    issue("motor 200\n");
    now_ms += 60000U;
    assert(cli_manual_override_active(now_ms));
    assert(cli_manual_speed() == 200);
    issue("hold\n");
    assert(!cli_manual_override_active(now_ms));
    assert(cli_manual_speed() == 0);
}

static void test_unknown_and_out_of_range_commands_are_rejected(void)
{
    reset_fixture();
    issue("motor 99999\n");
    assert(motor_speed == 0);
    issue("nonsense\n");
    assert(strstr(tx_text, "ERR") != NULL);
}

static const char *telem_stub_line(uint32_t ms)
{
    static char expected[80];

    (void)snprintf(expected, sizeof(expected),
                   "T,%lu,1600,3456,0,-3456,0,0,1,1,0,0\r\n",
                   (unsigned long)ms);
    return expected;
}

static void test_telem_off_by_default_and_emits_when_on(void)
{
    reset_fixture();
    tx_length = 0;
    tx_text[0] = '\0';
    cli_telem_tick(100);
    assert(tx_length == 0); /* off */

    issue("telem\n");
    assert(strstr(tx_text, "telem=off\r\n") != NULL);

    issue("telem on\n");
    assert(strstr(tx_text, "OK telem on") != NULL);
    issue("telem\n");
    assert(strstr(tx_text, "telem=on\r\n") != NULL);

    tx_length = 0;
    tx_text[0] = '\0';
    blocking_tx_calls = 0U;
    interrupt_tx_calls = 0U;
    cli_telem_tick(100);
    assert(strcmp(tx_text, telem_stub_line(100)) == 0);
    assert(interrupt_tx_calls == 1U);
    assert(blocking_tx_calls == 0U);

    tx_length = 0;
    tx_text[0] = '\0';
    cli_telem_tick(105);
    assert(tx_length == 0); /* <10 ms gate */

    tx_length = 0;
    tx_text[0] = '\0';
    cli_telem_tick(110);
    assert(strcmp(tx_text, telem_stub_line(110)) == 0);

    cli_uart.gState = HAL_UART_STATE_BUSY;
    tx_length = 0;
    tx_text[0] = '\0';
    cli_telem_tick(120);
    assert(tx_length == 0); /* UART busy drop */

    cli_uart.gState = HAL_UART_STATE_READY;
    tx_length = 0;
    tx_text[0] = '\0';
    cli_telem_tick(130);
    assert(strcmp(tx_text, telem_stub_line(130)) == 0);

    issue("telem off\n");
    assert(strstr(tx_text, "OK telem off") != NULL);
    issue("telem\n");
    assert(strstr(tx_text, "telem=off\r\n") != NULL);

    tx_length = 0;
    tx_text[0] = '\0';
    cli_telem_tick(200);
    assert(tx_length == 0);
}

static void test_telem_reports_filtered_pwm_even_when_holding(void)
{
    reset_fixture();
    issue("telem on\n");

    tx_length = 0U;
    tx_text[0] = '\0';
    status_pwm_ok = true;
    status_hold = false;
    cli_telem_tick(100U);
    assert(strstr(tx_text, "T,100,1600,") == tx_text);

    tx_length = 0U;
    tx_text[0] = '\0';
    status_hold = true;
    cli_telem_tick(110U);
    assert(strstr(tx_text, "T,110,1600,") == tx_text);

    tx_length = 0U;
    tx_text[0] = '\0';
    status_hold = false;
    status_pwm_ok = false;
    status_pwm_us = 0U;
    cli_telem_tick(120U);
    assert(strstr(tx_text, "T,120,0,") == tx_text);
}

int main(void)
{
    test_boot_defaults_are_calibrated();
    test_calibration_captures_both_endpoints_and_saves();
    test_failed_encoder_does_not_replace_endpoint();
    test_motor_hold_and_status_use_hardware_modules();
    test_motor_manual_override_persists_until_hold();
    test_unknown_and_out_of_range_commands_are_rejected();
    test_telem_off_by_default_and_emits_when_on();
    test_telem_reports_filtered_pwm_even_when_holding();
    printf("OK\n");
    return 0;
}
