#include "uart_debug.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <libretro.h>

#include "common.h"
#include "cpu.h"
#include "driver/uart_vfs.h"
#include "gba_memory.h"
#include "riscv/riscv_emit.h"
#include "rgb565_scale3x.h"
#include "sdkconfig.h"
#include "video.h"

#define UART_DEBUG_LINE_BYTES 128u
#define UART_DEBUG_TRACE_CAPACITY 256u
#define UART_DEBUG_ROM_WRITE_CAPACITY 64u
#define UART_DEBUG_MEM_MAX_BYTES 256u
#define UART_DEBUG_JIT_MAX_WORDS 256u
#define UART_DEBUG_FRAME_STEP_MAX 600u
#define UART_DEBUG_INPUT_FRAME_MAX 3600u
#define UART_DEBUG_MACRO_CAPACITY 512u
#define UART_DEBUG_MACRO_DELAY_MAX 36000u
#define U32_ARG(value) ((uint32_t)(value))

typedef enum
{
  UART_TRACE_BLOCK = 1,
  UART_TRACE_FALLBACK = 2,
} uart_trace_kind_t;

typedef struct
{
  uint32_t kind;
  uint32_t pc;
  uint32_t end_or_lookup;
  uint32_t next_pc_or_cycles;
  uint32_t cpsr_or_fallback;
  uint32_t thumb;
} uart_trace_entry_t;

typedef struct
{
  uint32_t pc;
  uint32_t address;
  uint32_t value;
  uint32_t bits;
} uart_rom_write_entry_t;

typedef struct
{
  uint32_t frame;
  uint16_t mask;
  uint16_t reserved;
} uart_macro_event_t;

typedef struct
{
  uint32_t regs[16];
  uint32_t cpsr;
  uint32_t mode;
  uint32_t next_pc;
  uint32_t irq_sp;
  uint32_t irq_lr;
  uint32_t irq_spsr;
  uint32_t stack_base;
  uint32_t stack_words[8];
  uint32_t active_index;
  uint32_t object_base;
  uint32_t object_value;
  uint32_t r2_value;
  uint32_t r2_object_value;
  uint32_t r2_object_valid;
  riscv_runtime_debug_branch_probe branch_probe;
} uart_break_snapshot_t;

static char g_line[UART_DEBUG_LINE_BYTES];
static size_t g_line_length;
static bool g_line_overflow;
static bool g_paused;
static uint32_t g_frame_budget;
static bool g_report_frame_step_complete;
static uint32_t g_completed_frames;
static uint16_t g_injected_joypad_mask;
static uint32_t g_injected_joypad_frames;
static bool g_macro_recording;
static bool g_macro_replaying;
static bool g_macro_have_mask;
static bool g_macro_overflow;
static uint32_t g_macro_record_start_frame;
static uint32_t g_macro_replay_start_frame;
static uint32_t g_macro_replay_index;
static uint32_t g_macro_event_count;
static uint16_t g_macro_last_mask;
static uint16_t g_macro_replay_mask;
static uart_macro_event_t g_macro_events[UART_DEBUG_MACRO_CAPACITY];

static uart_trace_entry_t g_trace[UART_DEBUG_TRACE_CAPACITY];
static volatile uint32_t g_trace_count;
static volatile uint32_t g_trace_remaining;
static volatile bool g_trace_active;
static volatile bool g_trace_complete_pending;
static uint32_t g_trace_range_start;
static uint32_t g_trace_range_end;

static uart_rom_write_entry_t
    g_rom_writes[UART_DEBUG_ROM_WRITE_CAPACITY];
static volatile uint32_t g_rom_write_count;
static volatile uint32_t g_rom_write_total;
static volatile bool g_rom_write_capture;
static volatile bool g_break_active;
static volatile bool g_break_complete_pending;
static uint32_t g_break_pc;
static uint32_t g_break_probe_pc;
static bool g_break_only_bad_next;
static uint32_t g_break_expected_next;
static uart_break_snapshot_t g_break_snapshot;
static uart_trace_entry_t g_break_trace[UART_DEBUG_TRACE_CAPACITY];
static volatile uint32_t g_break_trace_total;

extern u32 riscv_fast_read_u8(u32 address, u32 pc);
extern u32 riscv_fast_read_u16(u32 address, u32 pc);
extern u32 riscv_fast_read_s16(u32 address, u32 pc);
extern u32 riscv_fast_read_u32(u32 address, u32 pc);

static char *next_token(char **cursor)
{
  char *start = *cursor;

  while (*start != '\0' && isspace((unsigned char)*start))
    start++;
  if (*start == '\0')
  {
    *cursor = start;
    return NULL;
  }

  char *end = start;
  while (*end != '\0' && !isspace((unsigned char)*end))
    end++;
  if (*end != '\0')
    *end++ = '\0';
  *cursor = end;
  return start;
}

static bool parse_u32(const char *text, uint32_t *value)
{
  char *end = NULL;
  unsigned long parsed;

  if (text == NULL || value == NULL || *text == '\0')
    return false;
  errno = 0;
  parsed = strtoul(text, &end, 0);
  if (errno != 0 || end == text || *end != '\0' ||
      parsed > UINT32_MAX)
  {
    return false;
  }
  *value = (uint32_t)parsed;
  return true;
}

static bool joypad_button_mask(const char *name, uint16_t *mask)
{
  static const struct
  {
    const char *name;
    uint8_t id;
  } buttons[] = {
      {"up", RETRO_DEVICE_ID_JOYPAD_UP},
      {"u", RETRO_DEVICE_ID_JOYPAD_UP},
      {"down", RETRO_DEVICE_ID_JOYPAD_DOWN},
      {"d", RETRO_DEVICE_ID_JOYPAD_DOWN},
      {"left", RETRO_DEVICE_ID_JOYPAD_LEFT},
      {"right", RETRO_DEVICE_ID_JOYPAD_RIGHT},
      {"a", RETRO_DEVICE_ID_JOYPAD_A},
      {"b", RETRO_DEVICE_ID_JOYPAD_B},
      {"x", RETRO_DEVICE_ID_JOYPAD_X},
      {"y", RETRO_DEVICE_ID_JOYPAD_Y},
      {"l", RETRO_DEVICE_ID_JOYPAD_L},
      {"r", RETRO_DEVICE_ID_JOYPAD_R},
      {"select", RETRO_DEVICE_ID_JOYPAD_SELECT},
      {"sel", RETRO_DEVICE_ID_JOYPAD_SELECT},
      {"start", RETRO_DEVICE_ID_JOYPAD_START},
      {"sta", RETRO_DEVICE_ID_JOYPAD_START},
  };

  if (name == NULL || mask == NULL)
    return false;
  for (size_t i = 0; i < sizeof(buttons) / sizeof(buttons[0]); i++)
  {
    if (strcmp(name, buttons[i].name) == 0)
    {
      *mask = (uint16_t)(UINT16_C(1) << buttons[i].id);
      return true;
    }
  }
  return false;
}

static void clear_injected_joypad(void)
{
  g_injected_joypad_mask = 0u;
  g_injected_joypad_frames = 0u;
}

