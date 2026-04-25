#include <stdbool.h>
#include <ctype.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "esp_partition.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <libretro.h>

#include "common.h"
#include "cpu.h"
#include "cpu_backend.h"
#include "gba_memory.h"
#include "main.h"

#include "dhry_result.h"

#if GPSP_CORES3SE_LCD
#include "esp32s3/cores3se_lcd.h"
#endif

#if defined(HAVE_DYNAREC)
uint32_t xtensa_jit_get_blocks_emitted(void);
uint32_t xtensa_jit_get_blocks_executed(void);
uint32_t xtensa_jit_get_compiled_arm_instructions(void);
uint32_t xtensa_jit_get_compiled_thumb_instructions(void);
uint32_t xtensa_jit_get_helper_arm_instructions_executed(void);
uint32_t xtensa_jit_get_helper_thumb_instructions_executed(void);
uint32_t xtensa_jit_get_interpreter_blocks_executed(void);
uint32_t xtensa_jit_get_generic_fallbacks(void);
uint32_t xtensa_jit_get_unsupported_opcodes(void);
uint32_t xtensa_jit_get_thumb_blocks(void);
#endif

#define DEBUG_MAX_PC_BREAKPOINTS 8

#ifndef GPSP_TEST_EXPECT_FB_HASH
#define GPSP_TEST_EXPECT_FB_HASH 0
#endif

#ifndef GPSP_TEST_DUMP_FRAME
#define GPSP_TEST_DUMP_FRAME 0
#endif

#ifndef USE_QEMU
#define USE_QEMU 0
#endif

#ifndef USE_DEBUG
#define USE_DEBUG 0
#endif

#define U32_ARG(value) ((uint32_t)(value))
#define GPSP_STRINGIFY_INNER(value) #value
#define GPSP_STRINGIFY(value) GPSP_STRINGIFY_INNER(value)

#define GPSP_GAMEPAK_PARTITION "gamepak"
#define GPSP_PLAY_FRAMESKIP_INTERVAL 1
#define GPSP_PLAY_STATUS_PERIOD_MS 2000
#define GPSP_PLAY_STATUS_STACK_WORDS 2048

static const char *TAG = "gpsp-esp32s3";
static const char *g_base_dir = ".";
static unsigned g_video_frames;
static uint32_t g_frame_hash;
static uint8_t *g_frame_capture;
static size_t g_frame_capture_size;
static unsigned g_frame_width;
static unsigned g_frame_height;
static bool g_lcd_ready;
static bool g_debug_io_trace_enabled;
static u32 g_debug_io_trace_start;
static u32 g_debug_io_trace_end;
static bool g_debug_pc_trace_enabled;
static u32 g_debug_pc_trace_start;
static u32 g_debug_pc_trace_end;
static u32 g_debug_pc_trace_remaining;
static const void *g_rom_mmap_data;
static size_t g_rom_mmap_size;
static esp_partition_mmap_handle_t g_rom_mmap_handle;
static bool g_rom_mapped;
static volatile bool g_play_status_enabled;
static volatile bool g_play_in_retro_run;
static volatile uint32_t g_play_loop_count;
static const char * volatile g_play_stage = "boot";
static TaskHandle_t g_play_status_task_handle;
static StaticTask_t g_play_status_task_buffer;
static StackType_t g_play_status_task_stack[GPSP_PLAY_STATUS_STACK_WORDS];

#define ESP32S3_FRAME_CAPTURE_CAPACITY \
  (GBA_SCREEN_WIDTH * GBA_SCREEN_HEIGHT * sizeof(uint16_t))

static GPSP_EXT_RAM_BSS uint8_t
  g_frame_capture_storage[ESP32S3_FRAME_CAPTURE_CAPACITY];

#define DEBUG_RECENT_TRACE_SIZE 64

typedef struct debug_recent_trace_entry
{
  u32 pc;
  u32 opcode;
  u32 cpsr;
  u32 thumb;
  u32 regs[16];
} debug_recent_trace_entry_t;

static debug_recent_trace_entry_t
  g_debug_recent_trace[DEBUG_RECENT_TRACE_SIZE];
static u32 g_debug_recent_trace_pos;
static u32 g_debug_recent_trace_count;
static bool g_debug_io_break_enabled;
static u32 g_debug_io_break_start;
static u32 g_debug_io_break_end;
static bool g_debug_step_enabled;
static u32 g_debug_step_budget;
static bool g_debug_stop_hit;
static u32 g_debug_stop_pc;
static u32 g_debug_stop_opcode;
static u32 g_debug_stop_thumb;
static u32 g_debug_stop_address;
static u32 g_debug_stop_bits;
static u32 g_debug_stop_value;

typedef enum debug_stop_reason
{
  DEBUG_STOP_NONE = 0,
  DEBUG_STOP_STEP,
  DEBUG_STOP_BREAKPC,
  DEBUG_STOP_BREAKIO
} debug_stop_reason_t;

typedef struct debug_pc_breakpoint
{
  bool enabled;
  u32 start;
  u32 end;
} debug_pc_breakpoint_t;

static debug_stop_reason_t g_debug_stop_reason;
static debug_pc_breakpoint_t g_debug_pc_breakpoints[DEBUG_MAX_PC_BREAKPOINTS];

typedef struct jit_counters
{
  uint32_t blocks_emitted;
  uint32_t blocks_executed;
  uint32_t compiled_arm_instructions;
  uint32_t compiled_thumb_instructions;
  uint32_t helper_arm_instructions;
  uint32_t helper_thumb_instructions;
  uint32_t interpreter_blocks_executed;
  uint32_t generic_fallbacks;
  uint32_t unsupported_opcodes;
  uint32_t thumb_blocks;
} jit_counters_t;

static bool frame_capture_enabled(void);
static u32 debug_parse_u32(const char *text, u32 fallback);

static void print_play_status(const char *source)
{
  printf("play status source=%s backend=%s qemu=%u debug=%u stage=%s"
         " loops=%" PRIu32 " in_run=%u frames=%" PRIu32 " video_frames=%u"
         " fb_hash=0x%08" PRIx32 " pc=0x%08" PRIx32 "\n",
         source, GPSP_TEST_BACKEND, USE_QEMU ? 1u : 0u,
         USE_DEBUG ? 1u : 0u, (const char *)g_play_stage,
         U32_ARG(g_play_loop_count), g_play_in_retro_run ? 1u : 0u,
         U32_ARG(frame_counter), (unsigned)g_video_frames,
         U32_ARG(g_frame_hash), U32_ARG(reg[REG_PC]));
  fflush(stdout);
}

