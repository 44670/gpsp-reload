#include "esp32s31/psram_fault_trace.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "sdkconfig.h"

#include "esp_attr.h"
#include "esp_err.h"
#include "esp_intr_alloc.h"
#include "esp_log.h"
#include "esp_private/esp_psram_mspi.h"
#include "hal/mspi_periph.h"
#include "hal/psram_ctrlr_ll.h"
#include "riscv/rvruntime-frames.h"
#include "soc/soc.h"
#include "soc/spi_mem_s_reg.h"

#include "common.h"
#include "cpu.h"
#include "gba_memory.h"

#if !defined(CONFIG_IDF_TARGET_ESP32S31)
#error "PSRAM fault tracing is specific to ESP32-S31"
#endif

#if defined(MSPI_LL_INTR_SHARED) && MSPI_LL_INTR_SHARED
#error "ESP32-S31 PSRAM fault tracing requires the dedicated PSRAM MSPI IRQ"
#endif

#define TRACE_GAMEPAK_PAGES 1024u
#define TRACE_GAMEPAK_PAGE_BYTES 0x8000u
#define TRACE_AXI_ADDRESS_MASK SPI_MEM_S_AXI_ERR_ADDR_V

typedef struct esp32s31_psram_fault_trace_state
{
  volatile uint32_t sync_sequence;
  volatile uint32_t sync_active;
  volatile uint32_t sync_stage;
  volatile uint32_t sync_start;
  volatile uint32_t sync_end;
  volatile int32_t sync_result;

  volatile uint32_t flush_sequence;
  volatile uint32_t flush_kind;
  volatile uint32_t flush_old_pointer;
  volatile uint32_t flush_new_pointer;
  volatile uint32_t rom_epoch;
  volatile uint32_t ram_epoch;

  volatile uint32_t gamepak_page[TRACE_GAMEPAK_PAGES];
} esp32s31_psram_fault_trace_state_t;

typedef struct esp32s31_psram_fault_page_match
{
  uint32_t found;
  uint32_t logical_page;
  uint32_t page_pointer;
  uint32_t page_offset;
  uint32_t aliases;
} esp32s31_psram_fault_page_match_t;

enum
{
  TRACE_ADDRESS_UNKNOWN = 0,
  TRACE_ADDRESS_ROM_JIT = 1,
  TRACE_ADDRESS_RAM_JIT = 2,
  TRACE_ADDRESS_GAMEPAK = 3,
  TRACE_ADDRESS_NON_JIT = 4
};

static DRAM_ATTR volatile esp32s31_psram_fault_trace_state_t s_trace;
static DRAM_ATTR intr_handle_t s_trace_interrupt;

/* rtos_int_enter stores the interrupted task's RvExcFrame pointer in the
 * first TCB word before switching to the ISR stack. Both the upstream and IDF
 * FreeRTOS TCB layouts intentionally keep pxTopOfStack as their first field. */
extern void *volatile pxCurrentTCBs[];
extern volatile uint32_t port_uxInterruptNesting[];

ESP_LOG_ATTR_TAG_DRAM(s_fault_tag, "gpsp-psram-trace");
static const char *TAG = "gpsp-psram-trace";

_Static_assert(sizeof(uintptr_t) == sizeof(uint32_t),
               "ESP32-S31 fault trace assumes a 32-bit address space");
_Static_assert(ESP32S31_PSRAM_FAULT_TRACE_REG_LOOKUP_KEY < REG_MAX,
               "fault lookup key slot is outside the CPU state");
_Static_assert(ESP32S31_PSRAM_FAULT_TRACE_REG_LOOKUP_ENTRY < REG_MAX,
               "fault lookup entry slot is outside the CPU state");
void esp32s31_psram_fault_isr_c(void *argument)
    __attribute__((noreturn, noinline, used));

static const RvExcFrame *IRAM_ATTR trace_interrupted_frame(void)
{
  void *const current_tcb = pxCurrentTCBs[0];

  if (current_tcb == NULL || port_uxInterruptNesting[0] != 1u)
    return NULL;
  return *(RvExcFrame *volatile const *)current_tcb;
}

