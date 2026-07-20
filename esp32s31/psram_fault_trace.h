#ifndef GPSP_ESP32S31_PSRAM_FAULT_TRACE_H
#define GPSP_ESP32S31_PSRAM_FAULT_TRACE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The RV32 backend owns REG_USERDEF..REG_USERDEF+RISCV_HELPER_COUNT-1.
 * These final two machine-defined slots are reserved by the diagnostic build
 * for the most recent validated JIT lookup. The fast indirect tail writes
 * them immediately before jumping to a cached entry. */
#define ESP32S31_PSRAM_FAULT_TRACE_REG_LOOKUP_ENTRY 62u
#define ESP32S31_PSRAM_FAULT_TRACE_REG_LOOKUP_KEY 63u
#define ESP32S31_PSRAM_FAULT_TRACE_FAST_INDIRECT_BIT UINT32_C(0x80000000)

typedef enum esp32s31_psram_fault_cache_kind
{
  ESP32S31_PSRAM_FAULT_CACHE_ROM = 1,
  ESP32S31_PSRAM_FAULT_CACHE_RAM = 2
} esp32s31_psram_fault_cache_kind_t;

typedef enum esp32s31_psram_fault_sync_stage
{
  ESP32S31_PSRAM_FAULT_SYNC_IDLE = 0,
  ESP32S31_PSRAM_FAULT_SYNC_BEGIN = 1,
  ESP32S31_PSRAM_FAULT_SYNC_DATA_WRITEBACK = 2,
  ESP32S31_PSRAM_FAULT_SYNC_DATA_DONE = 3,
  ESP32S31_PSRAM_FAULT_SYNC_INST_INVALIDATE = 4,
  ESP32S31_PSRAM_FAULT_SYNC_INST_DONE = 5,
  ESP32S31_PSRAM_FAULT_SYNC_FENCE = 6,
  ESP32S31_PSRAM_FAULT_SYNC_DONE = 7,
  ESP32S31_PSRAM_FAULT_SYNC_BAD_RANGE = 8,
  ESP32S31_PSRAM_FAULT_SYNC_DATA_ERROR = 9,
  ESP32S31_PSRAM_FAULT_SYNC_INST_ERROR = 10
} esp32s31_psram_fault_sync_stage_t;

typedef struct esp32s31_psram_fault_indirect_snapshot
{
  uint32_t requested_key;
  uint32_t global_generation;
  uint32_t slot_key;
  uint32_t slot_entry;
  uint32_t slot_generation;
} esp32s31_psram_fault_indirect_snapshot_t;

bool esp32s31_psram_fault_trace_init(void);

void esp32s31_psram_fault_trace_note_cache_sync(
    esp32s31_psram_fault_sync_stage_t stage,
    uintptr_t start, uintptr_t end, int32_t result);

void esp32s31_psram_fault_trace_note_cache_flush(
    esp32s31_psram_fault_cache_kind_t kind,
    uintptr_t old_pointer, uintptr_t new_pointer);

void esp32s31_psram_fault_trace_note_gamepak_page(
    uint32_t logical_page, const void *page);

void esp32s31_psram_fault_trace_note_jit_lookup(
    uint32_t key, const void *entry);

/* Implemented by riscv_runtime.c so the ISR can inspect its otherwise-private
 * lookup cache without exposing that backend data structure globally. */
void esp32s31_psram_fault_trace_get_indirect_snapshot(
    uint32_t key, esp32s31_psram_fault_indirect_snapshot_t *snapshot);

#ifdef __cplusplus
}
#endif

#endif