static void play_status_task(void *arg)
{
  (void)arg;

  print_play_status("task_start");

  while (g_play_status_enabled)
  {
    vTaskDelay(pdMS_TO_TICKS(GPSP_PLAY_STATUS_PERIOD_MS));
    if (g_play_status_enabled)
      print_play_status("task");
  }

  g_play_status_task_handle = NULL;
  vTaskDelete(NULL);
}

static void start_play_status_task(void)
{
  if (strcmp(GPSP_TEST_MODE, "play") != 0 || g_play_status_task_handle)
    return;

  g_play_status_enabled = true;
  g_play_status_task_handle =
    xTaskCreateStatic(play_status_task, "gpsp_play_stat",
                      GPSP_PLAY_STATUS_STACK_WORDS, NULL,
                      tskIDLE_PRIORITY + 2,
                      g_play_status_task_stack,
                      &g_play_status_task_buffer);

  if (!g_play_status_task_handle)
    ESP_LOGE(TAG, "create play status task failed");
}

static void stop_play_status_task(void)
{
  g_play_status_enabled = false;
}

static void test_log_cb(enum retro_log_level level, const char *fmt, ...)
{
  va_list args;

  (void)level;
  va_start(args, fmt);
  vprintf(fmt, args);
  va_end(args);
}

static const char *lookup_variable(const char *key)
{
  if (strcmp(key, "gpsp_drc") == 0)
    return strcmp(GPSP_TEST_BACKEND, "dynarec") == 0 ? "enabled" : "disabled";
  if (strcmp(key, "gpsp_bios") == 0)
    return "builtin";
  if (strcmp(key, "gpsp_boot_mode") == 0)
    return "game";
  if (strcmp(key, "gpsp_frameskip") == 0)
    return strcmp(GPSP_TEST_MODE, "play") == 0 ? "fixed_interval" : "disabled";
  if (strcmp(key, "gpsp_frameskip_interval") == 0)
    return strcmp(GPSP_TEST_MODE, "play") == 0 ?
      GPSP_STRINGIFY(GPSP_PLAY_FRAMESKIP_INTERVAL) : "0";
  if (strcmp(key, "gpsp_color_correction") == 0)
    return "disabled";
  if (strcmp(key, "gpsp_frame_mixing") == 0)
    return "disabled";
  if (strcmp(key, "gpsp_rtc") == 0)
    return "disabled";
  if (strcmp(key, "gpsp_rumble") == 0)
    return "disabled";
  if (strcmp(key, "gpsp_serial") == 0)
    return "disabled";
  if (strcmp(key, "gpsp_sprlim") == 0)
    return "disabled";
  return NULL;
}

static bool env_cb(unsigned cmd, void *data)
{
  switch (cmd)
  {
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
      ((struct retro_log_callback *)data)->log = test_log_cb;
      return true;

    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
      return true;

    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
      *(const char **)data = g_base_dir;
      return true;

    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
      *(bool *)data = false;
      return true;

    case RETRO_ENVIRONMENT_GET_VARIABLE:
    {
      struct retro_variable *var = (struct retro_variable *)data;
      const char *value = lookup_variable(var->key);
      if (!value)
        return false;
      var->value = value;
      return true;
    }

    case RETRO_ENVIRONMENT_SET_MEMORY_MAPS:
    case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
    case RETRO_ENVIRONMENT_SET_FASTFORWARDING_OVERRIDE:
    case RETRO_ENVIRONMENT_SET_NETPACKET_INTERFACE:
    case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
      return true;

    default:
      return false;
  }
}

static void video_cb(const void *data, unsigned width, unsigned height, size_t pitch)
{
  const uint8_t *pixels = (const uint8_t *)data;
  size_t row_bytes;
  unsigned y;

  if (!pixels)
    return;

  row_bytes = (size_t)width * 2;

  for (y = 0; y < height; y++)
  {
    size_t x;
    const uint8_t *row = pixels + ((size_t)y * pitch);
    for (x = 0; x < row_bytes; x++)
    {
      g_frame_hash ^= row[x];
      g_frame_hash *= 16777619u;
    }
  }

  if (frame_capture_enabled())
  {
    size_t frame_size = row_bytes * height;

    if (frame_size != g_frame_capture_size)
    {
      if (frame_size <= sizeof(g_frame_capture_storage))
      {
        g_frame_capture = g_frame_capture_storage;
        g_frame_capture_size = frame_size;
      }
      else
      {
        g_frame_capture = NULL;
        g_frame_capture_size = 0;
      }
    }

    if (g_frame_capture)
    {
      for (y = 0; y < height; y++)
      {
        memcpy(g_frame_capture + ((size_t)y * row_bytes),
               pixels + ((size_t)y * pitch), row_bytes);
      }
      g_frame_width = width;
      g_frame_height = height;
    }
  }

#if GPSP_CORES3SE_LCD
  if (g_lcd_ready)
    (void)esp32s3_cores3se_lcd_present_rgb565(data, width, height, pitch);
#endif

  g_video_frames++;
}

