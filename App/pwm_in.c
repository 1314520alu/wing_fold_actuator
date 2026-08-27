#include "pwm_in.h"

/* Accept short servo high-time; reject frame-low (~18 ms). */
#define PWM_IN_CAPTURE_MIN_US  500U
#define PWM_IN_CAPTURE_MAX_US  2500U
#define PWM_IN_MEDIAN_N        3U
/* Time-based slew: µs of command change allowed per millisecond. */
#define PWM_IN_SLEW_US_PER_MS  20U

static TIM_HandleTypeDef *s_htim;
static volatile uint16_t s_last_pulse_us;
static volatile bool s_pulse_valid;
static volatile uint32_t s_last_edge_ms;
static volatile uint32_t s_edge_mark;
static volatile bool s_have_rise;
static volatile uint32_t s_irq_count;
static volatile uint32_t s_last_raw_us;

static uint16_t s_cmd_min_us = 1000U;
static uint16_t s_cmd_max_us = 2000U;

static uint16_t s_med_buf[PWM_IN_MEDIAN_N];
static uint8_t s_med_count;
static uint8_t s_med_idx;
static bool s_slew_valid;
static uint16_t s_slewed_us;
static uint32_t s_slew_last_ms;

static uint32_t capture_delta(uint32_t start, uint32_t end)
{
    if (end >= start) {
        return end - start;
    }
    return (65536U - start) + end;
}

static uint16_t clamp_cmd_us(uint32_t width_us)
{
    if (width_us < s_cmd_min_us) {
        return s_cmd_min_us;
    }
    if (width_us > s_cmd_max_us) {
        return s_cmd_max_us;
    }
    return (uint16_t)width_us;
}

static uint16_t median3(uint16_t a, uint16_t b, uint16_t c)
{
    if (a > b) {
        uint16_t t = a;
        a = b;
        b = t;
    }
    if (b > c) {
        uint16_t t = b;
        b = c;
        c = t;
    }
    if (a > b) {
        uint16_t t = a;
        a = b;
        b = t;
    }
    return b;
}

static uint16_t push_median(uint16_t sample)
{
    s_med_buf[s_med_idx] = sample;
    s_med_idx = (uint8_t)((s_med_idx + 1U) % PWM_IN_MEDIAN_N);
    if (s_med_count < PWM_IN_MEDIAN_N) {
        ++s_med_count;
        return sample;
    }
    return median3(s_med_buf[0], s_med_buf[1], s_med_buf[2]);
}

static uint16_t slew_limit(uint16_t sample, uint32_t now_ms)
{
    int32_t delta;
    int32_t allow;
    uint32_t dt_ms;

    if (!s_slew_valid) {
        s_slewed_us = sample;
        s_slew_valid = true;
        s_slew_last_ms = now_ms;
        return sample;
    }

    dt_ms = (uint32_t)(now_ms - s_slew_last_ms);
    if (dt_ms == 0U) {
        dt_ms = 1U;
    }
    if (dt_ms > 100U) {
        dt_ms = 100U;
    }
    allow = (int32_t)(dt_ms * PWM_IN_SLEW_US_PER_MS);
    delta = (int32_t)sample - (int32_t)s_slewed_us;
    if (delta > allow) {
        delta = allow;
    } else if (delta < -allow) {
        delta = -allow;
    }
    s_slewed_us = (uint16_t)((int32_t)s_slewed_us + delta);
    s_slew_last_ms = now_ms;
    return s_slewed_us;
}

static void accept_width(uint32_t width_us)
{
    uint16_t cmd;
    const uint32_t now_ms = HAL_GetTick();

    s_last_raw_us = width_us;
    if ((width_us < PWM_IN_CAPTURE_MIN_US) || (width_us > PWM_IN_CAPTURE_MAX_US)) {
        return;
    }

    cmd = clamp_cmd_us(width_us);
    cmd = push_median(cmd);
    cmd = slew_limit(cmd, now_ms);

    s_last_pulse_us = cmd;
    s_pulse_valid = true;
    s_last_edge_ms = now_ms;
}

void pwm_in_set_cmd_range(uint16_t min_us, uint16_t max_us)
{
    if ((min_us < 500U) || (max_us > 2500U) || (min_us >= max_us)) {
        s_cmd_min_us = 1000U;
        s_cmd_max_us = 2000U;
        return;
    }
    s_cmd_min_us = min_us;
    s_cmd_max_us = max_us;
}

void pwm_in_init(TIM_HandleTypeDef *htim)
{
    GPIO_InitTypeDef gpio = {0};

    s_htim = htim;
    s_last_pulse_us = 0U;
    s_pulse_valid = false;
    s_last_edge_ms = 0U;
    s_edge_mark = 0U;
    s_have_rise = false;
    s_irq_count = 0U;
    s_last_raw_us = 0U;
    s_med_count = 0U;
    s_med_idx = 0U;
    s_slew_valid = false;
    s_slewed_us = 0U;
    s_slew_last_ms = 0U;
    s_cmd_min_us = 1000U;
    s_cmd_max_us = 2000U;

    if (s_htim == NULL) {
        return;
    }

    (void)HAL_TIM_Base_Start(s_htim);

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_AFIO_CLK_ENABLE();

    gpio.Pin = GPIO_PIN_0;
    gpio.Mode = GPIO_MODE_IT_RISING_FALLING;
    gpio.Pull = GPIO_PULLDOWN;
    HAL_GPIO_Init(GPIOA, &gpio);

    HAL_NVIC_SetPriority(EXTI0_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(EXTI0_IRQn);
}

bool pwm_in_get_pulse_us(uint16_t *out_us)
{
    if ((out_us == NULL) || !s_pulse_valid) {
        return false;
    }

    *out_us = s_last_pulse_us;
    return true;
}

uint32_t pwm_in_last_edge_ms(void)
{
    return s_last_edge_ms;
}

bool pwm_in_get_fresh_pulse_us(uint32_t now_ms, uint32_t max_age_ms,
                               uint16_t *out_us)
{
    if (!pwm_in_get_pulse_us(out_us)) {
        return false;
    }
    if ((uint32_t)(now_ms - s_last_edge_ms) > max_age_ms) {
        return false;
    }
    return true;
}

uint32_t pwm_in_irq_count(void)
{
    return s_irq_count;
}

uint32_t pwm_in_last_raw_us(void)
{
    return s_last_raw_us;
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    uint32_t now;
    uint32_t width;

    if ((GPIO_Pin != GPIO_PIN_0) || (s_htim == NULL)) {
        return;
    }

    now = __HAL_TIM_GET_COUNTER(s_htim);
    s_irq_count++;

    if (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0) == GPIO_PIN_SET) {
        /* Rising: start of active-high pulse only (ignore low-time). */
        s_edge_mark = now;
        s_have_rise = true;
    } else if (s_have_rise) {
        /* Falling: end of active-high pulse. */
        width = capture_delta(s_edge_mark, now);
        accept_width(width);
        s_have_rise = false;
    }
}