static uint32_t IRAM_ATTR trace_address_class(uint32_t address)
{
  const uint32_t rom_start = (uint32_t)(uintptr_t)rom_translation_cache;
  const uint32_t ram_start = (uint32_t)(uintptr_t)ram_translation_cache;

  if (address >= rom_start &&
      address - rom_start < (uint32_t)ROM_TRANSLATION_CACHE_SIZE)
    return TRACE_ADDRESS_ROM_JIT;
  if (address >= ram_start &&
      address - ram_start < (uint32_t)RAM_TRANSLATION_CACHE_SIZE)
    return TRACE_ADDRESS_RAM_JIT;
  return TRACE_ADDRESS_NON_JIT;
}

static uint32_t IRAM_ATTR trace_raw_jit_class(uint32_t raw_address)
{
  const uint32_t rom_start =
      (uint32_t)(uintptr_t)rom_translation_cache & TRACE_AXI_ADDRESS_MASK;
  const uint32_t ram_start =
      (uint32_t)(uintptr_t)ram_translation_cache & TRACE_AXI_ADDRESS_MASK;

  if (raw_address >= rom_start &&
      raw_address - rom_start < (uint32_t)ROM_TRANSLATION_CACHE_SIZE)
    return TRACE_ADDRESS_ROM_JIT;
  if (raw_address >= ram_start &&
      raw_address - ram_start < (uint32_t)RAM_TRANSLATION_CACHE_SIZE)
    return TRACE_ADDRESS_RAM_JIT;
  return TRACE_ADDRESS_UNKNOWN;
}

static void IRAM_ATTR trace_find_gamepak_page(
    uint32_t raw_address, esp32s31_psram_fault_page_match_t *match)
{
  uint32_t page;

  match->found = 0u;
  match->logical_page = UINT32_MAX;
  match->page_pointer = 0u;
  match->page_offset = UINT32_MAX;
  match->aliases = 0u;

  for (page = 0; page < TRACE_GAMEPAK_PAGES; page++)
  {
    const uint32_t pointer = s_trace.gamepak_page[page];
    const uint32_t base = pointer & TRACE_AXI_ADDRESS_MASK;

    if (pointer != 0u && raw_address >= base &&
        raw_address - base < TRACE_GAMEPAK_PAGE_BYTES)
    {
      if (!match->found)
      {
        match->found = 1u;
        match->logical_page = page;
        match->page_pointer = pointer;
        match->page_offset = raw_address - base;
      }
      match->aliases++;
    }
  }
}

