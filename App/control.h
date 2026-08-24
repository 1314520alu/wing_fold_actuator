#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    int32_t count_a;
    int32_t count_b;
    uint16_t pwm_min_us;   /* default 1000 */
    uint16_t pwm_max_us;   /* default 2000 */
    int32_t deadzone;      /* counts */
    int32_t kp;            /* speed = kp * e / KP_SCALE */
    int32_t vmax;          /* abs speed limit */
    uint32_t pwm_timeout_ms;
} control_params_t;

typedef struct {
    control_params_t p;
    int32_t target;
    int32_t current;
    int16_t speed_cmd;
    bool hold;
    bool pwm_valid;
    uint32_t last_pwm_ms;
} control_state_t;

#define CONTROL_KP_SCALE 1000

void control_init(control_state_t *s, const control_params_t *p);
/* 有新脉宽时调用；更新 last_pwm_ms 与 pwm_valid */
void control_on_pwm(control_state_t *s, uint16_t pulse_us, uint32_t now_ms);
/* 每控制周期调用；更新 target（若非 hold）、speed_cmd */
void control_update(control_state_t *s, int32_t current_count, uint32_t now_ms);
int32_t control_pwm_to_target(const control_params_t *p, uint16_t pulse_us);
