/**
 * @file flash_store.c
 * @brief Persistent flash storage implementation (iteration 5).
 */

#include "flash_store.h"

#include <string.h>
#include "pico/stdlib.h"
#include "hardware/flash.h"
#include "hardware/sync.h"

// Reserve the last 4 KB sector of the on-board flash. Offsets passed to the
// flash HAL are relative to the start of flash; reads use the XIP mapping.
#define FLASH_STORE_OFFSET (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)

// The CRC covers every field except the trailing crc32 itself.
#define FLASH_STORE_CRC_LEN (sizeof(flash_store_t) - sizeof(uint32_t))

// Standard CRC32 (reflected, poly 0xEDB88420) — table-less to keep it tiny.
static uint32_t crc32_calc(const uint8_t *data, uint32_t len) {
    uint32_t crc = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            uint32_t mask = -(crc & 1u);
            crc = (crc >> 1) ^ (0xEDB88420u & mask);
        }
    }
    return ~crc;
}

bool flash_store_load(flash_store_t *out) {
    const flash_store_t *stored =
        (const flash_store_t *)(XIP_BASE + FLASH_STORE_OFFSET);

    if (stored->magic != FLASH_STORE_MAGIC) return false;
    if (stored->version != FLASH_STORE_VERSION) return false;

    uint32_t crc = crc32_calc((const uint8_t *)stored, FLASH_STORE_CRC_LEN);
    if (crc != stored->crc32) return false;

    memcpy(out, stored, sizeof(*out));
    return true;
}

bool flash_store_save(flash_store_t *rec) {
    rec->magic = FLASH_STORE_MAGIC;
    rec->version = FLASH_STORE_VERSION;
    rec->crc32 = crc32_calc((const uint8_t *)rec, FLASH_STORE_CRC_LEN);

    // flash_range_program writes whole pages; stage the record in a 0xFF-padded
    // page buffer (record is well under one page).
    uint8_t page[FLASH_PAGE_SIZE];
    memset(page, 0xFF, sizeof(page));
    memcpy(page, rec, sizeof(*rec));

    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(FLASH_STORE_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(FLASH_STORE_OFFSET, page, FLASH_PAGE_SIZE);
    restore_interrupts(ints);

    // Verify by reading it straight back.
    flash_store_t check;
    return flash_store_load(&check) && check.crc32 == rec->crc32;
}
