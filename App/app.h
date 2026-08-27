#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Tolerate brief capture dropouts from hand-tuned / noisy PWM (was 150 → mid-stroke HOLD stutter). */
#define APP_PWM_TIMEOUT_MS  400U

typedef struct {
    bool pwm_ok;
    uint16_t pwm_us;
    uint32_t pwm_age_ms;
    uint32_t pwm_irq;
    uint32_t pwm_raw_us;
    bool hold;
    bool servo_fault;
    int32_t target;
    int32_t current;
    int16_t speed_cmd;
    int16_t speed_out;
    bool settled;
    int8_t last_dir;
} app_status_t;

void app_init(void);
void app_tick(uint32_t now_ms);
void app_reload_params(void);
void app_get_status(app_status_t *out);
