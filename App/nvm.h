#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t magic;
    uint32_t version;
    int32_t count_a;
    int32_t count_b;
    uint16_t pwm_min_us;
    uint16_t pwm_max_us;
    int32_t deadzone;
    int32_t kp;
    int32_t vmax;
    int32_t cruise_err;
    uint32_t crc32;
} nvm_blob_t;

void nvm_defaults(nvm_blob_t *out);
bool nvm_load(nvm_blob_t *out);
bool nvm_save(const nvm_blob_t *in);
