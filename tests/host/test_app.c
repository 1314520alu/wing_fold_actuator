#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "main.h"
#include "../../App/app.h"
#include "../../App/nvm.h"

UART_HandleTypeDef huart1;
UART_HandleTypeDef huart2;
UART_HandleTypeDef huart3;
TIM_HandleTypeDef htim2;

static nvm_blob_t params;
static bool manual_override;
static bool encoder_ok;
static uint8_t encoder_failures;
static int32_t encoder_count;
static bool pwm_valid;
static uint16_t pwm_us;
static uint32_t pwm_edge_ms;
static int16_t motor_speed;
static unsigned int speed_writes;
static unsigned int stop_writes;
static unsigned int fault_led_calls;
static unsigned int hold_led_calls;
static unsigned int run_led_calls;

void servo_bus_init(UART_HandleTypeDef *huart, uint8_t servo_id)
{
    assert(huart == &huart1);
    assert(servo_id == 1U);
}

int servo_bus_set_motor_speed(int16_t speed)
{
    motor_speed = speed;
    ++speed_writes;
    return 0;
}

int servo_bus_motor_stop(void)
{
    motor_speed = 0;
    ++stop_writes;
    return 0;
}

void encoder_init(UART_HandleTypeDef *huart)
{
    assert(huart == &huart2);
}

bool encoder_read_count(int32_t *out_count)
{
    if (encoder_ok) {
        *out_count = encoder_count;
    }
    return encoder_ok;
}

uint8_t encoder_fail_streak(void)
{
    return encoder_failures;
}

void pwm_in_init(TIM_HandleTypeDef *htim)
{
    assert(htim == &htim2);
}

bool pwm_in_get_pulse_us(uint16_t *out_us)
{
    if (pwm_valid) {
        *out_us = pwm_us;
    }
    return pwm_valid;
}

uint32_t pwm_in_last_edge_ms(void)
{
    return pwm_edge_ms;
}

void cli_init(UART_HandleTypeDef *huart)
{
    assert(huart == &huart3);
}

void cli_poll(void)
{
}

const nvm_blob_t *cli_get_params(void)
{
    return &params;
}

bool cli_is_calibrated(void)
{
    return true;
}

bool cli_manual_override_active(uint32_t now_ms)
{
    (void)now_ms;
    return manual_override;
}

void led_status_init(bool calibrated)
{
    assert(calibrated);
}

void led_status_set_calibrated(bool calibrated)
{
    assert(calibrated);
}

void led_status_run(void)
{
    ++run_led_calls;
}

void led_status_hold(void)
{
    ++hold_led_calls;
}

void led_status_fault(void)
{
    ++fault_led_calls;
}

void led_status_poll(void)
{
}

static void reset_fixture(void)
{
    memset(&params, 0, sizeof(params));
    params.count_a = 0;
    params.count_b = 1000;
    params.pwm_min_us = 1000U;
    params.pwm_max_us = 2000U;
    params.deadzone = 5;
    params.kp = 1000;
    params.vmax = 500;
    manual_override = false;
    encoder_ok = true;
    encoder_failures = 0U;
    encoder_count = 0;
    pwm_valid = true;
    pwm_us = 2000U;
    pwm_edge_ms = 10U;
    motor_speed = 0;
    speed_writes = 0U;
    stop_writes = 0U;
    fault_led_calls = 0U;
    hold_led_calls = 0U;
    run_led_calls = 0U;
    app_init();
    speed_writes = 0U;
    stop_writes = 0U;
}

static void test_auto_writes_closed_loop_speed(void)
{
    reset_fixture();
    app_tick(10U);
    assert(speed_writes == 1U);
    assert(motor_speed == 500);
    assert(run_led_calls == 1U);
}

static void test_manual_override_pauses_closed_loop_writes(void)
{
    reset_fixture();
    manual_override = true;
    app_tick(10U);
    assert(speed_writes == 0U);
}

static void test_encoder_failure_streak_forces_fault_stop(void)
{
    reset_fixture();
    manual_override = true;
    encoder_ok = false;
    encoder_failures = 5U;
    motor_speed = 200;
    app_tick(10U);
    assert(stop_writes == 1U);
    assert(motor_speed == 0);
    assert(fault_led_calls == 1U);
}

static void test_stale_pwm_edge_enters_hold(void)
{
    reset_fixture();
    app_tick(10U);
    app_tick(161U);
    assert(motor_speed == 0);
    assert(hold_led_calls == 1U);
}

int main(void)
{
    test_auto_writes_closed_loop_speed();
    test_manual_override_pauses_closed_loop_writes();
    test_encoder_failure_streak_forces_fault_stop();
    test_stale_pwm_edge_enters_hold();
    printf("OK\n");
    return 0;
}