static void clear_macro_recorder(void)
{
  g_macro_recording = false;
  g_macro_replaying = false;
  g_macro_have_mask = false;
  g_macro_overflow = false;
  g_macro_record_start_frame = g_completed_frames;
  g_macro_replay_start_frame = g_completed_frames;
  g_macro_replay_index = 0u;
  g_macro_event_count = 0u;
  g_macro_last_mask = 0u;
  g_macro_replay_mask = 0u;
}

static void stop_macro_activity(void)
{
  g_macro_recording = false;
  g_macro_replaying = false;
  g_macro_replay_mask = 0u;
}

static void print_macro_status(const char *action)
{
  uint32_t elapsed = 0u;
  if (g_macro_recording)
    elapsed = g_completed_frames - g_macro_record_start_frame;
  else if (g_macro_replaying &&
           g_completed_frames >= g_macro_replay_start_frame)
    elapsed = g_completed_frames - g_macro_replay_start_frame;

  printf("result=PASS command=macro action=%s recording=%u replaying=%u "
         "version=1 record_start_frame=%" PRIu32
         " replay_start_frame=%" PRIu32 " elapsed_frames=%" PRIu32
         " events=%" PRIu32 " replay_index=%" PRIu32
         " last_mask=0x%04" PRIx16 " replay_mask=0x%04" PRIx16
         " overflow=%u capacity=%u\n",
         action, (unsigned)g_macro_recording, (unsigned)g_macro_replaying,
         g_macro_record_start_frame, g_macro_replay_start_frame, elapsed,
         g_macro_event_count, g_macro_replay_index, g_macro_last_mask,
         g_macro_replay_mask, (unsigned)g_macro_overflow,
         (unsigned)UART_DEBUG_MACRO_CAPACITY);
}

static void dump_macro(uint32_t start, uint32_t count)
{
  if (start > g_macro_event_count)
    start = g_macro_event_count;
  const uint32_t available = g_macro_event_count - start;
  if (count > available)
    count = available;

  printf("result=PASS command=macro action=dump version=1 start=%" PRIu32
         " count=%" PRIu32 " total=%" PRIu32 "\n",
         start, count, g_macro_event_count);
  for (uint32_t i = 0u; i < count; i++)
  {
    const uint32_t index = start + i;
    printf("result=PASS command=macro action=event version=1 index=%" PRIu32
           " frame=%" PRIu32 " mask=0x%04" PRIx16 "\n",
           index, g_macro_events[index].frame,
           g_macro_events[index].mask);
  }
}

static void set_injected_joypad(const char *command, uint16_t mask,
                                uint32_t frames)
{
  g_injected_joypad_mask = mask;
  g_injected_joypad_frames = frames;
  printf("result=PASS command=%s action=set mask=0x%04" PRIx16
         " frames=%" PRIu32 " hold=%u\n",
         command, mask, frames, (unsigned)(frames == 0u));
}

static void print_help(void)
{
  printf("result=PASS command=help commands=help status pause cont stepf "
         "reset boot regs framehash mem readprobe jit jitcode bp bpbad tracepc "
         "romwrites key joy macro\n");
  printf("debug help='boot [frames] [trace_count] resets while paused, "
         "captures exact JIT block exits and gamepak writes'\n");
  printf("debug help='tracepc N [start length] | tracepc dump [start count] | "
         "tracepc off; maximum N=%u'\n",
         (unsigned)UART_DEBUG_TRACE_CAPACITY);
  printf("debug help='bp block_pc [branch_pc] | bp dump [start count] | "
         "bp off; bp dump is the rolling pre-hit block trace'\n");
  printf("debug help='bpbad block_pc expected_next stops only when the "
         "translated block produces a different next PC'\n");
  printf("debug help='romwrites [clear|on|off] records writes to "
         "0x08000000..0x0cffffff without printing in the hot path'\n");
  printf("debug help='key NAME [frames] | joy MASK [frames] | key clear | "
         "key status; frames defaults to 2, zero holds until clear; "
         "names=up,down,left,right,a,b,x,y,l,r,select,start'\n");
  printf("debug help='macro record|stop|status|clear|dump [start count]|"
         "add FRAME MASK|play [delay]; records or replays final GBA joypad "
         "mask changes on emulated-frame boundaries; capacity=%u'\n",
         (unsigned)UART_DEBUG_MACRO_CAPACITY);
}

static void print_status(void)
{
  printf("result=PASS command=status backend=dynarec paused=%u "
         "frame_budget=%" PRIu32 " completed_frames=%" PRIu32
         " pc=0x%08" PRIx32 " cpsr=0x%08" PRIx32
         " mode=0x%02" PRIx32 " thumb=%u halt=%" PRIu32
         " trace_active=%u trace_count=%" PRIu32
         " trace_remaining=%" PRIu32
         " rom_write_count=%" PRIu32 " rom_write_total=%" PRIu32
         " joypad_injected=0x%04" PRIx16
         " joypad_frames=%" PRIu32 " joypad_hold=%u"
         " macro_recording=%u macro_replaying=%u"
         " macro_events=%" PRIu32 " macro_replay_index=%" PRIu32 "\n",
         (unsigned)g_paused, g_frame_budget, g_completed_frames,
         U32_ARG(reg[REG_PC]), U32_ARG(reg[REG_CPSR]),
         U32_ARG(reg[CPU_MODE]),
         (unsigned)((reg[REG_CPSR] >> 5) & 1u),
         U32_ARG(reg[CPU_HALT_STATE]), (unsigned)g_trace_active,
         (uint32_t)g_trace_count, (uint32_t)g_trace_remaining,
         (uint32_t)g_rom_write_count, (uint32_t)g_rom_write_total,
         g_injected_joypad_mask, g_injected_joypad_frames,
         (unsigned)(g_injected_joypad_mask != 0u &&
                    g_injected_joypad_frames == 0u),
         (unsigned)g_macro_recording, (unsigned)g_macro_replaying,
         g_macro_event_count, g_macro_replay_index);
}

static void print_regs(void)
{
  for (unsigned first = 0; first < 16u; first += 4u)
  {
    printf("result=PASS command=regs first=%u "
           "r%u=0x%08" PRIx32 " r%u=0x%08" PRIx32
           " r%u=0x%08" PRIx32 " r%u=0x%08" PRIx32 "\n",
           first, first, U32_ARG(reg[first]), first + 1u,
           U32_ARG(reg[first + 1u]), first + 2u,
           U32_ARG(reg[first + 2u]), first + 3u,
           U32_ARG(reg[first + 3u]));
  }
  printf("result=PASS command=regs state cpsr=0x%08" PRIx32
         " mode=0x%02" PRIx32 " halt=%" PRIu32
         " bus=0x%08" PRIx32 "\n",
         U32_ARG(reg[REG_CPSR]), U32_ARG(reg[CPU_MODE]),
         U32_ARG(reg[CPU_HALT_STATE]), U32_ARG(reg[REG_BUS_VALUE]));
}

