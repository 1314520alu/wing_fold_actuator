#include "control.h"

#define PWM_VALID_MIN_US 800
#define PWM_VALID_MAX_US 2200

void control_init(control_state_t *s, const control_params_t *p)
{
    s->p = *p;
    s->target = 0;
    s->current = 0;
    s->speed_cmd = 0;
    s->hold = false;
    s->pwm_valid = false;
    s->last_pwm_ms = 0;
}

int32_t control_pwm_to_target(const control_params_t *p, uint16_t pulse_us)
{
    if (pulse_us < p->pwm_min_us) {
        pulse_us = p->pwm_min_us;
    }
    if (pulse_us > p->pwm_max_us) {
        pulse_us = p->pwm_max_us;
    }
    int32_t span = (int32_t)p->pwm_max_us - (int32_t)p->pwm_min_us;
    if (span <= 0) {
        return p->count_a;
    }
    int32_t num = (int32_t)(pulse_us - p->pwm_min_us);
    return p->count_a + (int32_t)(((int64_t)(p->count_b - p->count_a) * num) / span);
}

void control_on_pwm(control_state_t *s, uint16_t pulse_us, uint32_t now_ms)
{
    if (pulse_us < PWM_VALID_MIN_US || pulse_us > PWM_VALID_MAX_US) {
        return;
    }
    s->hold = false;
    s->pwm_valid = true;
    s->last_pwm_ms = now_ms;
    s->target = control_pwm_to_target(&s->p, pulse_us);
}

void control_update(control_state_t *s, int32_t current_count, uint32_t now_ms)
{
    s->current = current_count;
    if (s->pwm_valid && (now_ms - s->last_pwm_ms) > s->p.pwm_timeout_ms) {
        s->hold = true;
        s->pwm_valid = false;
    }
    if (s->hold) {
        s->speed_cmd = 0;
        return;
    }
    int32_t e = s->target - s->current;
    if (e > -s->p.deadzone && e < s->p.deadzone) {
        s->speed_cmd = 0;
        return;
    }
    int32_t spd = (int32_t)(((int64_t)s->p.kp * e) / CONTROL_KP_SCALE);
    if (spd > s->p.vmax) {
        spd = s->p.vmax;
    }
    if (spd < -s->p.vmax) {
        spd = -s->p.vmax;
    }
    s->speed_cmd = (int16_t)spd;
}
