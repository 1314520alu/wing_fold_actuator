#include <assert.h>
#include <stdio.h>
#include "../../App/control.h"

static void test_pwm_mid_maps_midpoint(void) {
    control_params_t p = {
        .count_a = 0, .count_b = 1000,
        .pwm_min_us = 1000, .pwm_max_us = 2000,
        .deadzone = 5, .kp = 1000, .vmax = 500,
        .pwm_timeout_ms = 150
    };
    assert(control_pwm_to_target(&p, 1500) == 500);
}

static void test_deadzone_stops(void) {
    control_state_t s;
    control_params_t p = {
        .count_a = 0, .count_b = 1000,
        .pwm_min_us = 1000, .pwm_max_us = 2000,
        .deadzone = 10, .kp = 1000, .vmax = 500,
        .pwm_timeout_ms = 150
    };
    control_init(&s, &p);
    control_on_pwm(&s, 1500, 0);
    control_update(&s, 495, 10);  /* |e|=5 < 10 */
    assert(s.speed_cmd == 0);
}

static void test_pwm_timeout_hold(void) {
    control_state_t s;
    control_params_t p = {
        .count_a = 0, .count_b = 1000,
        .pwm_min_us = 1000, .pwm_max_us = 2000,
        .deadzone = 5, .kp = 1000, .vmax = 500,
        .pwm_timeout_ms = 150
    };
    control_init(&s, &p);
    control_on_pwm(&s, 2000, 0);
    control_update(&s, 0, 10);
    assert(s.speed_cmd != 0);
    control_update(&s, 0, 200); /* timeout */
    assert(s.hold == true);
    assert(s.speed_cmd == 0);
}

static void test_reverse_endpoints(void) {
    control_params_t p = {
        .count_a = 1000, .count_b = 0,
        .pwm_min_us = 1000, .pwm_max_us = 2000,
        .deadzone = 5, .kp = 1000, .vmax = 500,
        .pwm_timeout_ms = 150
    };
    assert(control_pwm_to_target(&p, 1000) == 1000);
    assert(control_pwm_to_target(&p, 2000) == 0);
}

int main(void) {
    test_pwm_mid_maps_midpoint();
    test_deadzone_stops();
    test_pwm_timeout_hold();
    test_reverse_endpoints();
    printf("OK\n");
    return 0;
}