void IRAM_ATTR esp32s31_psram_fault_isr_c(void *argument)
{
  esp32s31_psram_fault_indirect_snapshot_t indirect = {0};
  esp32s31_psram_fault_page_match_t page_match;
  uint32_t csr_mcause;
  uint32_t csr_mtval;
  uint32_t csr_mstatus;
  uint32_t csr_mepc;
  const uint32_t intr_events =
      psram_ctrlr_ll_get_intr_raw(PSRAM_CTRLR_LL_MSPI_ID_SYSTEM);
  const uint32_t axi_raw =
      (REG_READ(SPI_MEM_S_AXI_ERR_ADDR_REG) >>
       SPI_MEM_S_AXI_ERR_ADDR_S) & SPI_MEM_S_AXI_ERR_ADDR_V;
  const RvExcFrame *const frame = trace_interrupted_frame();

  (void)argument;

  __asm__ __volatile__("csrr %0, mepc" : "=r"(csr_mepc));
  __asm__ __volatile__("csrr %0, mcause" : "=r"(csr_mcause));
  __asm__ __volatile__("csrr %0, mtval" : "=r"(csr_mtval));
  __asm__ __volatile__("csrr %0, mstatus" : "=r"(csr_mstatus));

  /* Reading AXI_ERR_ADDR must precede this clear: the hardware clears that
   * register together with AXI_RADDR_ERR, which is why the stock IDF ISR
   * cannot report the failing address. */
  psram_ctrlr_ll_clear_intr(PSRAM_CTRLR_LL_MSPI_ID_SYSTEM, intr_events);

  const uint32_t mepc = frame != NULL ? (uint32_t)frame->mepc : csr_mepc;
  const uint32_t lookup_trace_key =
      reg[ESP32S31_PSRAM_FAULT_TRACE_REG_LOOKUP_KEY];
  const uint32_t lookup_fast =
      (lookup_trace_key &
       ESP32S31_PSRAM_FAULT_TRACE_FAST_INDIRECT_BIT) != 0u;
  const uint32_t lookup_key =
      lookup_trace_key & ~ESP32S31_PSRAM_FAULT_TRACE_FAST_INDIRECT_BIT;
  const uint32_t lookup_entry =
      reg[ESP32S31_PSRAM_FAULT_TRACE_REG_LOOKUP_ENTRY];
  const uint32_t axi_cpu_address = axi_raw | UINT32_C(0x40000000);
  uint32_t axi_class = trace_raw_jit_class(axi_raw);
  const uint32_t mepc_class = trace_address_class(mepc);
  uint32_t entry_delta = UINT32_MAX;

  trace_find_gamepak_page(axi_raw, &page_match);
  if (page_match.found)
    axi_class = TRACE_ADDRESS_GAMEPAK;
  if (lookup_entry != 0u && mepc >= lookup_entry)
    entry_delta = mepc - lookup_entry;

  esp32s31_psram_fault_trace_get_indirect_snapshot(
      lookup_key, &indirect);

  ESP_DRAM_LOGE(
      s_fault_tag,
      "psram_fault intr=0x%08x axi_raw=0x%08x axi_cpu=0x%08x "
      "axi_class=%u mepc=0x%08x mepc_class=%u mcause=0x%08x "
      "mtval=0x%08x mstatus=0x%08x nesting=%u frame=0x%08x",
      (unsigned)intr_events, (unsigned)axi_raw,
      (unsigned)axi_cpu_address, (unsigned)axi_class,
      (unsigned)mepc, (unsigned)mepc_class,
      (unsigned)csr_mcause, (unsigned)csr_mtval, (unsigned)csr_mstatus,
      (unsigned)port_uxInterruptNesting[0], (unsigned)(uintptr_t)frame);
  ESP_DRAM_LOGE(
      s_fault_tag,
      "psram_fault_guest reg_pc=0x%08x cpsr=0x%08x reg_sp=0x%08x "
      "reg_lr=0x%08x host_s0=0x%08x mapped_r5=0x%08x "
      "mapped_r6=0x%08x mapped_r7=0x%08x mapped_r8=0x%08x",
      (unsigned)reg[REG_PC], (unsigned)reg[REG_CPSR],
      (unsigned)reg[REG_SP], (unsigned)reg[REG_LR],
      frame != NULL ? (unsigned)frame->s0 : 0u,
      frame != NULL ? (unsigned)frame->s1 : 0u,
      frame != NULL ? (unsigned)frame->s2 : 0u,
      frame != NULL ? (unsigned)frame->s3 : 0u,
      frame != NULL ? (unsigned)frame->s4 : 0u);
  ESP_DRAM_LOGE(
      s_fault_tag,
      "psram_fault_guest2 mapped_r9=0x%08x mapped_r10=0x%08x "
      "mapped_r11=0x%08x mapped_r12=0x%08x mapped_sp=0x%08x "
      "mapped_lr=0x%08x mapped_mode=0x%08x",
      frame != NULL ? (unsigned)frame->s5 : 0u,
      frame != NULL ? (unsigned)frame->s6 : 0u,
      frame != NULL ? (unsigned)frame->s7 : 0u,
      frame != NULL ? (unsigned)frame->s8 : 0u,
      frame != NULL ? (unsigned)frame->s9 : 0u,
      frame != NULL ? (unsigned)frame->s10 : 0u,
      frame != NULL ? (unsigned)frame->s11 : 0u);
  ESP_DRAM_LOGE(
      s_fault_tag,
      "psram_fault_guest0 mapped_r0=0x%08x mapped_r1=0x%08x "
      "mapped_r2=0x%08x mapped_r3=0x%08x mapped_r4=0x%08x "
      "host_a0=0x%08x host_a1=0x%08x host_a2=0x%08x",
      frame != NULL ? (unsigned)frame->a3 : 0u,
      frame != NULL ? (unsigned)frame->a4 : 0u,
      frame != NULL ? (unsigned)frame->a5 : 0u,
      frame != NULL ? (unsigned)frame->a6 : 0u,
      frame != NULL ? (unsigned)frame->a7 : 0u,
      frame != NULL ? (unsigned)frame->a0 : 0u,
      frame != NULL ? (unsigned)frame->a1 : 0u,
      frame != NULL ? (unsigned)frame->a2 : 0u);
  ESP_DRAM_LOGE(
      s_fault_tag,
      "psram_fault_temps t0=0x%08x t1=0x%08x t2=0x%08x t3=0x%08x "
      "t4=0x%08x t5=0x%08x t6=0x%08x interrupted_sp=0x%08x",
      frame != NULL ? (unsigned)frame->t0 : 0u,
      frame != NULL ? (unsigned)frame->t1 : 0u,
      frame != NULL ? (unsigned)frame->t2 : 0u,
      frame != NULL ? (unsigned)frame->t3 : 0u,
      frame != NULL ? (unsigned)frame->t4 : 0u,
      frame != NULL ? (unsigned)frame->t5 : 0u,
      frame != NULL ? (unsigned)frame->t6 : 0u,
      frame != NULL ? (unsigned)frame->sp : 0u);
  ESP_DRAM_LOGE(
      s_fault_tag,
      "psram_fault_jit lookup_fast=%u lookup_key=0x%08x "
      "lookup_entry=0x%08x "
      "mepc_entry_delta=0x%08x rom_ptr=0x%08x ram_ptr=0x%08x "
      "rom_epoch=%u ram_epoch=%u global_gen=%u slot_key=0x%08x "
      "slot_entry=0x%08x slot_gen=%u",
      (unsigned)lookup_fast, (unsigned)lookup_key, (unsigned)lookup_entry,
      (unsigned)entry_delta, (unsigned)(uintptr_t)rom_translation_ptr,
      (unsigned)(uintptr_t)ram_translation_ptr,
      (unsigned)s_trace.rom_epoch, (unsigned)s_trace.ram_epoch,
      (unsigned)indirect.global_generation, (unsigned)indirect.slot_key,
      (unsigned)indirect.slot_entry, (unsigned)indirect.slot_generation);
  ESP_DRAM_LOGE(
      s_fault_tag,
      "psram_fault_sync active=%u seq=%u stage=%u start=0x%08x "
      "end=0x%08x result=%d flush_seq=%u flush_kind=%u "
      "flush_old=0x%08x flush_new=0x%08x",
      (unsigned)s_trace.sync_active, (unsigned)s_trace.sync_sequence,
      (unsigned)s_trace.sync_stage, (unsigned)s_trace.sync_start,
      (unsigned)s_trace.sync_end, (int)s_trace.sync_result,
      (unsigned)s_trace.flush_sequence, (unsigned)s_trace.flush_kind,
      (unsigned)s_trace.flush_old_pointer,
      (unsigned)s_trace.flush_new_pointer);
  ESP_DRAM_LOGE(
      s_fault_tag,
      "psram_fault_gamepak found=%u logical_page=%u page_ptr=0x%08x "
      "page_offset=0x%08x aliases=%u",
      (unsigned)page_match.found, (unsigned)page_match.logical_page,
      (unsigned)page_match.page_pointer, (unsigned)page_match.page_offset,
      (unsigned)page_match.aliases);
  ESP_DRAM_LOGE(s_fault_tag, "psram_fault_abort=1");

  abort();
  __builtin_unreachable();
}