static void print_frame_hash(void)
{
  if (gba_screen_pixels == NULL)
  {
    printf("result=FAIL command=framehash reason=no_framebuffer\n");
    return;
  }

  const uint8_t *bytes = (const uint8_t *)gba_screen_pixels;
  const size_t size = ESP32S31_GBA_WIDTH * ESP32S31_GBA_HEIGHT *
                      sizeof(*gba_screen_pixels);
  uint32_t hash = UINT32_C(2166136261);
  for (size_t i = 0; i < size; i++)
  {
    hash ^= bytes[i];
    hash *= UINT32_C(16777619);
  }
  printf("result=PASS command=framehash format=rgb565 width=%u height=%u "
         "bytes=%zu fnv1a=0x%08" PRIx32 "\n",
         (unsigned)ESP32S31_GBA_WIDTH, (unsigned)ESP32S31_GBA_HEIGHT,
         size, hash);
}

static void print_jit(void)
{
  riscv_runtime_stats jit = {0};
  riscv_get_runtime_stats(&jit);
  const uint32_t native_ops =
      jit.native_data_proc_insns + jit.native_branch_insns +
      jit.native_load_insns + jit.native_store_insns +
      jit.native_psr_insns;

  printf("result=PASS command=jit backend=dynarec blocks_emitted=%" PRIu32
         " native_blocks=%" PRIu32 " fallbacks=%" PRIu32
         " bios_blocks_emitted=%" PRIu32
         " bios_native_blocks=%" PRIu32
         " bios_fallbacks=%" PRIu32
         " initial_fallbacks=%" PRIu32
         " relookup_fallbacks=%" PRIu32
         " unsupported_fallbacks=%" PRIu32
         " native_ops=%" PRIu32 " thumb_helpers=%" PRIu32
         " rom_code_bytes=%zu ram_code_bytes=%zu\n",
         U32_ARG(jit.blocks_emitted), U32_ARG(jit.blocks_executed),
         U32_ARG(jit.interpreter_fallbacks),
         U32_ARG(jit.bios_native_blocks_emitted),
         U32_ARG(jit.bios_native_blocks_executed),
         U32_ARG(jit.bios_interpreter_fallbacks),
         U32_ARG(jit.initial_lookup_fallbacks),
         U32_ARG(jit.relookup_fallbacks),
         U32_ARG(jit.unsupported_fallbacks), native_ops,
         U32_ARG(jit.thumb_helper_insns),
         (size_t)(rom_translation_ptr - rom_translation_cache),
         (size_t)(ram_translation_ptr - ram_translation_cache));
}

static void dump_memory(uint32_t address, uint32_t length)
{
  if (length == 0u || length > UART_DEBUG_MEM_MAX_BYTES ||
      address > UINT32_MAX - (length - 1u))
  {
    printf("result=FAIL command=mem reason=bad_range max_len=%u\n",
           (unsigned)UART_DEBUG_MEM_MAX_BYTES);
    return;
  }

  for (uint32_t offset = 0; offset < length; offset += 16u)
  {
    const uint32_t row =
        length - offset < 16u ? length - offset : 16u;
    printf("result=PASS command=mem addr=0x%08" PRIx32
           " offset=%" PRIu32 " len=%" PRIu32 " data=",
           address, offset, row);
    for (uint32_t i = 0; i < row; i++)
      printf("%02" PRIx32,
             U32_ARG(read_memory8(address + offset + i) & 0xffu));
    putchar('\n');
  }
}

static void print_read_probe(uint32_t address)
{
  if ((address >> 24) != 0x02u && (address >> 24) != 0x03u)
  {
    printf("result=FAIL command=readprobe reason=not_work_ram\n");
    return;
  }

  printf("result=PASS command=readprobe addr=0x%08" PRIx32
         " generic_u8=0x%08" PRIx32 " fast_u8=0x%08" PRIx32
         " generic_u16=0x%08" PRIx32 " fast_u16=0x%08" PRIx32
         " generic_s16=0x%08" PRIx32 " fast_s16=0x%08" PRIx32
         " generic_u32=0x%08" PRIx32 " fast_u32=0x%08" PRIx32 "\n",
         address, U32_ARG(read_memory8(address)),
         U32_ARG(riscv_fast_read_u8(address, reg[REG_PC])),
         U32_ARG(read_memory16(address)),
         U32_ARG(riscv_fast_read_u16(address, reg[REG_PC])),
         U32_ARG(read_memory16s(address)),
         U32_ARG(riscv_fast_read_s16(address, reg[REG_PC])),
         U32_ARG(read_memory32(address)),
         U32_ARG(riscv_fast_read_u32(address, reg[REG_PC])));
}

static void dump_jit_code(uint32_t pc, uint32_t thumb, uint32_t words)
{
  if (words == 0u || words > UART_DEBUG_JIT_MAX_WORDS)
  {
    printf("result=FAIL command=jitcode reason=bad_word_count max=%u\n",
           (unsigned)UART_DEBUG_JIT_MAX_WORDS);
    return;
  }

  u8 *entry = thumb ? block_lookup_address_thumb(pc) :
                      block_lookup_address_arm(pc);
  if (entry == NULL || entry == (u8 *)(uintptr_t)UINT32_MAX)
  {
    printf("result=FAIL command=jitcode reason=lookup_failed\n");
    return;
  }

  const riscv_jit_block_meta *meta =
      (const riscv_jit_block_meta *)(const void *)(entry -
                                                   RISCV_BLOCK_META_BYTES);
  printf("result=PASS command=jitcode pc=0x%08" PRIx32
         " thumb=%" PRIu32 " entry=%p meta=%p meta_start=0x%08" PRIx32
         " meta_end_delta_thumb=0x%04x meta_chain_units=%u"
         " meta_flags=0x%02x words=%" PRIu32 "\n",
         pc, thumb, entry, (const void *)meta, U32_ARG(meta->start_pc),
         (unsigned)meta->end_delta_thumb, (unsigned)meta->chain_units,
         (unsigned)meta->flags, words);
  const uint32_t *code = (const uint32_t *)(const void *)entry;
  for (uint32_t offset = 0; offset < words; offset += 8u)
  {
    const uint32_t row = words - offset < 8u ? words - offset : 8u;
    printf("result=PASS command=jitcode offset_words=%" PRIu32 " data=",
           offset);
    for (uint32_t i = 0; i < row; i++)
      printf("%08" PRIx32, U32_ARG(code[offset + i]));
    putchar('\n');
  }
}

static void trace_restore_fast_dispatch(void)
{
  if (riscv_runtime_debug_force_dispatch())
  {
    riscv_set_runtime_debug_force_dispatch(false);
    flush_dynarec_caches();
  }
}

static void trace_cancel(void)
{
  g_trace_active = false;
  g_trace_remaining = 0u;
  g_trace_complete_pending = false;
  trace_restore_fast_dispatch();
}

static void breakpoint_cancel(void)
{
  g_break_active = false;
  g_break_complete_pending = false;
  g_break_probe_pc = 0u;
  g_break_only_bad_next = false;
  g_break_expected_next = 0u;
  riscv_set_runtime_debug_branch_probe_pc(0u);
  if (riscv_runtime_debug_force_dispatch())
  {
    riscv_set_runtime_debug_force_dispatch(false);
    flush_dynarec_caches();
  }
}

