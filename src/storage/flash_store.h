/**
 * @file flash_store.h
 * @brief Persistent key storage in the Pico's on-board flash (iteration 5).
 *
 * Reserves the final 4 KB sector of flash for a single, CRC-protected record
 * holding the gas-resistance baseline used by the IAQ calculation. Persisting
 * the baseline means the IAQ index no longer has to re-warm from scratch after
 * every reboot — on boot the stored baseline is restored (subject to a freshness
 * check) so IAQ is stable from the first valid reading.
 */

#ifndef FLASH_STORE_H
#define FLASH_STORE_H

#include <stdint.h>
#include <stdbool.h>

// Identifies a valid record. 'BME6' + iteration tag.
#define FLASH_STORE_MAGIC    0xB6E68005u
#define FLASH_STORE_VERSION  1u

/**
 * Persisted record layout. Kept small and fixed-size; `crc32` must be the last
 * field — it covers every byte that precedes it.
 */
typedef struct {
    uint32_t magic;         // FLASH_STORE_MAGIC
    uint32_t version;       // FLASH_STORE_VERSION
    uint32_t gas_baseline;  // clean-air gas resistance reference, in ohms
    uint32_t saved_epoch;   // unix seconds at save time, 0 if wall-clock unknown
    uint32_t boot_count;    // number of successful saves (wear/telemetry hint)
    uint32_t crc32;         // CRC32 over all preceding fields
} flash_store_t;

/**
 * @brief Load and validate the persisted record.
 * @param out Destination; only written when a valid record is found.
 * @return true if magic, version and CRC all check out.
 */
bool flash_store_load(flash_store_t *out);

/**
 * @brief Erase the sector and program a fresh record.
 *
 * Fills in magic, version and crc32 on @p rec, then writes it. Interrupts are
 * disabled around the erase/program (RP2040 executes in place from flash).
 * Verifies by reading the record back.
 *
 * @param rec Record to persist (magic/version/crc32 are overwritten).
 * @return true if the write verified successfully.
 */
bool flash_store_save(flash_store_t *rec);

#endif // FLASH_STORE_H
