#include "app.h"

#include <stdbool.h>

#include "cli.h"
#include "control.h"
#include "encoder.h"
#if FC_MAVLINK
#include "fc_link.h"
#endif
#include "led_status.h"
#include "main.h"
#include "nvm.h"
#include "pwm_in.h"
#include "servo_bus.h"
#if USB_CDC_DEBUG
#include "usb_device.h"
#endif

#ifndef FC_MAVLINK
#define FC_MAVLINK 0
#endif

#ifndef USB_CDC_DEBUG
#define USB_CDC_DEBUG 0
#endif

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
    params.cruise_err = stored->cruise_err;
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
        (void)servo_bus_set_motor_speed(speed);
    }
}

void app_reload_params(void)
{
    control_params_t params = control_params_from_nvm(cli_get_params());

    control_init(&s_control, &params);
    pwm_in_set_cmd_range(params.pwm_min_us, params.pwm_max_us);
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
#if USB_CDC_DEBUG
    MX_USB_DEVICE_Init();
#endif
#if FC_MAVLINK
    cli_init(NULL); /* NVM; CLI console may be USB */
    fc_link_init(&huart3);
#elif USB_CDC_DEBUG
    cli_init(NULL); /* NVM + USB CDC console */
#else
    cli_init(&huart3);
#endif
    led_status_init(cli_is_calibrated());

    params = control_params_from_nvm(cli_get_params());
    control_init(&s_control, &params);
    pwm_in_set_cmd_range(params.pwm_min_us, params.pwm_max_us);
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
    static uint32_t last_control_ms;
    bool do_control;

    cli_poll();

    if (s_servo_fault) {
        led_status_fault();
        led_status_poll();
        return;
    }

    if (pwm_in_get_fresh_pulse_us(now_ms, APP_PWM_TIMEOUT_MS, &pulse_us)) {
        control_on_pwm(&s_control, pulse_us, now_ms);
    }

    /* Encoder + control @ ~100 Hz; PWM stamp still updates every tick above. */
    do_control = ((uint32_t)(now_ms - last_control_ms) >= 10U);
    if (do_control) {
        last_control_ms = now_ms;
        if (encoder_read_count(&encoder_count)) {
            s_last_encoder_count = encoder_count;
        } else if (encoder_fail_streak() >= 5U) {
            servo_stop_tracked();
            led_status_fault();
            led_status_poll();
            return;
        }
        control_update(&s_control, s_last_encoder_count, now_ms);
    }

    const bool manual_override = cli_manual_override_active(now_ms);
    if (!cli_is_calibrated()) {
        s_control.hold = true;
        s_control.speed_cmd = 0;
    }

    if (manual_override) {
        servo_speed_tracked(cli_manual_speed());
    } else if (cli_is_calibrated() || (s_control.speed_cmd != 0)) {
        servo_speed_tracked(s_control.speed_cmd);
    } else {
        servo_speed_tracked(0);
    }

    if (!s_servo_fault) {
        const int ramp_rc = servo_bus_ramp_update();
        if (ramp_rc < 0) {
            servo_tx_result(-1, true);
        } else if (ramp_rc > 0) {
            servo_tx_result(0, servo_bus_get_output_speed() != 0);
        }
    }

#if FC_MAVLINK
    fc_link_tick(now_ms);
#endif
#if USB_CDC_DEBUG || !FC_MAVLINK
    cli_telem_tick(now_ms);
    cli_poll();
#endif

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

void app_get_status(app_status_t *out)
{
    uint16_t pulse_us = 0U;
    const uint32_t now_ms = HAL_GetTick();

    if (out == NULL) {
        return;
    }

    out->pwm_ok = pwm_in_get_fresh_pulse_us(now_ms, APP_PWM_TIMEOUT_MS,
                                            &pulse_us);
    out->pwm_irq = pwm_in_irq_count();
    out->pwm_raw_us = pwm_in_last_raw_us();
    if (pwm_in_get_pulse_us(&pulse_us)) {
        /* Filtered command width even when stale/hold (raw is separate). */
        out->pwm_us = pulse_us;
        out->pwm_age_ms = (uint32_t)(now_ms - pwm_in_last_edge_ms());
    } else {
        out->pwm_us = 0U;
        out->pwm_age_ms = 0xFFFFFFFFU;
    }
    out->hold = s_control.hold;
    out->servo_fault = s_servo_fault;
    out->target = s_control.target;
    out->current = s_control.filt_valid ? s_control.current_filt
                                        : s_control.current;
    out->speed_cmd = s_control.speed_cmd;
    out->speed_out = servo_bus_get_output_speed();
    out->settled = s_control.settled;
    out->last_dir = s_control.last_dir;
}