static void breakpoint_arm(uint32_t pc, uint32_t probe_pc,
                           bool only_bad_next, uint32_t expected_next)
{
  trace_cancel();
  g_break_pc = pc;
  g_break_probe_pc = probe_pc;
  g_break_only_bad_next = only_bad_next;
  g_break_expected_next = expected_next;
  g_break_complete_pending = false;
  g_break_active = true;
  g_break_trace_total = 0u;
  riscv_set_runtime_debug_branch_probe_pc(probe_pc);
  riscv_set_runtime_debug_force_dispatch(true);
  flush_dynarec_caches();
  printf("result=PASS command=%s action=armed pc=0x%08" PRIx32
         " probe_pc=0x%08" PRIx32 " expected_next=0x%08" PRIx32
         " exact_blocks=1\n",
         only_bad_next ? "bpbad" : "bp", pc, probe_pc, expected_next);
}

static void trace_arm(uint32_t count, uint32_t range_start,
                      uint32_t range_length)
{
  if (count == 0u)
    count = 1u;
  if (count > UART_DEBUG_TRACE_CAPACITY)
    count = UART_DEBUG_TRACE_CAPACITY;

  g_trace_count = 0u;
  g_trace_remaining = count;
  g_trace_complete_pending = false;
  g_trace_range_start = range_start;
  if (range_length == 0u ||
      range_start > UINT32_MAX - (range_length - 1u))
  {
    g_trace_range_end = UINT32_MAX;
  }
  else
  {
    g_trace_range_end = range_start + range_length - 1u;
  }
  g_trace_active = true;

  /* Existing blocks may already jump directly to one another. Rebuild with
   * external branch patching disabled so every translated block returns
   * through riscv_note_runtime_block_execute() while capture is armed. */
  riscv_set_runtime_debug_force_dispatch(true);
  flush_dynarec_caches();

  printf("result=PASS command=tracepc action=armed count=%" PRIu32
         " start=0x%08" PRIx32 " end=0x%08" PRIx32
         " exact_blocks=1\n",
         count, g_trace_range_start, g_trace_range_end);
}

static void print_trace_entry(const char *owner, uint32_t index,
                              const uart_trace_entry_t *entry)
{
  if (entry->kind == UART_TRACE_BLOCK)
  {
    printf("result=PASS command=%s index=%" PRIu32
           " kind=block start=0x%08" PRIx32
           " end=0x%08" PRIx32 " next=0x%08" PRIx32
           " cpsr=0x%08" PRIx32 " thumb=%" PRIu32 "\n",
           owner, index, entry->pc, entry->end_or_lookup,
           entry->next_pc_or_cycles, entry->cpsr_or_fallback,
           entry->thumb);
  }
  else
  {
    printf("result=PASS command=%s index=%" PRIu32
           " kind=fallback pc=0x%08" PRIx32
           " lookup=%" PRIu32 " cycles=%" PRIu32
           " fallback_kind=%" PRIu32 " thumb=%" PRIu32 "\n",
           owner, index, entry->pc, entry->end_or_lookup,
           entry->next_pc_or_cycles, entry->cpsr_or_fallback,
           entry->thumb);
  }
}

static void dump_trace(uint32_t start, uint32_t limit)
{
  const uint32_t count = g_trace_count;
  uint32_t end;

  if (start > count)
    start = count;
  end = count;
  if (limit != 0u && limit < end - start)
    end = start + limit;

  printf("result=PASS command=tracepc action=dump count=%" PRIu32
         " start=%" PRIu32 " shown=%" PRIu32
         " active=%u remaining=%" PRIu32 " exact_blocks=1\n",
         count, start, end - start, (unsigned)g_trace_active,
         (uint32_t)g_trace_remaining);
  for (uint32_t i = start; i < end; i++)
    print_trace_entry("tracepc", i, &g_trace[i]);
}

static void dump_break_trace(uint32_t start, uint32_t limit)
{
  const uint32_t total = g_break_trace_total;
  const uint32_t count = total < UART_DEBUG_TRACE_CAPACITY ? total :
                         UART_DEBUG_TRACE_CAPACITY;
  const uint32_t oldest = total > UART_DEBUG_TRACE_CAPACITY ?
                          total - UART_DEBUG_TRACE_CAPACITY : 0u;
  uint32_t end;

  if (start > count)
    start = count;
  end = count;
  if (limit != 0u && limit < end - start)
    end = start + limit;

  printf("result=PASS command=bp action=dump total=%" PRIu32
         " retained=%" PRIu32 " oldest=%" PRIu32
         " start=%" PRIu32 " shown=%" PRIu32 " active=%u\n",
         total, count, oldest, start, end - start,
         (unsigned)g_break_active);
  for (uint32_t i = start; i < end; i++)
  {
    const uint32_t sequence = oldest + i;
    const uint32_t slot = sequence % UART_DEBUG_TRACE_CAPACITY;
    print_trace_entry("bp", sequence, &g_break_trace[slot]);
  }
}

static void clear_rom_writes(void)
{
  g_rom_write_count = 0u;
  g_rom_write_total = 0u;
}

static void dump_rom_writes(void)
{
  const uint32_t count = g_rom_write_count;
  printf("result=PASS command=romwrites action=dump count=%" PRIu32
         " total=%" PRIu32 " capture=%u\n",
         count, (uint32_t)g_rom_write_total,
         (unsigned)g_rom_write_capture);
  for (uint32_t i = 0; i < count; i++)
  {
    const uart_rom_write_entry_t *entry = &g_rom_writes[i];
    printf("result=PASS command=romwrites index=%" PRIu32
           " pc=0x%08" PRIx32 " addr=0x%08" PRIx32
           " bits=%" PRIu32 " value=0x%08" PRIx32 "\n",
           i, entry->pc, entry->address, entry->bits, entry->value);
  }
}

static void reset_debug_run(uint32_t frames, uint32_t trace_count)
{
  if (frames == 0u)
    frames = 1u;
  if (frames > UART_DEBUG_FRAME_STEP_MAX)
    frames = UART_DEBUG_FRAME_STEP_MAX;

  g_paused = true;
  g_frame_budget = 0u;
  g_report_frame_step_complete = false;
  clear_injected_joypad();
  clear_rom_writes();
  g_rom_write_capture = true;
  trace_cancel();
  retro_reset();
  if (trace_count != 0u)
    trace_arm(trace_count, 0u, 0u);
  g_frame_budget = frames;
  printf("result=PASS command=boot action=reset frames=%" PRIu32
         " trace_count=%" PRIu32 " paused=1 pc=0x%08" PRIx32 "\n",
         frames, trace_count, U32_ARG(reg[REG_PC]));
}

