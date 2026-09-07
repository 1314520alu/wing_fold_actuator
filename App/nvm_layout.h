#pragma once

#include <stdint.h>

/* STM32F103CB: 128 KiB Flash, 1 KiB pages.  The linker script reserves this
 * final page so application code can never overlap calibration storage. */
#define NVM_FLASH_PAGE_SIZE     1024U
#define NVM_FLASH_PAGE_ADDRESS  0x0801FC00U

#define NVM_MAGIC               0x54464F4CU /* "TFOL" */
#define NVM_VERSION             2U
