#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    int32_t count_a;
    int32_t count_b;
    uint16_t pwm_min_us;
    uint16_t pwm_max_us;
    int32_t deadzone;
    int32_t kp;
    int32_t vmax;
    int32_t cruise_err;       /* |e| above this → vmax cruise */
    uint32_t pwm_timeout_ms;
} control_params_t;

typedef struct {
    control_params_t p;
    int32_t target;
    int32_t current;
    int32_t current_filt;
    int16_t speed_cmd;
    int8_t last_dir;       /* +1 / -1 / 0: last commanded travel direction */
    bool hold;
    bool pwm_valid;
    bool settled;
    bool filt_valid;
    uint32_t last_pwm_ms;
} control_state_t;

#define CONTROL_KP_SCALE 1000
#ifndef CONTROL_DZ_EXIT_MUL
#define CONTROL_DZ_EXIT_MUL  4
#endif
#ifndef CONTROL_MIN_BRAKE_SPEED
#define CONTROL_MIN_BRAKE_SPEED  200
#endif
/*
 * After overshoot while traveling, do not reverse until |error| exceeds this.
 * Stops mid-stroke hunting when PWM sweeps 1000→2000 (or reverse).
 */
#ifndef CONTROL_REVERSE_LOCK
#define CONTROL_REVERSE_LOCK  400
#endif

void control_init(control_state_t *s, const control_params_t *p);
void control_on_pwm(control_state_t *s, uint16_t pulse_us, uint32_t now_ms);
void control_update(control_state_t *s, int32_t current_count, uint32_t now_ms);
int32_t control_pwm_to_target(const control_params_t *p, uint16_t pulse_us);
