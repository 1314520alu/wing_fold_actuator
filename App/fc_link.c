#include "fc_link.h"

#include "app.h"
#include "cli.h"
#include "mavlink_nvf.h"
#include "nvm.h"

#ifndef FC_LINK_PERIOD_MS
#define FC_LINK_PERIOD_MS  100U
#endif

#ifndef FC_LINK_SYSID
#define FC_LINK_SYSID      1U
#endif

/* MAV_COMP_ID_ONBOARD_COMPUTER */
#ifndef FC_LINK_COMPID
#define FC_LINK_COMPID     191U
#endif

static UART_HandleTypeDef *s_huart;
static uint8_t s_seq;
static uint32_t s_last_ms;
static uint8_t s_tx_buf[MAVLINK_NVF_FRAME_LEN];

static float fold_percent(int32_t count)
{
    const nvm_blob_t *p = cli_get_params();
    int32_t span;
    float pct;

    if (p == NULL) {
        return 0.0f;
    }
    span = p->count_b - p->count_a;
    if (span == 0) {
        return 0.0f;
    }
    pct = (100.0f * (float)(count - p->count_a)) / (float)span;
    if (pct < 0.0f) {
        pct = 0.0f;
    } else if (pct > 100.0f) {
        pct = 100.0f;
    }
    return pct;
}

static void send_nvf(uint32_t now_ms, float value, const char *name)
{
    size_t n;

    if ((s_huart == NULL) || (s_huart->gState != HAL_UART_STATE_READY)) {
        return;
    }
    n = mavlink_nvf_pack(s_tx_buf, sizeof(s_tx_buf), s_seq++, FC_LINK_SYSID,
                         FC_LINK_COMPID, now_ms, value, name);
    if (n == 0U) {
        return;
    }
    (void)HAL_UART_Transmit_IT(s_huart, s_tx_buf, (uint16_t)n);
}

void fc_link_init(UART_HandleTypeDef *huart)
{
    s_huart = huart;
    s_seq = 0U;
    s_last_ms = 0U;
    if (s_huart == NULL) {
        return;
    }
    /* TX-complete via HAL_UART_IRQHandler (same IRQ as legacy CLI RX). */
    HAL_NVIC_SetPriority(USART3_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(USART3_IRQn);
}

void fc_link_tick(uint32_t now_ms)
{
    app_status_t st;
    static uint8_t phase;

    if (s_huart == NULL) {
        return;
    }
    if ((uint32_t)(now_ms - s_last_ms) < FC_LINK_PERIOD_MS) {
        return;
    }
    /* One named float per period so TX IT buffer is never overwritten. */
    if (s_huart->gState != HAL_UART_STATE_READY) {
        return;
    }
    s_last_ms = now_ms;
    app_get_status(&st);

    switch (phase % 5U) {
    case 0U:
        send_nvf(now_ms, fold_percent(st.current), "fold_pct");
        break;
    case 1U:
        send_nvf(now_ms, (float)st.current, "fold_cnt");
        break;
    case 2U:
        send_nvf(now_ms, st.servo_fault ? 1.0f : 0.0f, "fold_flt");
        break;
    case 3U:
        send_nvf(now_ms, (float)st.pwm_us, "fold_pwm");
        break;
    default:
        send_nvf(now_ms, st.hold ? 1.0f : 0.0f, "fold_hld");
        break;
    }
    ++phase;
}