bool esp32s31_psram_fault_trace_init(void)
{
  esp_err_t error;

  memset((void *)&s_trace, 0, sizeof(s_trace));
  reg[ESP32S31_PSRAM_FAULT_TRACE_REG_LOOKUP_ENTRY] = 0u;
  reg[ESP32S31_PSRAM_FAULT_TRACE_REG_LOOKUP_KEY] = 0u;

  psram_ctrlr_ll_enable_intr(PSRAM_CTRLR_LL_MSPI_ID_SYSTEM,
                             PSRAM_CTRLR_LL_EVENT_MASK, false);
  error = esp_psram_mspi_unregister_isr();
  if (error != ESP_OK)
  {
    psram_ctrlr_ll_enable_intr(PSRAM_CTRLR_LL_MSPI_ID_SYSTEM,
                               PSRAM_CTRLR_LL_EVENT_MASK, true);
    ESP_LOGE(TAG, "cannot replace default PSRAM ISR: %s",
             esp_err_to_name(error));
    return false;
  }

  error = esp_intr_alloc(
      mspi_hw_info.instances[PSRAM_CTRLR_LL_MSPI_ID_SYSTEM].irq,
      ESP_INTR_FLAG_IRAM, esp32s31_psram_fault_isr_c, NULL,
      &s_trace_interrupt);
  if (error != ESP_OK)
  {
    const esp_err_t restore_error = esp_psram_mspi_register_isr();
    ESP_LOGE(TAG,
             "cannot install trace PSRAM ISR: %s; default_restore=%s",
             esp_err_to_name(error), esp_err_to_name(restore_error));
    return false;
  }

  psram_ctrlr_ll_clear_intr(PSRAM_CTRLR_LL_MSPI_ID_SYSTEM,
                            PSRAM_CTRLR_LL_EVENT_MASK);
  psram_ctrlr_ll_enable_intr(PSRAM_CTRLR_LL_MSPI_ID_SYSTEM,
                             PSRAM_CTRLR_LL_EVENT_MASK, true);
  ESP_LOGI(TAG, "installed pre-clear PSRAM AXI/JIT fault tracer");
  return true;
}