static void handle_command(char *line)
{
  char *cursor = line;
  char *command = next_token(&cursor);
  char *arg0;
  char *arg1;
  char *arg2;
  uint32_t value0;
  uint32_t value1;
  uint32_t value2;

  if (command == NULL)
    return;

  if (strcmp(command, "help") == 0 || strcmp(command, "?") == 0)
  {
    print_help();
  }
  else if (strcmp(command, "status") == 0)
  {
    print_status();
  }
  else if (strcmp(command, "pause") == 0)
  {
    g_paused = true;
    g_frame_budget = 0u;
    g_report_frame_step_complete = false;
    printf("result=PASS command=pause pc=0x%08" PRIx32 "\n",
           U32_ARG(reg[REG_PC]));
  }
  else if (strcmp(command, "cont") == 0 ||
           strcmp(command, "continue") == 0)
  {
    g_paused = false;
    g_frame_budget = 0u;
    g_report_frame_step_complete = false;
    printf("result=PASS command=cont pc=0x%08" PRIx32 "\n",
           U32_ARG(reg[REG_PC]));
  }
  else if (strcmp(command, "stepf") == 0 ||
           strcmp(command, "run") == 0)
  {
    arg0 = next_token(&cursor);
    value0 = 1u;
    if ((arg0 != NULL && !parse_u32(arg0, &value0)) || value0 == 0u ||
        value0 > UART_DEBUG_FRAME_STEP_MAX)
    {
      printf("result=FAIL command=stepf reason=bad_count max=%u\n",
             (unsigned)UART_DEBUG_FRAME_STEP_MAX);
    }
    else
    {
      g_paused = true;
      g_frame_budget = value0;
      g_report_frame_step_complete = false;
      printf("result=PASS command=stepf action=armed frames=%" PRIu32
             "\n",
             value0);
    }
  }
  else if (strcmp(command, "reset") == 0)
  {
    g_paused = true;
    g_frame_budget = 0u;
    trace_cancel();
    clear_rom_writes();
    clear_injected_joypad();
    stop_macro_activity();
    g_rom_write_capture = true;
    retro_reset();
    printf("result=PASS command=reset paused=1 pc=0x%08" PRIx32
           "\n",
           U32_ARG(reg[REG_PC]));
  }
  else if (strcmp(command, "boot") == 0)
  {
    arg0 = next_token(&cursor);
    arg1 = next_token(&cursor);
    value0 = 1u;
    value1 = 128u;
    if ((arg0 != NULL && !parse_u32(arg0, &value0)) ||
        (arg1 != NULL && !parse_u32(arg1, &value1)))
    {
      printf("result=FAIL command=boot reason=bad_argument\n");
    }
    else
    {
      reset_debug_run(value0, value1);
    }
  }
  else if (strcmp(command, "regs") == 0)
  {
    print_regs();
  }
  else if (strcmp(command, "framehash") == 0)
  {
    print_frame_hash();
  }
  else if (strcmp(command, "mem") == 0)
  {
    arg0 = next_token(&cursor);
    arg1 = next_token(&cursor);
    if (!parse_u32(arg0, &value0) || !parse_u32(arg1, &value1))
      printf("result=FAIL command=mem reason=usage usage='mem address length'\n");
    else
      dump_memory(value0, value1);
  }
  else if (strcmp(command, "readprobe") == 0)
  {
    arg0 = next_token(&cursor);
    if (!parse_u32(arg0, &value0))
      printf("result=FAIL command=readprobe reason=usage "
             "usage='readprobe address'\n");
    else
      print_read_probe(value0);
  }
  else if (strcmp(command, "jit") == 0)
  {
    print_jit();
  }
  else if (strcmp(command, "jitcode") == 0)
  {
    arg0 = next_token(&cursor);
    arg1 = next_token(&cursor);
    arg2 = next_token(&cursor);
    if (!parse_u32(arg0, &value0) || !parse_u32(arg1, &value1) ||
        !parse_u32(arg2, &value2) || value1 > 1u)
    {
      printf("result=FAIL command=jitcode reason=usage "
             "usage='jitcode pc thumb words'\n");
    }
    else
      dump_jit_code(value0, value1, value2);
  }
  else if (strcmp(command, "bp") == 0)
  {
    arg0 = next_token(&cursor);
    arg1 = next_token(&cursor);
    arg2 = next_token(&cursor);
    if (arg0 != NULL && strcmp(arg0, "off") == 0)
    {
      breakpoint_cancel();
      printf("result=PASS command=bp action=off\n");
    }
    else if (arg0 != NULL && strcmp(arg0, "dump") == 0)
    {
      value0 = 0u;
      value1 = 64u;
      if ((arg1 != NULL && !parse_u32(arg1, &value0)) ||
          (arg2 != NULL && !parse_u32(arg2, &value1)))
      {
        printf("result=FAIL command=bp reason=usage "
               "usage='bp dump [start count]'\n");
      }
      else
        dump_break_trace(value0, value1);
    }
    else if (!parse_u32(arg0, &value0))
    {
      printf("result=FAIL command=bp reason=usage "
             "usage='bp block_pc [branch_pc]|dump [start count]|off'\n");
    }
    else if (arg1 != NULL && !parse_u32(arg1, &value1))
    {
      printf("result=FAIL command=bp reason=bad_branch_pc\n");
    }
    else
      breakpoint_arm(value0, arg1 != NULL ? value1 : 0u, false, 0u);
  }
  else if (strcmp(command, "bpbad") == 0)
  {
    arg0 = next_token(&cursor);
    arg1 = next_token(&cursor);
    if (!parse_u32(arg0, &value0) || !parse_u32(arg1, &value1))
    {
      printf("result=FAIL command=bpbad reason=usage "
             "usage='bpbad block_pc expected_next'\n");
    }
    else
      breakpoint_arm(value0, 0u, true, value1);
  }
  else if (strcmp(command, "tracepc") == 0)
  {
    arg0 = next_token(&cursor);
    if (arg0 != NULL && strcmp(arg0, "dump") == 0)
    {
      arg1 = next_token(&cursor);
      arg2 = next_token(&cursor);
      value0 = 0u;
      value1 = 64u;
      if ((arg1 != NULL && !parse_u32(arg1, &value0)) ||
          (arg2 != NULL && !parse_u32(arg2, &value1)))
      {
        printf("result=FAIL command=tracepc reason=usage "
               "usage='tracepc dump [start count]'\n");
      }
      else
        dump_trace(value0, value1);
    }
    else if (arg0 != NULL && strcmp(arg0, "off") == 0)
    {
      trace_cancel();
      printf("result=PASS command=tracepc action=off\n");
    }
    else
    {
      arg1 = next_token(&cursor);
      arg2 = next_token(&cursor);
      value0 = 128u;
      value1 = 0u;
      value2 = 0u;
      if ((arg0 != NULL && !parse_u32(arg0, &value0)) ||
          (arg1 != NULL && !parse_u32(arg1, &value1)) ||
          (arg2 != NULL && !parse_u32(arg2, &value2)))
      {
        printf("result=FAIL command=tracepc reason=bad_argument\n");
      }
      else
      {
        trace_arm(value0, value1, value2);
      }
    }
  }
  else if (strcmp(command, "romwrites") == 0)
  {
    arg0 = next_token(&cursor);
    if (arg0 == NULL || strcmp(arg0, "dump") == 0)
      dump_rom_writes();
    else if (strcmp(arg0, "clear") == 0)
    {
      clear_rom_writes();
      printf("result=PASS command=romwrites action=clear\n");
    }
    else if (strcmp(arg0, "on") == 0)
    {
      g_rom_write_capture = true;
      printf("result=PASS command=romwrites action=on\n");
    }
    else if (strcmp(arg0, "off") == 0)
    {
      g_rom_write_capture = false;
      printf("result=PASS command=romwrites action=off\n");
    }
    else
      printf("result=FAIL command=romwrites reason=bad_argument\n");
  }
  else if (strcmp(command, "key") == 0)
  {
    uint16_t mask;

    arg0 = next_token(&cursor);
    arg1 = next_token(&cursor);
    value0 = 2u;
    if (arg0 != NULL && strcmp(arg0, "clear") == 0 && arg1 == NULL)
    {
      clear_injected_joypad();
      printf("result=PASS command=key action=clear mask=0x0000\n");
    }
    else if (arg0 != NULL && strcmp(arg0, "status") == 0 && arg1 == NULL)
    {
      printf("result=PASS command=key action=status mask=0x%04" PRIx16
             " frames=%" PRIu32 " hold=%u\n",
             g_injected_joypad_mask, g_injected_joypad_frames,
             (unsigned)(g_injected_joypad_mask != 0u &&
                        g_injected_joypad_frames == 0u));
    }
    else if (!joypad_button_mask(arg0, &mask) ||
             (arg1 != NULL && !parse_u32(arg1, &value0)) ||
             value0 > UART_DEBUG_INPUT_FRAME_MAX)
    {
      printf("result=FAIL command=key reason=usage "
             "usage='key NAME [frames]|clear|status' max_frames=%u\n",
             (unsigned)UART_DEBUG_INPUT_FRAME_MAX);
    }
    else
    {
      set_injected_joypad("key", mask, value0);
    }
  }
  else if (strcmp(command, "joy") == 0)
  {
    arg0 = next_token(&cursor);
    arg1 = next_token(&cursor);
    value1 = 2u;
    if (!parse_u32(arg0, &value0) || value0 > UINT16_MAX ||
        (arg1 != NULL && !parse_u32(arg1, &value1)) ||
        value1 > UART_DEBUG_INPUT_FRAME_MAX)
    {
      printf("result=FAIL command=joy reason=usage "
             "usage='joy MASK [frames]' max_frames=%u\n",
             (unsigned)UART_DEBUG_INPUT_FRAME_MAX);
    }
    else
    {
      set_injected_joypad("joy", (uint16_t)value0, value1);
    }
  }
  else if (strcmp(command, "macro") == 0)
  {
    arg0 = next_token(&cursor);
    arg1 = next_token(&cursor);
    arg2 = next_token(&cursor);
    if (arg0 != NULL && strcmp(arg0, "record") == 0)
    {
      clear_macro_recorder();
      g_macro_recording = true;
      g_macro_record_start_frame = g_completed_frames;
      print_macro_status("record");
    }
    else if (arg0 != NULL && strcmp(arg0, "stop") == 0)
    {
      stop_macro_activity();
      print_macro_status("stop");
    }
    else if (arg0 != NULL && strcmp(arg0, "status") == 0 && arg1 == NULL)
    {
      print_macro_status("status");
    }
    else if (arg0 != NULL && strcmp(arg0, "clear") == 0 && arg1 == NULL)
    {
      clear_macro_recorder();
      print_macro_status("clear");
    }
    else if (arg0 != NULL && strcmp(arg0, "dump") == 0)
    {
      value0 = 0u;
      value1 = g_macro_event_count;
      if ((arg1 != NULL && !parse_u32(arg1, &value0)) ||
          (arg2 != NULL && !parse_u32(arg2, &value1)))
      {
        printf("result=FAIL command=macro reason=usage "
               "usage='macro dump [start count]'\n");
      }
      else
      {
        dump_macro(value0, value1);
      }
    }
    else if (arg0 != NULL && strcmp(arg0, "add") == 0)
    {
      if (!parse_u32(arg1, &value0) || !parse_u32(arg2, &value1) ||
          value1 > UINT16_MAX)
      {
        printf("result=FAIL command=macro reason=usage "
               "usage='macro add FRAME MASK'\n");
      }
      else if (g_macro_recording || g_macro_replaying)
      {
        printf("result=FAIL command=macro reason=busy recording=%u "
               "replaying=%u\n",
               (unsigned)g_macro_recording, (unsigned)g_macro_replaying);
      }
      else if (g_macro_event_count >= UART_DEBUG_MACRO_CAPACITY)
      {
        g_macro_overflow = true;
        printf("result=FAIL command=macro reason=capacity capacity=%u\n",
               (unsigned)UART_DEBUG_MACRO_CAPACITY);
      }
      else if (g_macro_event_count != 0u &&
               value0 < g_macro_events[g_macro_event_count - 1u].frame)
      {
        printf("result=FAIL command=macro reason=frame_order frame=%" PRIu32
               " previous=%" PRIu32 "\n",
               value0, g_macro_events[g_macro_event_count - 1u].frame);
      }
      else
      {
        const uint32_t index = g_macro_event_count++;
        g_macro_events[index].frame = value0;
        g_macro_events[index].mask = (uint16_t)value1;
        g_macro_events[index].reserved = 0u;
        g_macro_last_mask = (uint16_t)value1;
        printf("result=PASS command=macro action=add index=%" PRIu32
               " frame=%" PRIu32 " mask=0x%04" PRIx16 "\n",
               index, value0, (uint16_t)value1);
      }
    }
    else if (arg0 != NULL && strcmp(arg0, "play") == 0)
    {
      value0 = 0u;
      if ((arg1 != NULL && !parse_u32(arg1, &value0)) || arg2 != NULL ||
          value0 > UART_DEBUG_MACRO_DELAY_MAX)
      {
        printf("result=FAIL command=macro reason=usage "
               "usage='macro play [delay_frames]' max_delay=%u\n",
               (unsigned)UART_DEBUG_MACRO_DELAY_MAX);
      }
      else if (g_macro_event_count == 0u)
      {
        printf("result=FAIL command=macro reason=empty\n");
      }
      else
      {
        g_macro_recording = false;
        g_macro_replaying = true;
        g_macro_replay_start_frame = g_completed_frames + value0;
        g_macro_replay_index = 0u;
        g_macro_replay_mask = 0u;
        clear_injected_joypad();
        print_macro_status("play");
      }
    }
    else
    {
      printf("result=FAIL command=macro reason=usage "
             "usage='macro record|stop|status|clear|dump [start count]|"
             "add FRAME MASK|play [delay]'\n");
    }
  }
  else
  {
    printf("result=FAIL command=%s reason=unknown_command\n", command);
  }
  fflush(stdout);
}