static void dump_framebuffer_base64(void)
{
  static const char b64[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  size_t i;
  unsigned line_len = 0;

  if (!frame_capture_enabled() || !g_frame_capture || !g_frame_capture_size)
    return;

  printf("framebuffer_rgb565_base64_begin width=%u height=%u bytes=%zu"
         " hash=0x%08" PRIx32 "\n",
         g_frame_width, g_frame_height, g_frame_capture_size, g_frame_hash);

  for (i = 0; i < g_frame_capture_size; i += 3)
  {
    unsigned a = g_frame_capture[i];
    unsigned b = (i + 1 < g_frame_capture_size) ? g_frame_capture[i + 1] : 0;
    unsigned c = (i + 2 < g_frame_capture_size) ? g_frame_capture[i + 2] : 0;
    unsigned value = (a << 16) | (b << 8) | c;
    char out[4];
    unsigned j;

    out[0] = b64[(value >> 18) & 0x3F];
    out[1] = b64[(value >> 12) & 0x3F];
    out[2] = (i + 1 < g_frame_capture_size) ? b64[(value >> 6) & 0x3F] : '=';
    out[3] = (i + 2 < g_frame_capture_size) ? b64[value & 0x3F] : '=';

    for (j = 0; j < sizeof(out); j++)
    {
      putchar(out[j]);
      line_len++;
      if (line_len == 76)
      {
        putchar('\n');
        line_len = 0;
      }
    }
  }

  if (line_len != 0)
    putchar('\n');

  printf("framebuffer_rgb565_base64_end\n");
  fflush(stdout);
}

static bool frame_capture_enabled(void)
{
  return GPSP_TEST_DUMP_FRAME || strcmp(GPSP_TEST_MODE, "debug") == 0;
}

static void collect_jit_counters(jit_counters_t *counters)
{
  memset(counters, 0, sizeof(*counters));
#if defined(HAVE_DYNAREC)
  counters->blocks_emitted = xtensa_jit_get_blocks_emitted();
  counters->blocks_executed = xtensa_jit_get_blocks_executed();
  counters->compiled_arm_instructions =
    xtensa_jit_get_compiled_arm_instructions();
  counters->compiled_thumb_instructions =
    xtensa_jit_get_compiled_thumb_instructions();
  counters->helper_arm_instructions =
    xtensa_jit_get_helper_arm_instructions_executed();
  counters->helper_thumb_instructions =
    xtensa_jit_get_helper_thumb_instructions_executed();
  counters->interpreter_blocks_executed =
    xtensa_jit_get_interpreter_blocks_executed();
  counters->generic_fallbacks = xtensa_jit_get_generic_fallbacks();
  counters->unsupported_opcodes = xtensa_jit_get_unsupported_opcodes();
  counters->thumb_blocks = xtensa_jit_get_thumb_blocks();
#endif
}

static void print_jit_counters(const jit_counters_t *counters)
{
  printf("jit blocks_emitted=%" PRIu32 " blocks_executed=%" PRIu32
         " arm_insns_emitted=%" PRIu32 " thumb_insns_emitted=%" PRIu32
         " helper_arm_insns=%" PRIu32 " helper_thumb_insns=%" PRIu32
         " interp_blocks=%" PRIu32
         " generic_fallbacks=%" PRIu32 " unsupported=%" PRIu32
         " thumb_blocks=%" PRIu32 "\n",
         counters->blocks_emitted, counters->blocks_executed,
         counters->compiled_arm_instructions,
         counters->compiled_thumb_instructions,
         counters->helper_arm_instructions, counters->helper_thumb_instructions,
         counters->interpreter_blocks_executed, counters->generic_fallbacks,
         counters->unsupported_opcodes, counters->thumb_blocks);
}

static const char *debug_stop_reason_name(debug_stop_reason_t reason)
{
  switch (reason)
  {
    case DEBUG_STOP_STEP:
      return "step";
    case DEBUG_STOP_BREAKPC:
      return "breakpc";
    case DEBUG_STOP_BREAKIO:
      return "breakio";
    case DEBUG_STOP_NONE:
    default:
      return "none";
  }
}

static void debug_clear_stop(void)
{
  g_debug_stop_hit = false;
  g_debug_stop_reason = DEBUG_STOP_NONE;
  g_debug_stop_pc = 0;
  g_debug_stop_opcode = 0;
  g_debug_stop_thumb = 0;
  g_debug_stop_address = 0;
  g_debug_stop_bits = 0;
  g_debug_stop_value = 0;
}

static void debug_note_cpu_stop(debug_stop_reason_t reason, u32 pc, u32 opcode,
                                u32 thumb)
{
  if (g_debug_stop_hit)
    return;

  g_debug_stop_hit = true;
  g_debug_stop_reason = reason;
  g_debug_stop_pc = pc;
  g_debug_stop_opcode = opcode;
  g_debug_stop_thumb = thumb;
}

static void debug_note_io_stop(u32 address, u32 bits, u32 value)
{
  debug_note_cpu_stop(DEBUG_STOP_BREAKIO, reg[REG_PC], 0,
                      (reg[REG_CPSR] >> 5) & 1u);
  g_debug_stop_address = address;
  g_debug_stop_bits = bits;
  g_debug_stop_value = value;
}

static bool debug_range_contains(u32 start, u32 end, u32 value)
{
  return value >= start && value <= end;
}

static int debug_find_pc_breakpoint(u32 pc)
{
  unsigned i;

  for (i = 0; i < DEBUG_MAX_PC_BREAKPOINTS; i++)
  {
    if (g_debug_pc_breakpoints[i].enabled &&
        debug_range_contains(g_debug_pc_breakpoints[i].start,
                             g_debug_pc_breakpoints[i].end, pc))
      return (int)i;
  }

  return -1;
}

static void debug_print_stop(void)
{
  printf("dbg stop hit=%u reason=%s pc=0x%08" PRIx32
         " thumb=%" PRIu32 " opcode=0x%08" PRIx32
         " cpsr=0x%08" PRIx32,
         g_debug_stop_hit ? 1u : 0u,
         debug_stop_reason_name(g_debug_stop_reason), U32_ARG(g_debug_stop_pc),
         U32_ARG(g_debug_stop_thumb), U32_ARG(g_debug_stop_opcode),
         U32_ARG(reg[REG_CPSR]));

  if (g_debug_stop_reason == DEBUG_STOP_BREAKIO)
  {
    printf(" addr=0x%08" PRIx32 " bits=%" PRIu32 " value=0x%08" PRIx32,
           U32_ARG(g_debug_stop_address), U32_ARG(g_debug_stop_bits),
           U32_ARG(g_debug_stop_value));
  }

  putchar('\n');
}

static void debug_print_status(void)
{
  printf("dbg status backend=%s mode=%s frame_counter=%" PRIu32
         " video_frames=%u pc=0x%08" PRIx32 " cpsr=0x%08" PRIx32
         " cpu_mode=0x%02" PRIx32 " thumb=%" PRIu32
         " halt=%" PRIu32 " execute_cycles=%" PRIu32
         " sleep_cycles=0x%08" PRIx32 " fb_hash=0x%08" PRIx32 "\n",
         GPSP_TEST_BACKEND, GPSP_TEST_MODE, U32_ARG(frame_counter),
         g_video_frames, U32_ARG(reg[REG_PC]), U32_ARG(reg[REG_CPSR]),
         U32_ARG(reg[CPU_MODE]), U32_ARG((reg[REG_CPSR] >> 5) & 1u),
         U32_ARG(reg[CPU_HALT_STATE]), U32_ARG(execute_cycles),
         U32_ARG(reg[REG_SLEEP_CYCLES]), U32_ARG(g_frame_hash));
}

static void debug_print_regs(void)
{
  unsigned i;

  for (i = 0; i < 16; i += 4)
  {
    printf("dbg regs r%02u=0x%08" PRIx32 " r%02u=0x%08" PRIx32
           " r%02u=0x%08" PRIx32 " r%02u=0x%08" PRIx32 "\n",
           i, U32_ARG(reg[i]), i + 1, U32_ARG(reg[i + 1]),
           i + 2, U32_ARG(reg[i + 2]), i + 3, U32_ARG(reg[i + 3]));
  }

  printf("dbg regs cpsr=0x%08" PRIx32 " mode=0x%02" PRIx32
         " halt=%" PRIu32 " bus=0x%08" PRIx32 "\n",
         U32_ARG(reg[REG_CPSR]), U32_ARG(reg[CPU_MODE]),
         U32_ARG(reg[CPU_HALT_STATE]), U32_ARG(reg[REG_BUS_VALUE]));
}

static void debug_print_io(void)
{
  printf("dbg io dispcnt=0x%04x dispstat=0x%04x vcount=%u"
         " bg0cnt=0x%04x bg1cnt=0x%04x bg2cnt=0x%04x bg3cnt=0x%04x\n",
         read_ioreg(REG_DISPCNT), read_ioreg(REG_DISPSTAT),
         read_ioreg(REG_VCOUNT), read_ioreg(REG_BG0CNT),
         read_ioreg(REG_BG1CNT), read_ioreg(REG_BG2CNT),
         read_ioreg(REG_BG3CNT));
  printf("dbg io ie=0x%04x if=0x%04x ime=0x%04x waitcnt=0x%04x"
         " p1=0x%04x p1cnt=0x%04x\n",
         read_ioreg(REG_IE), read_ioreg(REG_IF), read_ioreg(REG_IME),
         read_ioreg(REG_WAITCNT), read_ioreg(REG_P1), read_ioreg(REG_P1CNT));
}

void gpsp_debug_trace_iowrite(u32 address, u32 bits, u32 value)
{
#if USE_DEBUG
  if (g_debug_io_trace_enabled &&
      debug_range_contains(g_debug_io_trace_start, g_debug_io_trace_end,
                           address))
  {
    printf("dbg iowrite pc=0x%08" PRIx32 " cpsr=0x%08" PRIx32
           " addr=0x%08" PRIx32 " bits=%" PRIu32 " value=0x%08" PRIx32 "\n",
           U32_ARG(reg[REG_PC]), U32_ARG(reg[REG_CPSR]),
           U32_ARG(address), U32_ARG(bits), U32_ARG(value));
  }

  if (g_debug_io_break_enabled &&
      debug_range_contains(g_debug_io_break_start, g_debug_io_break_end,
                           address))
  {
    debug_note_io_stop(address, bits, value);
  }
#else
  (void)address;
  (void)bits;
  (void)value;
#endif
}

void gpsp_debug_trace_cpu(u32 pc, u32 opcode, u32 thumb)
{
#if USE_DEBUG
  debug_recent_trace_entry_t *entry =
    &g_debug_recent_trace[g_debug_recent_trace_pos];
  entry->pc = pc;
  entry->opcode = opcode;
  entry->cpsr = reg[REG_CPSR];
  entry->thumb = thumb;
  memcpy(entry->regs, reg, sizeof(entry->regs));
  g_debug_recent_trace_pos =
    (g_debug_recent_trace_pos + 1) % DEBUG_RECENT_TRACE_SIZE;
  if (g_debug_recent_trace_count < DEBUG_RECENT_TRACE_SIZE)
    g_debug_recent_trace_count++;

  if (!g_debug_pc_trace_enabled)
    return;

  if (pc < g_debug_pc_trace_start || pc > g_debug_pc_trace_end)
    return;

  printf("dbg trace %s pc=0x%08" PRIx32 " opcode=0x%08" PRIx32
         " cpsr=0x%08" PRIx32 " r0=0x%08" PRIx32 " r1=0x%08" PRIx32
         " r2=0x%08" PRIx32 " r3=0x%08" PRIx32 " r8=0x%08" PRIx32
         " r9=0x%08" PRIx32 " r10=0x%08" PRIx32 " r11=0x%08" PRIx32
         " r12=0x%08" PRIx32 " sp=0x%08" PRIx32
         " lr=0x%08" PRIx32 "\n",
         thumb ? "thumb" : "arm", U32_ARG(pc), U32_ARG(opcode),
         U32_ARG(reg[REG_CPSR]), U32_ARG(reg[0]), U32_ARG(reg[1]),
         U32_ARG(reg[2]), U32_ARG(reg[3]), U32_ARG(reg[8]),
         U32_ARG(reg[9]), U32_ARG(reg[10]), U32_ARG(reg[11]),
         U32_ARG(reg[12]), U32_ARG(reg[REG_SP]), U32_ARG(reg[REG_LR]));

  if (g_debug_pc_trace_remaining != 0)
  {
    g_debug_pc_trace_remaining--;
    if (g_debug_pc_trace_remaining == 0)
    {
      g_debug_pc_trace_enabled = false;
      printf("dbg tracepc limit_reached\n");
    }
  }
#else
  (void)pc;
  (void)opcode;
  (void)thumb;
#endif
}

void gpsp_debug_dump_recent_cpu_trace(void)
{
#if USE_DEBUG
  u32 start = (g_debug_recent_trace_pos + DEBUG_RECENT_TRACE_SIZE -
               g_debug_recent_trace_count) % DEBUG_RECENT_TRACE_SIZE;

  printf("dbg recent count=%" PRIu32 "\n",
         U32_ARG(g_debug_recent_trace_count));
  for (u32 i = 0; i < g_debug_recent_trace_count; i++)
  {
    const debug_recent_trace_entry_t *entry =
      &g_debug_recent_trace[(start + i) % DEBUG_RECENT_TRACE_SIZE];
    printf("dbg recent %02" PRIu32 " %s pc=0x%08" PRIx32
           " opcode=0x%08" PRIx32 " cpsr=0x%08" PRIx32
           " r0=0x%08" PRIx32 " r1=0x%08" PRIx32
           " r2=0x%08" PRIx32 " r3=0x%08" PRIx32
           " r8=0x%08" PRIx32 " r9=0x%08" PRIx32
           " r10=0x%08" PRIx32 " r11=0x%08" PRIx32
           " r12=0x%08" PRIx32 " sp=0x%08" PRIx32
           " lr=0x%08" PRIx32 "\n",
           U32_ARG(i), entry->thumb ? "thumb" : "arm", U32_ARG(entry->pc),
           U32_ARG(entry->opcode), U32_ARG(entry->cpsr),
           U32_ARG(entry->regs[0]), U32_ARG(entry->regs[1]),
           U32_ARG(entry->regs[2]), U32_ARG(entry->regs[3]),
           U32_ARG(entry->regs[8]), U32_ARG(entry->regs[9]),
           U32_ARG(entry->regs[10]), U32_ARG(entry->regs[11]),
           U32_ARG(entry->regs[12]), U32_ARG(entry->regs[REG_SP]),
           U32_ARG(entry->regs[REG_LR]));
  }
#endif
}

bool gpsp_debug_cpu_should_break(u32 pc, u32 opcode, u32 thumb)
{
#if USE_DEBUG
  int breakpoint_index;

  if (g_debug_stop_hit)
    return true;

  breakpoint_index = debug_find_pc_breakpoint(pc);
  if (breakpoint_index >= 0)
  {
    printf("dbg breakpc hit index=%d pc=0x%08" PRIx32 "\n",
           breakpoint_index, U32_ARG(pc));
    debug_note_cpu_stop(DEBUG_STOP_BREAKPC, pc, opcode, thumb);
    return true;
  }

  if (g_debug_step_enabled)
  {
    if (g_debug_step_budget == 0)
    {
      g_debug_step_enabled = false;
      debug_note_cpu_stop(DEBUG_STOP_STEP, pc, opcode, thumb);
      return true;
    }

    g_debug_step_budget--;
  }

  return false;
#else
  (void)pc;
  (void)opcode;
  (void)thumb;
  return false;
#endif
}

bool gpsp_debug_cpu_stop_requested(void)
{
#if USE_DEBUG
  return g_debug_stop_hit;
#else
  return false;
#endif
}

static void debug_print_current_opcode(void)
{
  if (reg[REG_CPSR] & 0x20)
  {
    u32 pc = reg[REG_PC] & ~1u;
    printf("dbg op thumb pc=0x%08" PRIx32 " opcode=0x%04" PRIx32 "\n",
           U32_ARG(pc), U32_ARG(read_memory16(pc) & 0xFFFFu));
  }
  else
  {
    u32 pc = reg[REG_PC] & ~3u;
    printf("dbg op arm pc=0x%08" PRIx32 " opcode=0x%08" PRIx32 "\n",
           U32_ARG(pc), U32_ARG(read_memory32(pc)));
  }
}

static void debug_dump_memory(u32 address, u32 length)
{
  u32 offset;

  if (length > 512)
    length = 512;

  printf("dbg mem addr=0x%08" PRIx32 " len=%" PRIu32 "\n",
         U32_ARG(address), U32_ARG(length));

  for (offset = 0; offset < length; offset++)
  {
    if ((offset & 0x0F) == 0)
      printf("0x%08" PRIx32 ":", U32_ARG(address + offset));

    printf(" %02" PRIx32, U32_ARG(read_memory8(address + offset) & 0xFFu));

    if ((offset & 0x0F) == 0x0F || offset + 1 == length)
      putchar('\n');
  }
}

static void debug_print_framebuffer(void)
{
  printf("dbg fb width=%u height=%u bytes=%zu hash=0x%08" PRIx32
         " captured=%u video_frames=%u\n",
         g_frame_width, g_frame_height, g_frame_capture_size, g_frame_hash,
         g_frame_capture ? 1u : 0u, g_video_frames);
}

static void debug_print_breakpoints(void)
{
  unsigned i;
  bool any = false;

  for (i = 0; i < DEBUG_MAX_PC_BREAKPOINTS; i++)
  {
    if (!g_debug_pc_breakpoints[i].enabled)
      continue;

    any = true;
    printf("dbg bp index=%u start=0x%08" PRIx32 " end=0x%08" PRIx32 "\n",
           i, U32_ARG(g_debug_pc_breakpoints[i].start),
           U32_ARG(g_debug_pc_breakpoints[i].end));
  }

  if (!any)
    printf("dbg bp none\n");

  if (g_debug_io_break_enabled)
  {
    printf("dbg breakio start=0x%08" PRIx32 " end=0x%08" PRIx32 "\n",
           U32_ARG(g_debug_io_break_start), U32_ARG(g_debug_io_break_end));
  }
  else
  {
    printf("dbg breakio off\n");
  }
}

static void debug_add_pc_breakpoint(u32 start, u32 length)
{
  unsigned i;

  for (i = 0; i < DEBUG_MAX_PC_BREAKPOINTS; i++)
  {
    if (g_debug_pc_breakpoints[i].enabled)
      continue;

    g_debug_pc_breakpoints[i].enabled = true;
    g_debug_pc_breakpoints[i].start = start;
    g_debug_pc_breakpoints[i].end = start + (length ? length - 1 : 0);
    printf("dbg bp index=%u start=0x%08" PRIx32 " end=0x%08" PRIx32 "\n",
           i, U32_ARG(g_debug_pc_breakpoints[i].start),
           U32_ARG(g_debug_pc_breakpoints[i].end));
    return;
  }

  printf("dbg error bp_full max=%u\n", DEBUG_MAX_PC_BREAKPOINTS);
}

static void debug_clear_pc_breakpoint(const char *text)
{
  unsigned i;

  if (!text || strcmp(text, "all") == 0)
  {
    for (i = 0; i < DEBUG_MAX_PC_BREAKPOINTS; i++)
      g_debug_pc_breakpoints[i].enabled = false;
    printf("dbg bp cleared=all\n");
    return;
  }

  i = (unsigned)debug_parse_u32(text, DEBUG_MAX_PC_BREAKPOINTS);
  if (i >= DEBUG_MAX_PC_BREAKPOINTS)
  {
    printf("dbg error bad_bp_index=%s\n", text);
    return;
  }

  g_debug_pc_breakpoints[i].enabled = false;
  printf("dbg bp cleared=%u\n", i);
}

static void debug_print_help(void)
{
  printf("dbg help commands:\n");
  printf("dbg help   - print this help\n");
  printf("dbg status - print PC, mode, frame, halt, framebuffer hash\n");
  printf("dbg regs   - print ARM registers\n");
  printf("dbg stop   - print last debugger stop reason\n");
  printf("dbg bp [A L] - list or add PC breakpoint range\n");
  printf("dbg bc [N|all] - clear one/all PC breakpoints\n");
  printf("dbg breakio A L - stop after IO write in byte range, off disables\n");
  printf("dbg jit    - print ESP32-S3 JIT counters\n");
  printf("dbg io     - print display and interrupt IO registers\n");
  printf("dbg op     - print current ARM/Thumb opcode\n");
  printf("dbg run N  - execute N video frames, default 1\n");
  printf("dbg cont N - continue up to N CPU scheduler exits, default 1\n");
  printf("dbg stepi N - execute N ARM/Thumb instructions, default 1\n");
  printf("dbg mem A L - dump up to 512 bytes using GBA memory reads\n");
  printf("dbg watchio A L - trace IO writes in byte range, off disables\n");
  printf("dbg tracepc A L N - trace CPU PCs in byte range, off disables\n");
  printf("dbg fb     - print captured framebuffer metadata\n");
  printf("dbg png    - dump captured RGB565 framebuffer as base64\n");
  printf("dbg quit   - unload ROM and exit app_main\n");
}

static bool debug_read_line(char *buffer, size_t size)
{
  size_t length = 0;
  int ch;

  while ((ch = getchar()) != EOF)
  {
    if (ch == '\r' || ch == '\n')
      break;

    if ((ch == '\b' || ch == 0x7F) && length > 0)
    {
      length--;
      continue;
    }

    if (length + 1 < size)
      buffer[length++] = (char)ch;
  }

  buffer[length] = '\0';
  return ch != EOF || length != 0;
}

static char *debug_next_token(char **cursor)
{
  char *start = *cursor;

  while (*start && isspace((unsigned char)*start))
    start++;

  if (!*start)
  {
    *cursor = start;
    return NULL;
  }

  *cursor = start;
  while (**cursor && !isspace((unsigned char)**cursor))
    (*cursor)++;

  if (**cursor)
  {
    **cursor = '\0';
    (*cursor)++;
  }

  return start;
}

static u32 debug_parse_u32(const char *text, u32 fallback)
{
  char *end = NULL;
  unsigned long value;

  if (!text)
    return fallback;

  value = strtoul(text, &end, 0);
  if (end == text)
    return fallback;

  return (u32)value;
}

static void debug_run_frames(unsigned count)
{
  unsigned i;

  if (count == 0)
    count = 1;

  for (i = 0; i < count; i++)
  {
    retro_run();
    if (g_debug_stop_hit)
      break;
  }
}

static void debug_run_video_frames(unsigned count)
{
  debug_clear_stop();
  debug_run_frames(count);
  debug_print_status();
  if (g_debug_stop_hit)
    debug_print_stop();
}

static void debug_continue_cpu(unsigned count)
{
  unsigned i;

  if (count == 0)
    count = 1;

  debug_clear_stop();

  for (i = 0; i < count; i++)
  {
    u32 start_frame = frame_counter;
    cpu_backend_execute(execute_cycles);

    if (g_debug_stop_hit || frame_counter != start_frame)
      break;
  }

  debug_print_status();
  if (g_debug_stop_hit)
    debug_print_stop();
}

static void debug_step_instructions(unsigned count)
{
  if (count == 0)
    count = 1;

  debug_clear_stop();
  g_debug_step_enabled = true;
  g_debug_step_budget = count;
  cpu_backend_execute(execute_cycles);
  g_debug_step_enabled = false;

  debug_print_status();
  debug_print_stop();
}

static bool debug_handle_command(char *line)
{
  char *cursor = line;
  char *command = debug_next_token(&cursor);
  jit_counters_t counters;
  char *arg0;
  char *arg1;

  if (!command)
    return true;

  if (strcmp(command, "h") == 0 || strcmp(command, "help") == 0 ||
      strcmp(command, "?") == 0)
  {
    debug_print_help();
  }
  else if (strcmp(command, "s") == 0 || strcmp(command, "status") == 0)
  {
    debug_print_status();
  }
  else if (strcmp(command, "r") == 0 || strcmp(command, "regs") == 0)
  {
    debug_print_regs();
  }
  else if (strcmp(command, "stop") == 0)
  {
    debug_print_stop();
  }
  else if (strcmp(command, "bp") == 0 || strcmp(command, "breakpc") == 0)
  {
    arg0 = debug_next_token(&cursor);
    arg1 = debug_next_token(&cursor);

    if (!arg0)
      debug_print_breakpoints();
    else
      debug_add_pc_breakpoint(debug_parse_u32(arg0, reg[REG_PC]),
                              debug_parse_u32(arg1, 1));
  }
  else if (strcmp(command, "bc") == 0 || strcmp(command, "clearbp") == 0)
  {
    arg0 = debug_next_token(&cursor);
    debug_clear_pc_breakpoint(arg0);
  }
  else if (strcmp(command, "j") == 0 || strcmp(command, "jit") == 0)
  {
    collect_jit_counters(&counters);
    print_jit_counters(&counters);
  }
  else if (strcmp(command, "io") == 0)
  {
    debug_print_io();
  }
  else if (strcmp(command, "op") == 0)
  {
    debug_print_current_opcode();
  }
  else if (strcmp(command, "f") == 0 || strcmp(command, "run") == 0)
  {
    arg0 = debug_next_token(&cursor);
    debug_run_video_frames((unsigned)debug_parse_u32(arg0, 1));
  }
  else if (strcmp(command, "c") == 0 || strcmp(command, "cont") == 0 ||
           strcmp(command, "continue") == 0)
  {
    arg0 = debug_next_token(&cursor);
    debug_continue_cpu((unsigned)debug_parse_u32(arg0, 1));
  }
  else if (strcmp(command, "step") == 0 || strcmp(command, "stepi") == 0 ||
           strcmp(command, "si") == 0)
  {
    arg0 = debug_next_token(&cursor);
    debug_step_instructions((unsigned)debug_parse_u32(arg0, 1));
  }
  else if (strcmp(command, "m") == 0 || strcmp(command, "mem") == 0)
  {
    arg0 = debug_next_token(&cursor);
    arg1 = debug_next_token(&cursor);
    debug_dump_memory(debug_parse_u32(arg0, reg[REG_PC]),
                      debug_parse_u32(arg1, 64));
  }
  else if (strcmp(command, "watchio") == 0)
  {
    arg0 = debug_next_token(&cursor);
    arg1 = debug_next_token(&cursor);

    if (arg0 && strcmp(arg0, "off") == 0)
    {
      g_debug_io_trace_enabled = false;
      printf("dbg watchio off\n");
    }
    else
    {
      u32 start = debug_parse_u32(arg0, 0x04000000);
      u32 length = debug_parse_u32(arg1, 0x400);
      g_debug_io_trace_start = start;
      g_debug_io_trace_end = start + (length ? length - 1 : 0);
      g_debug_io_trace_enabled = true;
      printf("dbg watchio start=0x%08" PRIx32 " end=0x%08" PRIx32 "\n",
             U32_ARG(g_debug_io_trace_start), U32_ARG(g_debug_io_trace_end));
    }
  }
  else if (strcmp(command, "breakio") == 0)
  {
    arg0 = debug_next_token(&cursor);
    arg1 = debug_next_token(&cursor);

    if (arg0 && strcmp(arg0, "off") == 0)
    {
      g_debug_io_break_enabled = false;
      printf("dbg breakio off\n");
    }
    else
    {
      u32 start = debug_parse_u32(arg0, 0x04000000);
      u32 length = debug_parse_u32(arg1, 0x400);
      g_debug_io_break_start = start;
      g_debug_io_break_end = start + (length ? length - 1 : 0);
      g_debug_io_break_enabled = true;
      printf("dbg breakio start=0x%08" PRIx32 " end=0x%08" PRIx32 "\n",
             U32_ARG(g_debug_io_break_start), U32_ARG(g_debug_io_break_end));
    }
  }
  else if (strcmp(command, "tracepc") == 0)
  {
    arg0 = debug_next_token(&cursor);
    arg1 = debug_next_token(&cursor);

    if (arg0 && strcmp(arg0, "off") == 0)
    {
      g_debug_pc_trace_enabled = false;
      printf("dbg tracepc off\n");
    }
    else
    {
      char *arg2 = debug_next_token(&cursor);
      u32 start = debug_parse_u32(arg0, reg[REG_PC]);
      u32 length = debug_parse_u32(arg1, 0x100);
      g_debug_pc_trace_start = start;
      g_debug_pc_trace_end = start + (length ? length - 1 : 0);
      g_debug_pc_trace_remaining = debug_parse_u32(arg2, 128);
      g_debug_pc_trace_enabled = true;
      printf("dbg tracepc start=0x%08" PRIx32 " end=0x%08" PRIx32
             " remaining=%" PRIu32 "\n",
             U32_ARG(g_debug_pc_trace_start), U32_ARG(g_debug_pc_trace_end),
             U32_ARG(g_debug_pc_trace_remaining));
    }
  }
  else if (strcmp(command, "fb") == 0)
  {
    debug_print_framebuffer();
  }
  else if (strcmp(command, "png") == 0)
  {
    dump_framebuffer_base64();
  }
  else if (strcmp(command, "q") == 0 || strcmp(command, "quit") == 0)
  {
    printf("dbg quit\n");
    return false;
  }
  else
  {
    printf("dbg error unknown_command=%s\n", command);
  }

  fflush(stdout);
  return true;
}

static bool map_gamepak_partition(struct retro_game_info *info)
{
  const esp_partition_t *partition;
  esp_err_t err;

  if (!info)
    return false;

  if (g_rom_mapped)
  {
    info->data = g_rom_mmap_data;
    info->size = g_rom_mmap_size;
    return true;
  }

  partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                       ESP_PARTITION_SUBTYPE_ANY,
                                       GPSP_GAMEPAK_PARTITION);
  if (!partition)
  {
    ESP_LOGE(TAG, "ROM partition '%s' not found", GPSP_GAMEPAK_PARTITION);
    return false;
  }

  err = esp_partition_mmap(partition, 0, partition->size,
                           ESP_PARTITION_MMAP_DATA, &g_rom_mmap_data,
                           &g_rom_mmap_handle);
  if (err != ESP_OK)
  {
    ESP_LOGE(TAG, "mmap ROM partition '%s': %s", GPSP_GAMEPAK_PARTITION,
             esp_err_to_name(err));
    return false;
  }

  g_rom_mmap_size = partition->size;
  g_rom_mapped = true;
  info->data = g_rom_mmap_data;
  info->size = g_rom_mmap_size;

  ESP_LOGI(TAG, "mapped ROM partition '%s': flash=0x%08" PRIx32
                " size=%zu",
           GPSP_GAMEPAK_PARTITION, U32_ARG(partition->address),
           g_rom_mmap_size);
  return true;
}

