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

static void test_deadzone_hysteresis_stops_hunting(void) {
    control_state_t s;
    control_params_t p = {
        .count_a = 0, .count_b = 1000,
        .pwm_min_us = 1000, .pwm_max_us = 2000,
        .deadzone = 10, .kp = 1000, .vmax = 500,
        .pwm_timeout_ms = 150
    };
    control_init(&s, &p);
    control_on_pwm(&s, 1500, 0); /* target 500; clears settled if needed */
    s.settled = false;
    control_update(&s, 495, 10); /* |e|=5 < 10 → settle */
    assert(s.speed_cmd == 0);
    assert(s.settled);
    control_update(&s, 480, 20); /* |e| after EMA still <= dz_exit → stay settled */
    assert(s.speed_cmd == 0);
    /* Jump far enough that EMA |e| exceeds dz_exit (dz*4=40). */
    control_update(&s, 400, 30);
    assert(s.settled == false);
    assert(s.speed_cmd != 0);
}

static void test_cruise_holds_vmax_until_near_target(void) {
    control_state_t s;
    control_params_t p = {
        .count_a = 0, .count_b = 10000,
        .pwm_min_us = 1000, .pwm_max_us = 2000,
        .deadzone = 10, .kp = 250, .vmax = 500,
        .cruise_err = 1000, .pwm_timeout_ms = 150
    };
    control_init(&s, &p);
    control_on_pwm(&s, 2000, 0); /* target 10000 */
    control_update(&s, 0, 10);   /* |e|=10000 >> cruise */
    assert(s.speed_cmd == 500);
    /* Bypass EMA lag: place filtered count inside brake band. */
    s.current_filt = 9700;
    s.filt_valid = true;
    s.settled = false;
    control_update(&s, 9700, 20); /* |e|=300 < cruise → P brake */
    assert(s.speed_cmd > 0);
    assert(s.speed_cmd < 500);
}

static void test_reverse_lock_coasts_after_overshoot(void) {
    control_state_t s;
    control_params_t p = {
        .count_a = 0, .count_b = 10000,
        .pwm_min_us = 1000, .pwm_max_us = 2000,
        .deadzone = 50, .kp = 250, .vmax = 500,
        .cruise_err = 1000, .pwm_timeout_ms = 150
    };
    control_init(&s, &p);
    control_on_pwm(&s, 1500, 0); /* target 5000 */
    s.settled = false;
    s.last_dir = 1;
    s.filt_valid = true;
    s.current_filt = 5100; /* already past target */
    control_update(&s, 5100, 10); /* e=-100 < reverse_lock → coast */
    assert(s.speed_cmd == 0);
}

static void test_pwm_outside_command_range_clamps(void) {
    control_state_t s;
    control_params_t p = {
        .count_a = 0, .count_b = 1000,
        .pwm_min_us = 1000, .pwm_max_us = 2000,
        .deadzone = 5, .kp = 1000, .vmax = 500,
        .cruise_err = 1000, .pwm_timeout_ms = 150
    };
    control_init(&s, &p);
    control_on_pwm(&s, 900, 0);   /* <1000 → 1000 → count_a */
    assert(s.target == 0);
    control_on_pwm(&s, 2200, 10); /* >2000 → 2000 → count_b */
    assert(s.target == 1000);
    assert(control_pwm_to_target(&p, 800) == 0);
    assert(control_pwm_to_target(&p, 2500) == 1000);
}

static void test_brake_keeps_min_speed_outside_deadzone(void) {
    control_state_t s;
    control_params_t p = {
        .count_a = 0, .count_b = 10000,
        .pwm_min_us = 1000, .pwm_max_us = 2000,
        .deadzone = 50, .kp = 100, .vmax = 500,
        .cruise_err = 1000, .pwm_timeout_ms = 150
    };
    control_init(&s, &p);
    control_on_pwm(&s, 1500, 0); /* target 5000 */
    s.settled = false;
    /* |e|=100 < cruise, P spd = 50*100/1000 = 5 → floored to MIN_BRAKE 200 */
    control_update(&s, 4900, 10);
    assert(s.settled == false);
    assert(s.speed_cmd == 200 || s.speed_cmd == -200);
}

int main(void) {
    test_pwm_mid_maps_midpoint();
    test_deadzone_stops();
    test_deadzone_hysteresis_stops_hunting();
    test_pwm_timeout_hold();
    test_reverse_endpoints();
    test_cruise_holds_vmax_until_near_target();
    test_reverse_lock_coasts_after_overshoot();
    test_pwm_outside_command_range_clamps();
    test_brake_keeps_min_speed_outside_deadzone();
    printf("OK\n");
    return 0;
}