void esp32s31_uart_debug_init(void)
{
#if CONFIG_ESP_CONSOLE_UART
  uart_vfs_dev_use_nonblocking(CONFIG_ESP_CONSOLE_UART_NUM);
#endif
  const int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
  const bool fcntl_nonblocking =
      flags >= 0 && fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK) == 0;

  g_paused = false;
  g_frame_budget = 0u;
  g_completed_frames = 0u;
  clear_injected_joypad();
  clear_macro_recorder();
  g_rom_write_capture = true;
  clear_rom_writes();
  printf("result=PASS command=uart_debug ready=1 transport=uart%d "
         "nonblocking=1 fcntl_nonblocking=%u "
         "trace_capacity=%u rom_write_capacity=%u\n",
         CONFIG_ESP_CONSOLE_UART_NUM, (unsigned)fcntl_nonblocking,
         (unsigned)UART_DEBUG_TRACE_CAPACITY,
         (unsigned)UART_DEBUG_ROM_WRITE_CAPACITY);
  print_help();
  fflush(stdout);
}

void esp32s31_uart_debug_poll(void)
{
  unsigned char ch;

  for (;;)
  {
    const ssize_t received = read(STDIN_FILENO, &ch, 1u);
    if (received != 1)
      return;

    if (ch == '\r' || ch == '\n')
    {
      if (g_line_overflow)
      {
        printf("result=FAIL command=parse reason=line_too_long max=%u\n",
               (unsigned)(UART_DEBUG_LINE_BYTES - 1u));
        g_line_overflow = false;
        g_line_length = 0u;
        fflush(stdout);
        return;
      }
      if (g_line_length == 0u)
        continue;

      g_line[g_line_length] = '\0';
      handle_command(g_line);
      g_line_length = 0u;
      /* Process at most one command per emulation-loop iteration. This gives
       * a queued "boot ...\ntracepc dump\n" one complete frame between the
       * two commands instead of dumping an empty capture immediately. */
      return;
    }

    if (ch == '\b' || ch == 0x7fu)
    {
      if (g_line_length != 0u)
        g_line_length--;
      continue;
    }

    if (g_line_length + 1u < sizeof(g_line))
      g_line[g_line_length++] = (char)ch;
    else
      g_line_overflow = true;
  }
}