static void unmap_gamepak_partition(void)
{
  if (!g_rom_mapped)
    return;

  esp_partition_munmap(g_rom_mmap_handle);
  g_rom_mmap_data = NULL;
  g_rom_mmap_size = 0;
  g_rom_mmap_handle = 0;
  g_rom_mapped = false;
}

static void run_debugger(void)
{
  char line[128];

  printf("dbg ready backend=%s rom_bytes=%zu\n", GPSP_TEST_BACKEND,
         g_rom_mmap_size);
  debug_print_help();
  debug_print_status();

  while (1)
  {
    printf("dbg> ");
    fflush(stdout);

    if (!debug_read_line(line, sizeof(line)))
      break;

    if (!debug_handle_command(line))
      break;
  }
}

static void run_play_loop(void)
{
  printf("play ready backend=%s rom_bytes=%zu\n", GPSP_TEST_BACKEND,
         g_rom_mmap_size);
  fflush(stdout);
  print_play_status("ready");

  while (1)
  {
    g_play_stage = "retro_run";
    g_play_in_retro_run = true;
    retro_run();
    g_play_in_retro_run = false;
    g_play_loop_count++;

    g_play_stage = "yield";
    vTaskDelay(1);
  }
}

static void audio_cb(int16_t left, int16_t right)
{
  (void)left;
  (void)right;
}

