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

extern TIM_HandleTypeDef htim2;
extern UART_HandleTypeDef huart1;
extern UART_HandleTypeDef huart2;
extern UART_HandleTypeDef huart3;

static control_state_t s_control;
static int32_t s_last_encoder_count;

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
    (void)servo_bus_motor_stop();
}

void app_tick(uint32_t now_ms)
{
    int32_t encoder_count;
    uint16_t pulse_us;

    cli_poll();

    if (encoder_read_count(&encoder_count)) {
        s_last_encoder_count = encoder_count;
    } else if (encoder_fail_streak() >= 5U) {
        (void)servo_bus_motor_stop();
        led_status_fault();
        led_status_poll();
        return;
    }

    if (pwm_in_get_pulse_us(&pulse_us)) {
        control_on_pwm(&s_control, pulse_us, pwm_in_last_edge_ms());
    }
    control_update(&s_control, s_last_encoder_count, now_ms);

    if (!cli_manual_override_active(now_ms)) {
        (void)servo_bus_set_motor_speed(s_control.speed_cmd);
    }

    led_status_set_calibrated(cli_is_calibrated());
    if (s_control.hold) {
        led_status_hold();
    } else {
        led_status_run();
    }
    led_status_poll();
}