bool esp32s31_uart_debug_should_run_frame(void)
{
  if (!g_paused)
    return true;
  if (g_frame_budget == 0u)
    return false;

  g_frame_budget--;
  if (g_frame_budget == 0u)
    g_report_frame_step_complete = true;
  return true;
}

void esp32s31_uart_debug_frame_complete(void)
{
  g_completed_frames++;

  if (g_injected_joypad_mask != 0u && g_injected_joypad_frames != 0u)
  {
    g_injected_joypad_frames--;
    if (g_injected_joypad_frames == 0u)
    {
      g_injected_joypad_mask = 0u;
      printf("result=PASS command=key action=auto_release mask=0x0000 "
             "completed_frames=%" PRIu32 "\n",
             g_completed_frames);
    }
  }

  if (g_break_complete_pending)
  {
    g_break_complete_pending = false;
    if (riscv_runtime_debug_force_dispatch())
    {
      riscv_set_runtime_debug_force_dispatch(false);
      flush_dynarec_caches();
    }
    printf("result=PASS command=%s action=hit pc=0x%08" PRIx32
           " next=0x%08" PRIx32 " cpsr=0x%08" PRIx32
           " mode=0x%02" PRIx32 " expected_next=0x%08" PRIx32
           " r0=0x%08" PRIx32 " r1=0x%08" PRIx32
           " r2=0x%08" PRIx32 " r3=0x%08" PRIx32
           " r12=0x%08" PRIx32 " sp=0x%08" PRIx32
           " lr=0x%08" PRIx32
           " irq_sp=0x%08" PRIx32 " irq_lr=0x%08" PRIx32
           " irq_spsr=0x%08" PRIx32
           " stack_base=0x%08" PRIx32
           " stack_0=0x%08" PRIx32 " stack_1=0x%08" PRIx32
           " stack_2=0x%08" PRIx32 " stack_3=0x%08" PRIx32
           " stack_4=0x%08" PRIx32 " stack_5=0x%08" PRIx32
           " stack_6=0x%08" PRIx32 " stack_7=0x%08" PRIx32
           " active_index=0x%02" PRIx32
           " object_base=0x%08" PRIx32
           " object_value=0x%08" PRIx32
           " r2_object_valid=%" PRIu32
           " r2_object_value=0x%08" PRIx32
           " probe_valid=%" PRIu32 " probe_pc=0x%08" PRIx32
           " probe_r0=0x%08" PRIx32 " probe_r1=0x%08" PRIx32
           " probe_nzcv=0x%08" PRIx32
           " fast_dispatch_restored=1\n",
           g_break_only_bad_next ? "bpbad" : "bp",
           g_break_pc, g_break_snapshot.next_pc, g_break_snapshot.cpsr,
           g_break_snapshot.mode, g_break_expected_next,
           g_break_snapshot.regs[0], g_break_snapshot.regs[1],
           g_break_snapshot.regs[2], g_break_snapshot.regs[3],
           g_break_snapshot.regs[12], g_break_snapshot.regs[REG_SP],
           g_break_snapshot.regs[REG_LR], g_break_snapshot.irq_sp,
           g_break_snapshot.irq_lr, g_break_snapshot.irq_spsr,
           g_break_snapshot.stack_base,
           g_break_snapshot.stack_words[0],
           g_break_snapshot.stack_words[1],
           g_break_snapshot.stack_words[2],
           g_break_snapshot.stack_words[3],
           g_break_snapshot.stack_words[4],
           g_break_snapshot.stack_words[5],
           g_break_snapshot.stack_words[6],
           g_break_snapshot.stack_words[7],
           g_break_snapshot.active_index,
           g_break_snapshot.object_base, g_break_snapshot.object_value,
           g_break_snapshot.r2_object_valid,
           g_break_snapshot.r2_object_value,
           U32_ARG(g_break_snapshot.branch_probe.valid),
           U32_ARG(g_break_snapshot.branch_probe.pc),
           U32_ARG(g_break_snapshot.branch_probe.r0_host),
           U32_ARG(g_break_snapshot.branch_probe.r1_host),
           U32_ARG(g_break_snapshot.branch_probe.nzcv_host));
  }

  if (g_trace_complete_pending)
  {
    g_trace_complete_pending = false;
    trace_restore_fast_dispatch();
    printf("result=PASS command=tracepc action=complete count=%" PRIu32
           " pc=0x%08" PRIx32 " fast_dispatch_restored=1\n",
           (uint32_t)g_trace_count, U32_ARG(reg[REG_PC]));
  }

  if (g_report_frame_step_complete)
  {
    g_report_frame_step_complete = false;
    printf("result=PASS command=stepf action=complete "
           "completed_frames=%" PRIu32 " pc=0x%08" PRIx32 "\n",
           g_completed_frames, U32_ARG(reg[REG_PC]));
  }
  fflush(stdout);
}

uint16_t esp32s31_uart_debug_joypad_mask(void)
{
  return g_injected_joypad_mask;
}

uint16_t esp32s31_uart_debug_apply_joypad(uint16_t physical_mask)
{
  if (!g_macro_replaying)
    return physical_mask | g_injected_joypad_mask;

  if (g_completed_frames < g_macro_replay_start_frame)
    return 0u;

  const uint32_t relative_frame =
      g_completed_frames - g_macro_replay_start_frame;
  while (g_macro_replay_index < g_macro_event_count &&
         g_macro_events[g_macro_replay_index].frame <= relative_frame)
  {
    const uart_macro_event_t *event =
        &g_macro_events[g_macro_replay_index];
    g_macro_replay_mask = event->mask;
    printf("result=PASS command=macro action=play_event version=1 "
           "index=%" PRIu32 " frame=%" PRIu32 " mask=0x%04" PRIx16
           " absolute_frame=%" PRIu32 "\n",
           g_macro_replay_index, event->frame, event->mask,
           g_completed_frames);
    g_macro_replay_index++;
  }

  if (g_macro_replay_index == g_macro_event_count &&
      relative_frame > g_macro_events[g_macro_event_count - 1u].frame)
  {
    g_macro_replaying = false;
    g_macro_replay_mask = 0u;
    printf("result=PASS command=macro action=complete version=1 "
           "events=%" PRIu32 " elapsed_frames=%" PRIu32
           " absolute_frame=%" PRIu32 "\n",
           g_macro_event_count, relative_frame, g_completed_frames);
    fflush(stdout);
    return physical_mask;
  }

  fflush(stdout);
  return g_macro_replay_mask;
}