static size_t audio_batch_cb(const int16_t *data, size_t frames)
{
  (void)data;
  return frames;
}

static void input_poll_cb(void)
{
}

static int16_t input_state_cb(unsigned port, unsigned device, unsigned index, unsigned id)
{
  (void)port;
  (void)device;
  (void)index;
  (void)id;
  return 0;
}

static int run_test(void)
{
  struct retro_game_info info;
  const dhry_result_t *result;
  const uint8_t *system_ram;
  size_t system_ram_size;
  unsigned max_frames = GPSP_TEST_FRAMES;
  unsigned frame;
  unsigned frames_run = 0;
  jit_counters_t jit_counters;

  memset(&info, 0, sizeof(info));
  g_video_frames = 0;
  g_frame_hash = 2166136261u;
  g_frame_width = 0;
  g_frame_height = 0;
  g_play_in_retro_run = false;
  g_play_loop_count = 0;
  g_play_stage = "map_gamepak";
  start_play_status_task();

  if (!map_gamepak_partition(&info))
  {
    stop_play_status_task();
    return 1;
  }

#if GPSP_CORES3SE_LCD
  g_play_stage = "lcd_init";
  g_lcd_ready = esp32s3_cores3se_lcd_init();
#endif

  g_play_stage = "retro_init";
  retro_set_environment(env_cb);
  retro_set_video_refresh(video_cb);
  retro_set_audio_sample(audio_cb);
  retro_set_audio_sample_batch(audio_batch_cb);
  retro_set_input_poll(input_poll_cb);
  retro_set_input_state(input_state_cb);
  retro_init();

  g_play_stage = "retro_load_game";
  if (!retro_load_game(&info))
  {
    ESP_LOGE(TAG, "retro_load_game failed");
    stop_play_status_task();
    retro_deinit();
    unmap_gamepak_partition();
    return 1;
  }

  if (strcmp(GPSP_TEST_MODE, "debug") == 0)
  {
    stop_play_status_task();
    run_debugger();
    retro_unload_game();
    retro_deinit();
    unmap_gamepak_partition();
    printf("result=PASS backend=%s mode=debug\n", GPSP_TEST_BACKEND);
    return 0;
  }

  if (strcmp(GPSP_TEST_MODE, "play") == 0)
  {
    g_play_stage = "ready";
    run_play_loop();
    return 0;
  }

  stop_play_status_task();
  system_ram = (const uint8_t *)retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM);
  system_ram_size = retro_get_memory_size(RETRO_MEMORY_SYSTEM_RAM);
  if (!system_ram || system_ram_size < sizeof(dhry_result_t))
  {
    ESP_LOGE(TAG, "system RAM mapping unavailable");
    retro_unload_game();
    retro_deinit();
    unmap_gamepak_partition();
    return 1;
  }

  result = (const dhry_result_t *)system_ram;
  for (frame = 0; frame < max_frames; frame++)
  {
    retro_run();
    frames_run++;
    if (strcmp(GPSP_TEST_MODE, "dhrystone") == 0 &&
        result->magic == DHRY_RESULT_MAGIC)
      break;
  }

  printf("backend=%s mode=%s frames=%u video_frames=%u fb_hash=0x%08" PRIx32
         " magic=0x%08" PRIx32
         " status=%" PRIu32
         " iterations=%" PRIu32 " int_glob=%" PRId32 " bool_glob=%" PRId32
         " ch1=%c ch2=%c arr1_8=%" PRId32 " arr2_8_7=%" PRId32
         " ptr=%" PRId32 " next=%" PRId32 "\n",
         GPSP_TEST_BACKEND, GPSP_TEST_MODE, frames_run, g_video_frames,
         g_frame_hash, result->magic, result->status, result->iterations,
         result->int_glob, result->bool_glob, (char)result->ch_1_glob,
         (char)result->ch_2_glob,
         result->arr_1_8, result->arr_2_8_7, result->ptr_int_comp,
         result->next_ptr_int_comp);

  collect_jit_counters(&jit_counters);
  print_jit_counters(&jit_counters);

  dump_framebuffer_base64();

  retro_unload_game();
  retro_deinit();
  unmap_gamepak_partition();

  if (strcmp(GPSP_TEST_MODE, "dhrystone") == 0 &&
      (result->magic != DHRY_RESULT_MAGIC ||
       result->status != DHRY_STATUS_PASS))
  {
    printf("result=FAIL backend=%s\n", GPSP_TEST_BACKEND);
    return 1;
  }

  if (strcmp(GPSP_TEST_MODE, "frames") == 0 && g_video_frames == 0)
  {
    printf("result=FAIL backend=%s no_video_frames\n", GPSP_TEST_BACKEND);
    return 1;
  }

  if (strcmp(GPSP_TEST_MODE, "frames") == 0 &&
      GPSP_TEST_EXPECT_FB_HASH != 0 &&
      g_frame_hash != GPSP_TEST_EXPECT_FB_HASH)
  {
    printf("result=FAIL backend=%s fb_hash=0x%08" PRIx32
           " expected=0x%08x\n",
           GPSP_TEST_BACKEND, U32_ARG(g_frame_hash), GPSP_TEST_EXPECT_FB_HASH);
    return 1;
  }

