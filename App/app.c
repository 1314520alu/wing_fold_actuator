#include "app.h"

#include <stdbool.h>

#include "cli.h"
#include "control.h"
#include "encoder.h"
#include "led_status.h"
#include "main.h"
#include "nvm.h"
#include "pwm_in.h"
#include "servo_bus.h"

#define APP_SERVO_TX_FAILURE_LIMIT  3U

extern TIM_HandleTypeDef htim2;
extern UART_HandleTypeDef huart1;
extern UART_HandleTypeDef huart2;
extern UART_HandleTypeDef huart3;

static control_state_t s_control;
static int32_t s_last_encoder_count;
static uint8_t s_servo_tx_failures;
static bool s_servo_fault;

static control_params_t control_params_from_nvm(const nvm_blob_t *stored)
{
    control_params_t params;

    params.count_a = stored->count_a;
    params.count_b = stored->count_b;
    params.pwm_min_us = stored->pwm_min_us;
    params.pwm_max_us = stored->pwm_max_us;
    params.deadzone = stored->deadzone;
    params.kp = stored->kp;
    params.vmax = stored->vmax;
    params.pwm_timeout_ms = APP_PWM_TIMEOUT_MS;
    return params;
}

static void servo_tx_result(int result, bool motion_command)
{
    if (result == 0) {
        s_servo_tx_failures = 0U;
        return;
    }

    if (s_servo_tx_failures < UINT8_MAX) {
        ++s_servo_tx_failures;
    }
    if (s_servo_tx_failures >= APP_SERVO_TX_FAILURE_LIMIT) {
        s_servo_fault = true;
        s_control.hold = true;
        s_control.speed_cmd = 0;
        if (motion_command) {
            (void)servo_bus_motor_stop();
        }
    }
}

static void servo_stop_tracked(void)
{
    if (!s_servo_fault) {
        servo_tx_result(servo_bus_motor_stop(), false);
    }
}

static void servo_speed_tracked(int16_t speed)
{
    if (!s_servo_fault) {
        servo_tx_result(servo_bus_set_motor_speed(speed), speed != 0);
    }
}

void app_reload_params(void)
{
    control_params_t params = control_params_from_nvm(cli_get_params());

    control_init(&s_control, &params);
    s_control.current = s_last_encoder_count;
    s_control.target = s_last_encoder_count;
    s_control.hold = true;
    s_control.speed_cmd = 0;
    servo_stop_tracked();
}

void app_init(void)
{
    control_params_t params;

    servo_bus_init(&huart1, 1U);
    encoder_init(&huart2);
    pwm_in_init(&htim2);
    cli_init(&huart3);
    led_status_init(cli_is_calibrated());

    params = control_params_from_nvm(cli_get_params());
    control_init(&s_control, &params);
    s_control.hold = true;
    s_last_encoder_count = 0;
    s_servo_tx_failures = 0U;
    s_servo_fault = false;
    servo_stop_tracked();
}

void app_tick(uint32_t now_ms)
{
    int32_t encoder_count;
    uint16_t pulse_us;

    cli_poll();

    if (s_servo_fault) {
        led_status_fault();
        led_status_poll();
        return;
    }

    if (encoder_read_count(&encoder_count)) {
        s_last_encoder_count = encoder_count;
    } else if (encoder_fail_streak() >= 5U) {
        servo_stop_tracked();
        led_status_fault();
        led_status_poll();
        return;
    }

    if (pwm_in_get_pulse_us(&pulse_us)) {
        control_on_pwm(&s_control, pulse_us, pwm_in_last_edge_ms());
    }
    control_update(&s_control, s_last_encoder_count, now_ms);

    const bool manual_override = cli_manual_override_active(now_ms);
    if (!cli_is_calibrated()) {
        s_control.hold = true;
        s_control.speed_cmd = 0;
    }

    if (!manual_override) {
        servo_speed_tracked(s_control.speed_cmd);
    }
    if (s_servo_fault) {
        led_status_fault();
        led_status_poll();
        return;
    }

    led_status_set_calibrated(cli_is_calibrated());
    if (s_control.hold) {
        led_status_hold();
    } else {
        led_status_run();
    }
    led_status_poll();
}