void esp32s31_psram_fault_trace_note_cache_sync(
    esp32s31_psram_fault_sync_stage_t stage,
    uintptr_t start, uintptr_t end, int32_t result)
{
  if (stage == ESP32S31_PSRAM_FAULT_SYNC_BEGIN)
  {
    s_trace.sync_sequence++;
    s_trace.sync_active = 1u;
    s_trace.sync_start = (uint32_t)start;
    s_trace.sync_end = (uint32_t)end;
  }

  s_trace.sync_result = result;
  s_trace.sync_stage = (uint32_t)stage;

  if (stage == ESP32S31_PSRAM_FAULT_SYNC_DONE ||
      stage == ESP32S31_PSRAM_FAULT_SYNC_BAD_RANGE ||
      stage == ESP32S31_PSRAM_FAULT_SYNC_DATA_ERROR ||
      stage == ESP32S31_PSRAM_FAULT_SYNC_INST_ERROR)
    s_trace.sync_active = 0u;
}

void esp32s31_psram_fault_trace_note_cache_flush(
    esp32s31_psram_fault_cache_kind_t kind,
    uintptr_t old_pointer, uintptr_t new_pointer)
{
  s_trace.flush_kind = (uint32_t)kind;
  s_trace.flush_old_pointer = (uint32_t)old_pointer;
  s_trace.flush_new_pointer = (uint32_t)new_pointer;
  if (kind == ESP32S31_PSRAM_FAULT_CACHE_ROM)
    s_trace.rom_epoch++;
  else if (kind == ESP32S31_PSRAM_FAULT_CACHE_RAM)
    s_trace.ram_epoch++;
  s_trace.flush_sequence++;
}

void esp32s31_psram_fault_trace_note_gamepak_page(
    uint32_t logical_page, const void *page)
{
  if (logical_page < TRACE_GAMEPAK_PAGES)
    s_trace.gamepak_page[logical_page] = (uint32_t)(uintptr_t)page;
}

void esp32s31_psram_fault_trace_note_jit_lookup(
    uint32_t key, const void *entry)
{
  reg[ESP32S31_PSRAM_FAULT_TRACE_REG_LOOKUP_ENTRY] =
      (uint32_t)(uintptr_t)entry;
  __asm__ __volatile__("" ::: "memory");
  reg[ESP32S31_PSRAM_FAULT_TRACE_REG_LOOKUP_KEY] = key;
}
