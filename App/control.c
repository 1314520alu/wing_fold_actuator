#include "control.h"

/* Capture gate before clamp; command range uses p->pwm_min_us / pwm_max_us. */
#define PWM_VALID_MIN_US 500
#define PWM_VALID_MAX_US 2500

#ifndef CONTROL_CRUISE_ERR_DEFAULT
#define CONTROL_CRUISE_ERR_DEFAULT  1000
#endif

static int32_t iabs32(int32_t v)
{
    return (v < 0) ? -v : v;
}

void control_init(control_state_t *s, const control_params_t *p)
{
    s->p = *p;
    if (s->p.cruise_err < 1) {
        s->p.cruise_err = CONTROL_CRUISE_ERR_DEFAULT;
    }
    if (s->p.kp < 1) {
        s->p.kp = 1;
    }
    s->target = 0;
    s->current = 0;
    s->current_filt = 0;
    s->speed_cmd = 0;
    s->last_dir = 0;
    s->hold = false;
    s->pwm_valid = false;
    s->settled = true;
    s->filt_valid = false;
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
    int32_t new_target;

    if (pulse_us < PWM_VALID_MIN_US || pulse_us > PWM_VALID_MAX_US) {
        return;
    }
    /* Hand-tuned sticks often exceed command range — clamp then map. */
    if (pulse_us < s->p.pwm_min_us) {
        pulse_us = s->p.pwm_min_us;
    } else if (pulse_us > s->p.pwm_max_us) {
        pulse_us = s->p.pwm_max_us;
    }
    new_target = control_pwm_to_target(&s->p, pulse_us);
    if (iabs32(new_target - s->target) > s->p.deadzone) {
        s->settled = false;
    }
    s->hold = false;
    s->pwm_valid = true;
    s->last_pwm_ms = now_ms;
    s->target = new_target;
}

void control_update(control_state_t *s, int32_t current_count, uint32_t now_ms)
{
    int32_t e;
    int32_t ae;
    int32_t spd;
    int32_t cruise;
    int32_t dz;
    int32_t dz_exit;
    const int32_t reverse_lock = (int32_t)CONTROL_REVERSE_LOCK;

    s->current = current_count;
    /* Mild EMA — heavy lag makes one-way overshoot worse. */
    if (!s->filt_valid) {
        s->current_filt = current_count;
        s->filt_valid = true;
    } else {
        s->current_filt = (s->current_filt + current_count) / 2;
    }

    if (s->pwm_valid && (now_ms - s->last_pwm_ms) > s->p.pwm_timeout_ms) {
        s->hold = true;
        s->pwm_valid = false;
    }
    if (s->hold) {
        s->speed_cmd = 0;
        s->settled = true;
        s->last_dir = 0;
        return;
    }

    e = s->target - s->current_filt;
    ae = iabs32(e);
    dz = s->p.deadzone;
    if (dz < 1) {
        dz = 1;
    }
    dz_exit = dz * (int32_t)CONTROL_DZ_EXIT_MUL;

    if (s->settled) {
        if (ae <= dz_exit) {
            s->speed_cmd = 0;
            s->last_dir = 0;
            return;
        }
        s->settled = false;
    } else if (ae < dz) {
        /* Only enter settle inside deadzone — never from low brake speed. */
        s->settled = true;
        s->speed_cmd = 0;
        s->last_dir = 0;
        return;
    }

    /*
     * Mid-stroke anti-hunt: if we were driving + and briefly overshoot (e<0),
     * coast instead of slamming reverse.
     */
    if ((s->last_dir > 0) && (e < 0) && (ae < reverse_lock)) {
        s->speed_cmd = 0;
        return;
    }
    if ((s->last_dir < 0) && (e > 0) && (ae < reverse_lock)) {
        s->speed_cmd = 0;
        return;
    }

    cruise = s->p.cruise_err;
    if (cruise < 1) {
        cruise = CONTROL_CRUISE_ERR_DEFAULT;
    }
    if (cruise <= dz_exit) {
        cruise = dz_exit + 1;
    }

    if (ae >= cruise) {
        spd = s->p.vmax;
    } else {
        /* P-style brake: spd ~= kp * (|e| - dz) / KP_SCALE, floor MIN_BRAKE. */
        spd = (int32_t)(((int64_t)(ae - dz) * (int64_t)s->p.kp)
                        / (int64_t)CONTROL_KP_SCALE);
        if (spd > s->p.vmax) {
            spd = s->p.vmax;
        }
        if (spd < (int32_t)CONTROL_MIN_BRAKE_SPEED) {
            spd = (int32_t)CONTROL_MIN_BRAKE_SPEED;
        }
    }
    if (e < 0) {
        spd = -spd;
    }
    if (spd > s->p.vmax) {
        spd = s->p.vmax;
    }
    if (spd < -s->p.vmax) {
        spd = -s->p.vmax;
    }

    if (spd > 0) {
        s->last_dir = 1;
    } else if (spd < 0) {
        s->last_dir = -1;
    }
    s->speed_cmd = (int16_t)spd;
}