#if defined(HAVE_DYNAREC)
  if (strcmp(GPSP_TEST_BACKEND, "dynarec") == 0)
  {
    if (jit_counters.blocks_executed == 0 ||
        (jit_counters.compiled_arm_instructions +
         jit_counters.compiled_thumb_instructions) == 0 ||
        jit_counters.interpreter_blocks_executed != 0 ||
        jit_counters.generic_fallbacks != 0 ||
        jit_counters.unsupported_opcodes != 0 ||
        (strcmp(GPSP_TEST_MODE, "dhrystone") == 0 &&
         jit_counters.thumb_blocks != 0))
    {
      printf("result=FAIL backend=%s jit_blocks=%" PRIu32
             " arm_insns=%" PRIu32 " thumb_insns=%" PRIu32
             " interp_blocks=%" PRIu32
             " fallbacks=%" PRIu32 " unsupported=%" PRIu32
             " thumb=%" PRIu32 "\n",
             GPSP_TEST_BACKEND, jit_counters.blocks_executed,
             jit_counters.compiled_arm_instructions,
             jit_counters.compiled_thumb_instructions,
             jit_counters.interpreter_blocks_executed,
             jit_counters.generic_fallbacks, jit_counters.unsupported_opcodes,
             jit_counters.thumb_blocks);
      return 1;
    }
  }
#endif

  printf("result=PASS backend=%s\n", GPSP_TEST_BACKEND);
  return 0;
}

void app_main(void)
{
  int rc = run_test();
  fflush(stdout);
  ESP_LOGI(TAG, "run_test rc=%d", rc);
}
