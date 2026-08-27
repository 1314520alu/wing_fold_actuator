#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "main.h"
#include "../../App/nvm.h"
#include "../../App/nvm_layout.h"

uint8_t nvm_test_flash_page[NVM_FLASH_PAGE_SIZE];

static HAL_StatusTypeDef unlock_status;
static HAL_StatusTypeDef erase_status;
static HAL_StatusTypeDef program_status;
static unsigned int lock_calls;

HAL_StatusTypeDef HAL_FLASH_Unlock(void)
{
    return unlock_status;
}

HAL_StatusTypeDef HAL_FLASH_Lock(void)
{
    ++lock_calls;
    return HAL_OK;
}

HAL_StatusTypeDef HAL_FLASHEx_Erase(FLASH_EraseInitTypeDef *erase,
                                    uint32_t *page_error)
{
    assert(erase->TypeErase == FLASH_TYPEERASE_PAGES);
    assert(erase->PageAddress == NVM_FLASH_PAGE_ADDRESS);
    assert(erase->NbPages == 1U);
    if (erase_status == HAL_OK) {
        memset(nvm_test_flash_page, 0xFF, sizeof(nvm_test_flash_page));
    } else {
        *page_error = NVM_FLASH_PAGE_ADDRESS;
    }
    return erase_status;
}

HAL_StatusTypeDef HAL_FLASH_Program(uint32_t type,
                                    uint32_t address,
                                    uint64_t data)
{
    assert(type == FLASH_TYPEPROGRAM_HALFWORD);
    assert(address >= NVM_FLASH_PAGE_ADDRESS);
    assert(address + 2U <= NVM_FLASH_PAGE_ADDRESS + NVM_FLASH_PAGE_SIZE);
    if (program_status == HAL_OK) {
        const uint32_t offset = address - NVM_FLASH_PAGE_ADDRESS;
        nvm_test_flash_page[offset] = (uint8_t)data;
        nvm_test_flash_page[offset + 1U] = (uint8_t)(data >> 8U);
    }
    return program_status;
}

static nvm_blob_t sample_blob(void)
{
    nvm_blob_t blob;

    nvm_defaults(&blob);
    blob.count_a = -1234;
    blob.count_b = 98765;
    return blob;
}

static void reset_fixture(void)
{
    memset(nvm_test_flash_page, 0xFF, sizeof(nvm_test_flash_page));
    unlock_status = HAL_OK;
    erase_status = HAL_OK;
    program_status = HAL_OK;
    lock_calls = 0U;
}

static void test_defaults_require_endpoint_calibration(void)
{
    nvm_blob_t blob;

    nvm_defaults(&blob);
    assert(blob.magic == NVM_MAGIC);
    assert(blob.version == NVM_VERSION);
    assert(blob.count_a == 0);
    assert(blob.count_b == 24000);
    assert(blob.pwm_min_us == 1000U);
    assert(blob.pwm_max_us == 2000U);
    assert(blob.deadzone == 150);
    assert(blob.kp == 1500);
    assert(blob.vmax == 1000);
    assert(blob.cruise_err == 800);
}

static void test_erased_or_corrupt_flash_is_rejected(void)
{
    nvm_blob_t out;
    nvm_blob_t blob = sample_blob();

    reset_fixture();
    assert(!nvm_load(&out));
    assert(nvm_save(&blob));
    nvm_test_flash_page[8] ^= 0x01U;
    assert(!nvm_load(&out));
}

static void test_save_round_trip_adds_header_and_crc(void)
{
    nvm_blob_t in = sample_blob();
    nvm_blob_t out;

    reset_fixture();
    in.magic = 0U;
    in.version = 0U;
    in.crc32 = 0U;
    assert(nvm_save(&in));
    assert(lock_calls == 1U);
    assert(nvm_load(&out));
    assert(out.magic == NVM_MAGIC);
    assert(out.version == NVM_VERSION);
    assert(out.count_a == in.count_a);
    assert(out.count_b == in.count_b);
    assert(out.crc32 != 0U);
}

static void test_flash_failures_are_reported_and_flash_is_locked(void)
{
    nvm_blob_t blob = sample_blob();

    reset_fixture();
    unlock_status = HAL_ERROR;
    assert(!nvm_save(&blob));
    assert(lock_calls == 0U);

    reset_fixture();
    erase_status = HAL_ERROR;
    assert(!nvm_save(&blob));
    assert(lock_calls == 1U);

    reset_fixture();
    program_status = HAL_ERROR;
    assert(!nvm_save(&blob));
    assert(lock_calls == 1U);
}

int main(void)
{
    test_defaults_require_endpoint_calibration();
    test_erased_or_corrupt_flash_is_rejected();
    test_save_round_trip_adds_header_and_crc();
    test_flash_failures_are_reported_and_flash_is_locked();
    printf("OK\n");
    return 0;
}
