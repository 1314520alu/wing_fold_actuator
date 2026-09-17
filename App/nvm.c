#include "nvm.h"

#include <stddef.h>
#include <string.h>

#include "main.h"
#include "nvm_layout.h"

#ifdef NVM_HOST_TEST
extern uint8_t nvm_test_flash_page[NVM_FLASH_PAGE_SIZE];
#define NVM_FLASH_DATA  nvm_test_flash_page
#else
#define NVM_FLASH_DATA  ((const uint8_t *)NVM_FLASH_PAGE_ADDRESS)
#endif

_Static_assert((sizeof(nvm_blob_t) % 2U) == 0U,
               "nvm_blob_t must be half-word aligned");
_Static_assert(sizeof(nvm_blob_t) <= NVM_FLASH_PAGE_SIZE,
               "nvm_blob_t exceeds reserved Flash page");

static uint32_t crc32(const uint8_t *data, size_t length)
{
    uint32_t crc = 0xFFFFFFFFU;

    for (size_t i = 0U; i < length; ++i) {
        crc ^= data[i];
        for (uint8_t bit = 0U; bit < 8U; ++bit) {
            const uint32_t mask = (uint32_t)-(int32_t)(crc & 1U);
            crc = (crc >> 1U) ^ (0xEDB88320U & mask);
        }
    }
    return ~crc;
}

static uint32_t blob_crc(const nvm_blob_t *blob)
{
    return crc32((const uint8_t *)blob, offsetof(nvm_blob_t, crc32));
}

void nvm_defaults(nvm_blob_t *out)
{
    if (out == NULL) {
        return;
    }

    memset(out, 0, sizeof(*out));
    out->magic = NVM_MAGIC;
    out->version = NVM_VERSION;
    out->count_a = 0;
    out->count_b = 24000;
    out->pwm_min_us = 1000U;
    out->pwm_max_us = 2000U;
    out->deadzone = 150;
    out->kp = 1500;
    /* Host units shared with上位机: 100 ≈ full useful speed. */
    out->vmax = 100;
    out->cruise_err = 800;
    out->crc32 = blob_crc(out);
}

bool nvm_load(nvm_blob_t *out)
{
    nvm_blob_t stored;

    if (out == NULL) {
        return false;
    }

    memcpy(&stored, NVM_FLASH_DATA, sizeof(stored));
    if ((stored.magic != NVM_MAGIC)
        || (stored.version != NVM_VERSION)
        || (stored.crc32 != blob_crc(&stored))) {
        return false;
    }

    *out = stored;
    return true;
}

bool nvm_save(const nvm_blob_t *in)
{
    nvm_blob_t stored;
    FLASH_EraseInitTypeDef erase = {0};
    uint32_t page_error = 0U;
    HAL_StatusTypeDef status;
    const uint8_t *bytes;

    if (in == NULL) {
        return false;
    }

    stored = *in;
    stored.magic = NVM_MAGIC;
    stored.version = NVM_VERSION;
    stored.crc32 = blob_crc(&stored);

    if (HAL_FLASH_Unlock() != HAL_OK) {
        return false;
    }

    erase.TypeErase = FLASH_TYPEERASE_PAGES;
    erase.PageAddress = NVM_FLASH_PAGE_ADDRESS;
    erase.NbPages = 1U;
    status = HAL_FLASHEx_Erase(&erase, &page_error);

    bytes = (const uint8_t *)&stored;
    if (status == HAL_OK) {
        for (uint32_t offset = 0U; offset < sizeof(stored); offset += 2U) {
            const uint16_t halfword = (uint16_t)bytes[offset]
                                    | (uint16_t)((uint16_t)bytes[offset + 1U]
                                                 << 8U);
            status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD,
                                       NVM_FLASH_PAGE_ADDRESS + offset,
                                       halfword);
            if (status != HAL_OK) {
                break;
            }
        }
    }

    (void)HAL_FLASH_Lock();
    return (status == HAL_OK)
        && (memcmp(NVM_FLASH_DATA, &stored, sizeof(stored)) == 0);
}