void esp32s31_uart_debug_record_joypad(uint16_t mask)
{
  if (!g_macro_recording ||
      (g_macro_have_mask && mask == g_macro_last_mask))
  {
    return;
  }

  const uint32_t relative_frame =
      g_completed_frames - g_macro_record_start_frame;
  if (g_macro_event_count >= UART_DEBUG_MACRO_CAPACITY)
  {
    g_macro_recording = false;
    g_macro_overflow = true;
    printf("result=FAIL command=macro reason=capacity capacity=%u "
           "elapsed_frames=%" PRIu32 "\n",
           (unsigned)UART_DEBUG_MACRO_CAPACITY, relative_frame);
    fflush(stdout);
    return;
  }

  g_macro_have_mask = true;
  g_macro_last_mask = mask;
  const uint32_t index = g_macro_event_count++;
  g_macro_events[index].frame = relative_frame;
  g_macro_events[index].mask = mask;
  g_macro_events[index].reserved = 0u;
  printf("result=PASS command=macro action=event version=1 "
         "index=%" PRIu32 " frame=%" PRIu32 " mask=0x%04" PRIx16
         " absolute_frame=%" PRIu32 " pc=0x%08" PRIx32 "\n",
         index, relative_frame, mask, g_completed_frames,
         U32_ARG(reg[REG_PC]));
  fflush(stdout);
}

void riscv_note_runtime_block_execute(u32 start_pc, u32 end_pc, u32 thumb)
{
  if (g_break_active)
  {
    const uint32_t sequence = g_break_trace_total;
    uart_trace_entry_t *entry =
        &g_break_trace[sequence % UART_DEBUG_TRACE_CAPACITY];

    entry->kind = UART_TRACE_BLOCK;
    entry->pc = start_pc;
    entry->end_or_lookup = end_pc;
    entry->next_pc_or_cycles = reg[REG_PC];
    entry->cpsr_or_fallback = reg[REG_CPSR];
    entry->thumb = thumb;
    g_break_trace_total = sequence + 1u;
  }

  if (g_break_active && start_pc == g_break_pc &&
      (!g_break_only_bad_next || reg[REG_PC] != g_break_expected_next))
  {
    for (uint32_t i = 0; i < 16u; i++)
      g_break_snapshot.regs[i] = reg[i];
    g_break_snapshot.cpsr = reg[REG_CPSR];
    g_break_snapshot.mode = reg[CPU_MODE];
    g_break_snapshot.next_pc = reg[REG_PC];
    g_break_snapshot.irq_sp = REG_MODE(MODE_IRQ)[5];
    g_break_snapshot.irq_lr = REG_MODE(MODE_IRQ)[6];
    g_break_snapshot.irq_spsr = REG_SPSR(MODE_IRQ);
    g_break_snapshot.stack_base = reg[REG_SP] - 32u;
    for (uint32_t i = 0; i < 8u; i++)
      g_break_snapshot.stack_words[i] =
          read_memory32(g_break_snapshot.stack_base + i * 4u);
    g_break_snapshot.active_index = ewram[0x227c8u];
    g_break_snapshot.object_base =
        0x020205acu + g_break_snapshot.active_index * 68u;
    const uint32_t object_offset =
        (g_break_snapshot.object_base + 60u) & 0x3ffffu;
    g_break_snapshot.object_value =
        (uint32_t)(int32_t)(int16_t)(ewram[object_offset] |
            ((uint16_t)ewram[(object_offset + 1u) & 0x3ffffu] << 8));
    g_break_snapshot.r2_value = reg[2];
    g_break_snapshot.r2_object_valid =
        (reg[2] >> 24) == 0x02u ? 1u : 0u;
    if (g_break_snapshot.r2_object_valid)
    {
      const uint32_t r2_offset = (reg[2] + 60u) & 0x3ffffu;
      g_break_snapshot.r2_object_value =
          (uint32_t)(int32_t)(int16_t)(ewram[r2_offset] |
              ((uint16_t)ewram[(r2_offset + 1u) & 0x3ffffu] << 8));
    }
    else
      g_break_snapshot.r2_object_value = 0u;
    riscv_get_runtime_debug_branch_probe(&g_break_snapshot.branch_probe);

    g_break_active = false;
    g_break_complete_pending = true;
    g_paused = true;
    g_frame_budget = 0u;
    g_report_frame_step_complete = false;
    riscv_request_runtime_debug_stop();
  }

  if (!g_trace_active || start_pc < g_trace_range_start ||
      start_pc > g_trace_range_end)
  {
    return;
  }

  const uint32_t index = g_trace_count;
  if (index >= UART_DEBUG_TRACE_CAPACITY || g_trace_remaining == 0u)
    return;

  uart_trace_entry_t *entry = &g_trace[index];
  entry->kind = UART_TRACE_BLOCK;
  entry->pc = start_pc;
  entry->end_or_lookup = end_pc;
  entry->next_pc_or_cycles = reg[REG_PC];
  entry->cpsr_or_fallback = reg[REG_CPSR];
  entry->thumb = thumb;
  g_trace_count = index + 1u;
  g_trace_remaining--;
  if (g_trace_remaining == 0u)
  {
    g_trace_active = false;
    g_trace_complete_pending = true;
  }
}

void riscv_note_runtime_fallback(u32 kind, u32 pc, u32 thumb,
                                 u32 lookup_result,
                                 u32 cycles_remaining)
{
  if (!g_trace_active || pc < g_trace_range_start ||
      pc > g_trace_range_end)
  {
    return;
  }

  const uint32_t index = g_trace_count;
  if (index >= UART_DEBUG_TRACE_CAPACITY || g_trace_remaining == 0u)
    return;

  uart_trace_entry_t *entry = &g_trace[index];
  entry->kind = UART_TRACE_FALLBACK;
  entry->pc = pc;
  entry->end_or_lookup = lookup_result;
  entry->next_pc_or_cycles = cycles_remaining;
  entry->cpsr_or_fallback = kind;
  entry->thumb = thumb;
  g_trace_count = index + 1u;
  g_trace_remaining--;
  if (g_trace_remaining == 0u)
  {
    g_trace_active = false;
    g_trace_complete_pending = true;
  }
}

void gpsp_debug_trace_gamepak_write(u32 pc, u32 address, u32 bits,
                                    u32 value)
{
  if (!g_rom_write_capture)
    return;

  const uint32_t total = g_rom_write_total;
  g_rom_write_total = total + 1u;
  const uint32_t index = g_rom_write_count;
  if (index >= UART_DEBUG_ROM_WRITE_CAPACITY)
    return;

  g_rom_writes[index].pc = pc;
  g_rom_writes[index].address = address;
  g_rom_writes[index].value = value;
  g_rom_writes[index].bits = bits;
  g_rom_write_count = index + 1u;
}
