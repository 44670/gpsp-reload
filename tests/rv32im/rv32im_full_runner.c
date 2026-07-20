#include <stdbool.h>
#include <stdint.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include "../../common.h"
#include "../../cpu.h"
#include "../../gba_memory.h"
#include "../../libretro/libretro-common/include/libretro.h"
#include "../../main.h"
#include "../../riscv/riscv_emit.h"

extern int _open(const char *path, int flags, int mode);
extern int _close(int fd);
extern ssize_t _read(int fd, void *buffer, size_t length);
extern ssize_t _write(int fd, const void *buffer, size_t length);

static const char *g_base_dir = "/tmp";
static enum retro_pixel_format g_pixel_format = RETRO_PIXEL_FORMAT_RGB565;
static uint32_t g_frame_hash;
static unsigned g_frame_width;
static unsigned g_frame_height;
static size_t g_frame_pitch;
static unsigned g_video_frames;
static unsigned g_current_frame;
static unsigned g_last_frame_hash_change;
static uint16_t g_latest_frame[240u * 160u];
static unsigned g_start_frame = 600u;
static unsigned g_start_duration = 2u;
static unsigned g_second_start_frame = UINT32_MAX;
static unsigned g_second_start_duration = 2u;
static unsigned g_third_start_frame = 1800u;
static unsigned g_third_start_duration = 5u;
static uint32_t g_second_start_hash = 0xe888423fu;
static unsigned g_second_start_stable_frames = 8u;
static unsigned g_second_start_hash_streak;
static bool g_second_start_auto_triggered;
static bool g_use_dynarec = true;
static unsigned g_switch_frame = UINT32_MAX;
static bool g_switch_to_dynarec;
static u32 g_thumb_native_disable_mask;
static u32 g_arm_native_disable_mask;
static unsigned g_irq_flag_writes;
static unsigned g_irq_flag_bit0_set_writes;
static unsigned g_post_start_irq_flag_writes;
static unsigned g_post_start_bit0_set_writes;
static unsigned g_last_bit0_set_frame = UINT32_MAX;
static uint32_t g_last_bit0_set_pc;
static uint16_t g_last_irq_flag_value;
static uint16_t g_last_sampled_irq_flags;
static uint16_t g_current_input_mask;
static unsigned g_input_event_active_frames;
static u32 g_auto_a_until_callback;
static unsigned g_auto_a_start_frame;
static unsigned g_auto_a_period = 60u;
static unsigned g_auto_a_reached_frame = UINT32_MAX;

typedef struct scheduled_input_event
{
  unsigned frame;
  unsigned duration;
  uint16_t mask;
} scheduled_input_event;

enum { INPUT_EVENT_CAPACITY = 128 };
static scheduled_input_event g_input_events[INPUT_EVENT_CAPACITY];
static unsigned g_input_event_count;

typedef struct block_trace_entry
{
  uint32_t start_pc;
  uint32_t end_pc;
  uint32_t thumb;
  unsigned frame;
} block_trace_entry;

enum { BLOCK_TRACE_CAPACITY = 256 };
static block_trace_entry g_block_trace[BLOCK_TRACE_CAPACITY];
static unsigned g_block_trace_total;
static unsigned g_bios_native_blocks;
static unsigned g_irq_dispatch_blocks;
static unsigned g_vblank_callback_blocks;
static unsigned g_vcount_callback_blocks;
static unsigned g_vblank_wait_blocks;
static unsigned g_post_start_wait_entry_blocks;
static unsigned g_post_start_vblank_tail_blocks;
static unsigned g_post_start_bit0_sample_frames;
static unsigned g_post_start_bit0_sample_transitions;
static unsigned g_irq_dispatch_hist[(0x158u / 4u)];
static unsigned g_vblank_callback_hist[(0x0a4u / 2u)];
static unsigned g_vblank_wait_hist[(0x028u / 2u)];

typedef struct fault_block_trace_entry
{
  uint32_t start_pc;
  uint32_t end_pc;
  uint32_t next_pc;
  uint32_t sp;
  uint32_t lr;
  uint32_t r0;
  uint32_t r1;
  uint32_t r2;
  uint32_t r3;
  uint32_t cpsr;
} fault_block_trace_entry;

enum { FAULT_BLOCK_TRACE_CAPACITY = 64 };
static fault_block_trace_entry
  g_fault_block_trace[FAULT_BLOCK_TRACE_CAPACITY];
static unsigned g_fault_block_trace_total;

volatile u32 riscv_runtime_perf_disable_mapped_alu_fastpath;
volatile u32 riscv_runtime_perf_disable_fast_ram_reads;
volatile u32 riscv_runtime_perf_disable_fast_ram_stores;
volatile u32 riscv_runtime_perf_disable_entry_setup_opt;
volatile u32 riscv_runtime_perf_disable_state_helper_opt;
volatile u32 riscv_runtime_perf_disable_validated_entry_opt;
volatile u32 riscv_runtime_perf_disable_indirect_lookup_cache;

static bool post_start_observation_active(void);

void riscv_note_runtime_block_execute(u32 start_pc, u32 end_pc, u32 thumb)
{
  bool interesting = false;
  bool post_start = post_start_observation_active();
  const u32 next_pc = reg[REG_PC];
  fault_block_trace_entry *fault_entry =
    &g_fault_block_trace[g_fault_block_trace_total %
                         FAULT_BLOCK_TRACE_CAPACITY];

  fault_entry->start_pc = start_pc;
  fault_entry->end_pc = end_pc;
  fault_entry->next_pc = next_pc;
  fault_entry->sp = reg[REG_SP];
  fault_entry->lr = reg[REG_LR];
  fault_entry->r0 = reg[0];
  fault_entry->r1 = reg[1];
  fault_entry->r2 = reg[2];
  fault_entry->r3 = reg[3];
  fault_entry->cpsr = reg[REG_CPSR];
  g_fault_block_trace_total++;

  if (start_pc < 0x00004000u)
    g_bios_native_blocks++;

  /*
   * A translated block must leave the guest PC in the GBA address space.
   * Stop at the producing block instead of letting the subsequent lookup
   * fall back to the interpreter and fault while fetching from the corrupt
   * address; the latter loses the block that actually caused the damage.
   */
  if ((next_pc & ~1u) >= 0x10000000u)
  {
    const unsigned retained =
      g_fault_block_trace_total < FAULT_BLOCK_TRACE_CAPACITY ?
      g_fault_block_trace_total : FAULT_BLOCK_TRACE_CAPACITY;
    const unsigned first = g_fault_block_trace_total - retained;

    printf("result=FAIL command=p-gba reason=invalid_guest_pc "
           "frame=%u start=0x%08x end=0x%08x thumb=%u "
           "next=0x%08x cpsr=0x%08x "
           "r0=0x%08x r1=0x%08x r2=0x%08x r3=0x%08x "
           "r4=0x%08x r5=0x%08x r6=0x%08x r7=0x%08x "
           "r8=0x%08x r9=0x%08x r10=0x%08x r11=0x%08x "
           "r12=0x%08x sp=0x%08x lr=0x%08x\n",
           g_current_frame, start_pc, end_pc, thumb, next_pc,
           reg[REG_CPSR], reg[0], reg[1], reg[2], reg[3],
           reg[4], reg[5], reg[6], reg[7], reg[8], reg[9],
           reg[10], reg[11], reg[12], reg[REG_SP], reg[REG_LR]);
    for (unsigned trace_index = 0; trace_index < retained; trace_index++)
    {
      const unsigned sequence = first + trace_index;
      const fault_block_trace_entry *entry =
        &g_fault_block_trace[sequence % FAULT_BLOCK_TRACE_CAPACITY];

      printf("fault_trace seq=%u start=0x%08x end=0x%08x "
             "next=0x%08x sp=0x%08x lr=0x%08x "
             "r0=0x%08x r1=0x%08x r2=0x%08x r3=0x%08x "
             "cpsr=0x%08x\n",
             sequence, entry->start_pc, entry->end_pc, entry->next_pc,
             entry->sp, entry->lr, entry->r0, entry->r1,
             entry->r2, entry->r3, entry->cpsr);
    }
    fflush(stdout);
    _exit(86);
  }

  if (start_pc >= 0x030027f0u && start_pc < 0x03002948u)
  {
    g_irq_dispatch_blocks++;
    g_irq_dispatch_hist[(start_pc - 0x030027f0u) / 4u]++;
    interesting = true;
  }
  if (start_pc >= 0x08000738u && start_pc < 0x080007dcu)
  {
    g_vblank_callback_blocks++;
    g_vblank_callback_hist[(start_pc - 0x08000738u) / 2u]++;
    interesting = true;
    if (post_start && start_pc == 0x080007c2u)
      g_post_start_vblank_tail_blocks++;
  }
  if (start_pc >= 0x08000814u && start_pc < 0x08000868u)
  {
    g_vcount_callback_blocks++;
    interesting = true;
  }
  if (start_pc >= 0x080008acu && start_pc < 0x080008d4u)
  {
    g_vblank_wait_blocks++;
    g_vblank_wait_hist[(start_pc - 0x080008acu) / 2u]++;
    interesting = true;
    if (post_start && start_pc == 0x080008acu)
      g_post_start_wait_entry_blocks++;
  }

  if (interesting && post_start)
  {
    block_trace_entry *entry =
      &g_block_trace[g_block_trace_total % BLOCK_TRACE_CAPACITY];

    entry->start_pc = start_pc;
    entry->end_pc = end_pc;
    entry->thumb = thumb;
    entry->frame = g_current_frame;
    g_block_trace_total++;
  }
}

extern cpu_alert_type __real_write_memory16(u32 address, u16 value);

static bool post_start_observation_active(void)
{
  unsigned frame;
  unsigned duration;

  if (g_third_start_frame != UINT32_MAX)
  {
    frame = g_third_start_frame;
    duration = g_third_start_duration;
  }
  else if (g_second_start_hash != 0u && g_second_start_frame == UINT32_MAX)
    return false;
  else
  {
    frame = g_second_start_frame != UINT32_MAX ?
      g_second_start_frame : g_start_frame;
    duration = g_second_start_frame != UINT32_MAX ?
      g_second_start_duration : g_start_duration;
  }

  return frame != UINT32_MAX && frame <= UINT32_MAX - duration &&
         g_current_frame >= frame + duration;
}

cpu_alert_type __wrap_write_memory16(u32 address, u16 value)
{
  if ((address & ~1u) == 0x0300237cu)
  {
    g_irq_flag_writes++;
    g_last_irq_flag_value = value;
    if (value & 1u)
    {
      g_irq_flag_bit0_set_writes++;
      g_last_bit0_set_frame = g_current_frame;
      g_last_bit0_set_pc = reg[REG_PC];
    }
    if (post_start_observation_active())
    {
      g_post_start_irq_flag_writes++;
      if (value & 1u)
        g_post_start_bit0_set_writes++;
    }
  }

  return __real_write_memory16(address, value);
}

static int finish_process(int status)
{
  fflush(NULL);
  _exit(status);
  return status;
}

static uint32_t fnv1a(const void *data, size_t size)
{
  const uint8_t *bytes = (const uint8_t *)data;
  uint32_t hash = 2166136261u;
  size_t i;

  for (i = 0; i < size; i++)
  {
    hash ^= bytes[i];
    hash *= 16777619u;
  }
  return hash;
}

static const char *lookup_variable(const char *key)
{
  if (strcmp(key, "gpsp_drc") == 0)
    return g_use_dynarec ? "enabled" : "disabled";
  if (strcmp(key, "gpsp_bios") == 0)
    return "builtin";
  if (strcmp(key, "gpsp_boot_mode") == 0)
    return "game";
  if (strcmp(key, "gpsp_frameskip") == 0)
    return "disabled";
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

static bool environment_cb(unsigned command, void *data)
{
  switch (command)
  {
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
      g_pixel_format = *(const enum retro_pixel_format *)data;
      return g_pixel_format == RETRO_PIXEL_FORMAT_RGB565;
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
      *(const char **)data = g_base_dir;
      return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
      *(bool *)data = false;
      return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE:
    {
      struct retro_variable *variable = (struct retro_variable *)data;
      variable->value = lookup_variable(variable->key);
      return variable->value != NULL;
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

static void video_cb(const void *data, unsigned width, unsigned height,
                     size_t pitch)
{
  uint32_t frame_hash;

  if (!data)
    return;

  frame_hash = fnv1a(data, pitch * height);
  if (g_video_frames == 0u || frame_hash != g_frame_hash)
    g_last_frame_hash_change = g_current_frame;
  g_frame_hash = frame_hash;
  g_frame_width = width;
  g_frame_height = height;
  g_frame_pitch = pitch;
  if (width == 240u && height == 160u && pitch >= 480u)
  {
    const uint8_t *source = (const uint8_t *)data;
    unsigned y;

    for (y = 0; y < 160u; y++)
      memcpy(g_latest_frame + y * 240u, source + y * pitch, 480u);
  }
  g_video_frames++;
}

static bool write_ppm(const char *path)
{
  const size_t pixels = 240u * 160u;
  const char header[] = "P6\n240 160\n255\n";
  const size_t header_size = sizeof(header) - 1u;
  const size_t output_size = header_size + pixels * 3u;
  uint8_t *output = (uint8_t *)malloc(output_size);
  size_t pixel;
  size_t written = 0u;
  int fd;

  if (!output)
    return false;
  memcpy(output, header, header_size);
  for (pixel = 0; pixel < pixels; pixel++)
  {
    uint16_t value = g_latest_frame[pixel];
    uint8_t r5 = (uint8_t)((value >> 11) & 0x1fu);
    uint8_t g6 = (uint8_t)((value >> 5) & 0x3fu);
    uint8_t b5 = (uint8_t)(value & 0x1fu);
    size_t dest = header_size + pixel * 3u;

    output[dest] = (uint8_t)((r5 << 3) | (r5 >> 2));
    output[dest + 1u] = (uint8_t)((g6 << 2) | (g6 >> 4));
    output[dest + 2u] = (uint8_t)((b5 << 3) | (b5 >> 2));
  }

  fd = _open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
  if (fd < 0)
  {
    free(output);
    return false;
  }
  while (written < output_size)
  {
    ssize_t count = _write(fd, output + written, output_size - written);

    if (count <= 0)
    {
      _close(fd);
      free(output);
      return false;
    }
    written += (size_t)count;
  }
  _close(fd);
  free(output);
  return true;
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
  uint16_t mask = 0u;
  u32 callback = 0u;

  if (g_current_frame >= g_start_frame &&
      g_current_frame - g_start_frame < g_start_duration)
    mask |= UINT16_C(1) << RETRO_DEVICE_ID_JOYPAD_START;
  if (g_current_frame >= g_second_start_frame &&
      g_current_frame - g_second_start_frame < g_second_start_duration)
    mask |= UINT16_C(1) << RETRO_DEVICE_ID_JOYPAD_START;
  if (g_current_frame >= g_third_start_frame &&
      g_current_frame - g_third_start_frame < g_third_start_duration)
    mask |= UINT16_C(1) << RETRO_DEVICE_ID_JOYPAD_START;

  for (unsigned i = 0; i < g_input_event_count; i++)
  {
    const scheduled_input_event *event = &g_input_events[i];
    if (g_current_frame >= event->frame &&
        g_current_frame - event->frame < event->duration)
    {
      mask |= event->mask;
      g_input_event_active_frames++;
    }
  }
  if (g_auto_a_until_callback != 0u &&
      g_auto_a_reached_frame == UINT32_MAX)
  {
    memcpy(&callback, GPSP_IWRAM_DATA + 0x2364u, sizeof(callback));
    if (callback == g_auto_a_until_callback)
      g_auto_a_reached_frame = g_current_frame;
    else if (g_current_frame >= g_auto_a_start_frame &&
             (g_current_frame - g_auto_a_start_frame) %
               g_auto_a_period < 2u)
      mask |= UINT16_C(1) << RETRO_DEVICE_ID_JOYPAD_A;
  }
  g_current_input_mask = mask;
}

static int16_t input_state_cb(unsigned port, unsigned device,
                              unsigned index, unsigned id)
{
  if (port != 0u || device != RETRO_DEVICE_JOYPAD || index != 0u)
    return 0;
  if (id == RETRO_DEVICE_ID_JOYPAD_MASK)
    return (int16_t)g_current_input_mask;
  if (id >= 16u)
    return 0;
  return (g_current_input_mask & (UINT16_C(1) << id)) != 0u;
}

static bool button_name_to_mask(const char *name, uint16_t *mask)
{
  static const struct
  {
    const char *name;
    uint8_t id;
  } buttons[] = {
      {"up", RETRO_DEVICE_ID_JOYPAD_UP},
      {"down", RETRO_DEVICE_ID_JOYPAD_DOWN},
      {"left", RETRO_DEVICE_ID_JOYPAD_LEFT},
      {"right", RETRO_DEVICE_ID_JOYPAD_RIGHT},
      {"a", RETRO_DEVICE_ID_JOYPAD_A},
      {"b", RETRO_DEVICE_ID_JOYPAD_B},
      {"x", RETRO_DEVICE_ID_JOYPAD_X},
      {"y", RETRO_DEVICE_ID_JOYPAD_Y},
      {"l", RETRO_DEVICE_ID_JOYPAD_L},
      {"r", RETRO_DEVICE_ID_JOYPAD_R},
      {"select", RETRO_DEVICE_ID_JOYPAD_SELECT},
      {"start", RETRO_DEVICE_ID_JOYPAD_START},
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

static bool append_input_event(uint16_t mask, const char *frame_text,
                               const char *duration_text)
{
  char *frame_end = NULL;
  char *duration_end = NULL;
  unsigned long frame;
  unsigned long duration;

  if (mask == 0u || frame_text == NULL || duration_text == NULL ||
      g_input_event_count >= INPUT_EVENT_CAPACITY)
    return false;
  frame = strtoul(frame_text, &frame_end, 0);
  duration = strtoul(duration_text, &duration_end, 0);
  if (frame_end == frame_text || *frame_end != '\0' ||
      duration_end == duration_text || *duration_end != '\0' ||
      frame > UINT32_MAX || duration == 0u || duration > UINT32_MAX)
  {
    return false;
  }

  scheduled_input_event *event = &g_input_events[g_input_event_count++];
  event->mask = mask;
  event->frame = (unsigned)frame;
  event->duration = (unsigned)duration;
  return true;
}

static void *load_file(const char *path, size_t *size_out)
{
  size_t capacity = 1024u * 1024u;
  size_t size = 0u;
  uint8_t *data;
  int fd = _open(path, O_RDONLY, 0);

  if (fd < 0)
    return NULL;
  data = (uint8_t *)malloc(capacity);
  if (!data)
  {
    _close(fd);
    return NULL;
  }

  for (;;)
  {
    ssize_t count;

    if (size == capacity)
    {
      uint8_t *grown;
      size_t next_capacity;

      if (capacity > SIZE_MAX / 2u)
      {
        free(data);
        _close(fd);
        return NULL;
      }
      next_capacity = capacity * 2u;
      grown = (uint8_t *)realloc(data, next_capacity);
      if (!grown)
      {
        free(data);
        _close(fd);
        return NULL;
      }
      data = grown;
      capacity = next_capacity;
    }

    count = _read(fd, data + size, capacity - size);
    if (count < 0)
    {
      free(data);
      _close(fd);
      return NULL;
    }
    if (count == 0)
      break;
    size += (size_t)count;
  }

  _close(fd);
  if (size == 0u)
  {
    free(data);
    return NULL;
  }
  *size_out = size;
  return data;
}

static bool pc_is_bios_reset(uint32_t pc)
{
  return pc >= 0x00001800u && pc < 0x00001900u;
}

static bool pc_is_p_gba_vblank_wait(uint32_t pc)
{
  return pc >= 0x080008c6u && pc <= 0x080008ceu;
}

int main(int argc, char **argv)
{
  const char *rom_path = NULL;
  unsigned frames = 600u;
  unsigned dump_iwram_offset = UINT32_MAX;
  unsigned dump_iwram_length = 0u;
  bool dump_interest_trace = false;
  const char *ppm_path = NULL;
  uint32_t arm_probe_pc = 0u;
  bool force_dispatch = false;
  struct retro_game_info info;
  riscv_runtime_stats stats;
  void *rom_data;
  size_t rom_size;
  unsigned frame;
  unsigned post_start_frames = 0u;
  unsigned wait_sample_frames = 0u;
  unsigned wait_streak = 0u;
  unsigned max_wait_streak = 0u;
  int i;
  bool passed;
  bool basic_passed;
  bool progress_passed;
  const char *reason;
  uint32_t main_callback2 = 0u;
  uint32_t expected_callback = 0x0802f315u;

  for (i = 1; i < argc; i++)
  {
    if (strcmp(argv[i], "--rom") == 0 && i + 1 < argc)
      rom_path = argv[++i];
    else if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc)
      frames = (unsigned)strtoul(argv[++i], NULL, 0);
    else if (strcmp(argv[i], "--force-dispatch") == 0)
      force_dispatch = true;
    else if (strcmp(argv[i], "--disable-mapped-alu") == 0)
      riscv_runtime_perf_disable_mapped_alu_fastpath = 1u;
    else if (strcmp(argv[i], "--disable-fast-reads") == 0)
      riscv_runtime_perf_disable_fast_ram_reads = 1u;
    else if (strcmp(argv[i], "--disable-fast-stores") == 0)
      riscv_runtime_perf_disable_fast_ram_stores = 1u;
    else if (strcmp(argv[i], "--disable-entry-setup") == 0)
      riscv_runtime_perf_disable_entry_setup_opt = 1u;
    else if (strcmp(argv[i], "--disable-state-helpers") == 0)
      riscv_runtime_perf_disable_state_helper_opt = 1u;
    else if (strcmp(argv[i], "--disable-validated-entry") == 0)
      riscv_runtime_perf_disable_validated_entry_opt = 1u;
    else if (strcmp(argv[i], "--disable-indirect-cache") == 0)
      riscv_runtime_perf_disable_indirect_lookup_cache = 1u;
    else if (strcmp(argv[i], "--disable-thumb-native") == 0 &&
             i + 1 < argc)
      g_thumb_native_disable_mask = (u32)strtoul(argv[++i], NULL, 0);
    else if (strcmp(argv[i], "--disable-arm-native") == 0)
      g_arm_native_disable_mask = ~0u;
    else if (strcmp(argv[i], "--disable-arm-native-mask") == 0 &&
             i + 1 < argc)
      g_arm_native_disable_mask = (u32)strtoul(argv[++i], NULL, 0);
    else if (strcmp(argv[i], "--dump-iwram") == 0 && i + 2 < argc)
    {
      dump_iwram_offset = (unsigned)strtoul(argv[++i], NULL, 0);
      dump_iwram_length = (unsigned)strtoul(argv[++i], NULL, 0);
    }
    else if (strcmp(argv[i], "--dump-interest-trace") == 0)
      dump_interest_trace = true;
    else if (strcmp(argv[i], "--ppm") == 0 && i + 1 < argc)
      ppm_path = argv[++i];
    else if (strcmp(argv[i], "--arm-probe-pc") == 0 && i + 1 < argc)
      arm_probe_pc = (uint32_t)strtoul(argv[++i], NULL, 0);
    else if (strcmp(argv[i], "--backend") == 0 && i + 1 < argc)
    {
      const char *backend = argv[++i];

      if (strcmp(backend, "rv32im") == 0)
        g_use_dynarec = true;
      else if (strcmp(backend, "interp") == 0)
        g_use_dynarec = false;
      else
      {
        printf("result=FAIL command=p-gba reason=bad_backend value=%s\n",
               backend);
        return finish_process(2);
      }
    }
    else if (strcmp(argv[i], "--start-frame") == 0 && i + 1 < argc)
      g_start_frame = (unsigned)strtoul(argv[++i], NULL, 0);
    else if (strcmp(argv[i], "--start-duration") == 0 && i + 1 < argc)
      g_start_duration = (unsigned)strtoul(argv[++i], NULL, 0);
    else if (strcmp(argv[i], "--second-start-frame") == 0 && i + 1 < argc)
      g_second_start_frame = (unsigned)strtoul(argv[++i], NULL, 0);
    else if (strcmp(argv[i], "--second-start-duration") == 0 &&
             i + 1 < argc)
      g_second_start_duration = (unsigned)strtoul(argv[++i], NULL, 0);
    else if (strcmp(argv[i], "--second-start-hash") == 0 && i + 1 < argc)
      g_second_start_hash = (uint32_t)strtoul(argv[++i], NULL, 0);
    else if (strcmp(argv[i], "--second-start-stable") == 0 && i + 1 < argc)
      g_second_start_stable_frames = (unsigned)strtoul(argv[++i], NULL, 0);
    else if (strcmp(argv[i], "--third-start-frame") == 0 && i + 1 < argc)
      g_third_start_frame = (unsigned)strtoul(argv[++i], NULL, 0);
    else if (strcmp(argv[i], "--third-start-duration") == 0 && i + 1 < argc)
      g_third_start_duration = (unsigned)strtoul(argv[++i], NULL, 0);
    else if (strcmp(argv[i], "--button-event") == 0 && i + 3 < argc)
    {
      uint16_t mask;
      const char *name = argv[++i];
      const char *event_frame = argv[++i];
      const char *event_duration = argv[++i];

      if (!button_name_to_mask(name, &mask) ||
          !append_input_event(mask, event_frame, event_duration))
      {
        printf("result=FAIL command=p-gba reason=bad_button_event "
               "name=%s frame=%s duration=%s\n",
               name, event_frame, event_duration);
        return finish_process(2);
      }
    }
    else if (strcmp(argv[i], "--auto-a-until-callback") == 0 &&
             i + 2 < argc)
    {
      g_auto_a_until_callback = (u32)strtoul(argv[++i], NULL, 0);
      g_auto_a_start_frame = (unsigned)strtoul(argv[++i], NULL, 0);
    }
    else if (strcmp(argv[i], "--input-event") == 0 && i + 3 < argc)
    {
      char *mask_end = NULL;
      const char *mask_text = argv[++i];
      const char *event_frame = argv[++i];
      const char *event_duration = argv[++i];
      unsigned long mask = strtoul(mask_text, &mask_end, 0);

      if (mask_end == mask_text || *mask_end != '\0' ||
          mask == 0u || mask > UINT16_MAX ||
          !append_input_event((uint16_t)mask, event_frame, event_duration))
      {
        printf("result=FAIL command=p-gba reason=bad_input_event "
               "mask=%s frame=%s duration=%s\n",
               mask_text, event_frame, event_duration);
        return finish_process(2);
      }
    }
    else if (strcmp(argv[i], "--switch-frame") == 0 && i + 1 < argc)
      g_switch_frame = (unsigned)strtoul(argv[++i], NULL, 0);
    else if (strcmp(argv[i], "--switch-backend") == 0 && i + 1 < argc)
    {
      const char *backend = argv[++i];

      if (strcmp(backend, "rv32im") == 0)
        g_switch_to_dynarec = true;
      else if (strcmp(backend, "interp") == 0)
        g_switch_to_dynarec = false;
      else
      {
        printf("result=FAIL command=p-gba reason=bad_switch_backend value=%s\n",
               backend);
        return finish_process(2);
      }
    }
    else
    {
      printf("result=FAIL command=p-gba reason=usage "
             "usage='%s --rom path [--frames n] "
             "[--backend rv32im|interp] [--start-frame n] "
             "[--start-duration n] [--second-start-frame n] "
             "[--second-start-duration n] [--second-start-hash hash] "
             "[--second-start-stable n] [--third-start-frame n] "
             "[--third-start-duration n] "
             "[--button-event name frame duration] "
             "[--input-event mask frame duration] [--switch-frame n] "
             "[--switch-backend rv32im|interp] [--force-dispatch] "
             "[--auto-a-until-callback pc frame] "
             "[--disable-mapped-alu] [--disable-fast-reads] "
             "[--disable-fast-stores] [--disable-entry-setup] "
             "[--disable-state-helpers] [--disable-validated-entry] "
             "[--disable-indirect-cache] [--disable-thumb-native mask] "
             "[--disable-arm-native] [--disable-arm-native-mask mask] "
             "[--dump-iwram offset length] [--dump-interest-trace] "
             "[--ppm path] [--arm-probe-pc pc]'\n",
             argv[0]);
      return finish_process(2);
    }
  }

  if (!rom_path || frames == 0u || g_start_duration == 0u ||
      g_second_start_duration == 0u ||
      g_third_start_duration == 0u ||
      (g_second_start_hash != 0u && g_second_start_stable_frames == 0u))
  {
    printf("result=FAIL command=p-gba reason=missing_argument\n");
    return finish_process(2);
  }

  rom_data = load_file(rom_path, &rom_size);
  if (!rom_data)
  {
    printf("result=FAIL command=p-gba reason=rom_load_failed path=%s\n",
           rom_path);
    return finish_process(2);
  }

  memset(&info, 0, sizeof(info));
  info.path = rom_path;
  info.data = rom_data;
  info.size = rom_size;

  retro_set_environment(environment_cb);
  retro_set_video_refresh(video_cb);
  retro_set_audio_sample(audio_cb);
  retro_set_audio_sample_batch(audio_batch_cb);
  retro_set_input_poll(input_poll_cb);
  retro_set_input_state(input_state_cb);
  retro_init();
  riscv_set_runtime_debug_force_dispatch(force_dispatch);
  riscv_set_runtime_debug_disable_thumb_native(g_thumb_native_disable_mask);
  riscv_set_runtime_debug_disable_arm_native_mask(g_arm_native_disable_mask);
  riscv_set_runtime_debug_arm_probe_pc(arm_probe_pc);

  if (!retro_load_game(&info))
  {
    printf("result=FAIL command=p-gba reason=retro_load_failed bytes=%lu\n",
           (unsigned long)rom_size);
    retro_deinit();
    return finish_process(1);
  }

  for (frame = 0; frame < frames; frame++)
  {
    if (g_second_start_frame == UINT32_MAX && g_second_start_hash != 0u &&
        frame > g_start_frame + g_start_duration)
    {
      if (g_video_frames != 0u && g_frame_hash == g_second_start_hash)
        g_second_start_hash_streak++;
      else
        g_second_start_hash_streak = 0u;
      if (g_second_start_hash_streak >= g_second_start_stable_frames)
      {
        g_second_start_frame = frame;
        g_second_start_auto_triggered = true;
      }
    }
    if (frame == g_switch_frame)
    {
      dynarec_enable = g_switch_to_dynarec ? 1 : 0;
      g_use_dynarec = g_switch_to_dynarec;
      flush_dynarec_caches();
    }
    g_current_frame = frame;
    retro_run();
    if (post_start_observation_active())
    {
      uint16_t sampled_irq_flags;

      post_start_frames++;
      memcpy(&sampled_irq_flags, GPSP_IWRAM_DATA + 0x237cu,
             sizeof(sampled_irq_flags));
      if (sampled_irq_flags & 1u)
      {
        g_post_start_bit0_sample_frames++;
        if (g_post_start_bit0_sample_frames == 1u ||
            !(g_last_sampled_irq_flags & 1u))
          g_post_start_bit0_sample_transitions++;
      }
      g_last_sampled_irq_flags = sampled_irq_flags;
      if (pc_is_p_gba_vblank_wait(reg[REG_PC]))
      {
        wait_sample_frames++;
        wait_streak++;
        if (wait_streak > max_wait_streak)
          max_wait_streak = wait_streak;
      }
      else
      {
        wait_streak = 0u;
      }
    }
  }

  memset(&stats, 0, sizeof(stats));
  riscv_get_runtime_stats(&stats);
  memcpy(&main_callback2, GPSP_IWRAM_DATA + 0x2364u,
         sizeof(main_callback2));
  if (g_auto_a_until_callback != 0u)
    expected_callback = g_auto_a_until_callback;
  if (arm_probe_pc != 0u)
  {
    riscv_runtime_debug_arm_probe probe;

    riscv_get_runtime_debug_arm_probe(&probe);
    printf("arm_probe requested=0x%08x count=%u pc=0x%08x "
           "valid=0x%08x dirty=0x%08x "
           "host_r0=0x%08x host_r1=0x%08x host_r2=0x%08x "
           "host_r3=0x%08x host_r12=0x%08x host_sp=0x%08x "
           "host_lr=0x%08x host_nzcv=0x%08x "
           "state_r0=0x%08x state_r1=0x%08x state_r2=0x%08x "
           "state_r3=0x%08x state_r12=0x%08x state_sp=0x%08x "
           "state_lr=0x%08x state_cpsr=0x%08x state_mode=0x%08x\n",
           arm_probe_pc, probe.count, probe.pc, probe.valid_mask,
           probe.dirty_mask, probe.host_r0, probe.host_r1, probe.host_r2,
           probe.host_r3, probe.host_r12, probe.host_sp, probe.host_lr,
           probe.host_nzcv, probe.state_r0, probe.state_r1, probe.state_r2,
           probe.state_r3, probe.state_r12, probe.state_sp, probe.state_lr,
           probe.state_cpsr, probe.state_mode);
  }
  if (dump_iwram_offset != UINT32_MAX && dump_iwram_length != 0u)
  {
    unsigned offset;
    unsigned end = dump_iwram_offset + dump_iwram_length;

    if (dump_iwram_offset >= 0x8000u || end < dump_iwram_offset ||
        end > 0x8000u)
    {
      printf("result=FAIL command=dump-iwram reason=range "
             "offset=0x%x length=0x%x\n",
             dump_iwram_offset, dump_iwram_length);
    }
    else
    {
      for (offset = dump_iwram_offset; offset < end; offset += 16u)
      {
        unsigned count = end - offset;
        unsigned byte;

        if (count > 16u)
          count = 16u;
        printf("iwram addr=0x%08x data=", 0x03000000u + offset);
        for (byte = 0; byte < count; byte++)
          printf("%02x", GPSP_IWRAM_DATA[offset + byte]);
        printf("\n");
      }
    }
  }
  if (dump_interest_trace)
  {
    unsigned stored = g_block_trace_total;
    unsigned first;
    unsigned trace_index;

    if (stored > BLOCK_TRACE_CAPACITY)
      stored = BLOCK_TRACE_CAPACITY;
    first = g_block_trace_total - stored;
    for (trace_index = 0; trace_index < stored; trace_index++)
    {
      const block_trace_entry *entry =
        &g_block_trace[(first + trace_index) % BLOCK_TRACE_CAPACITY];

      printf("block_trace seq=%u frame=%u start=0x%08x end=0x%08x "
             "thumb=%u\n",
             first + trace_index, entry->frame, entry->start_pc,
             entry->end_pc, entry->thumb);
    }
    for (trace_index = 0;
         trace_index < sizeof(g_irq_dispatch_hist) /
                         sizeof(g_irq_dispatch_hist[0]);
         trace_index++)
    {
      if (g_irq_dispatch_hist[trace_index])
        printf("block_hist kind=irq start=0x%08x count=%u\n",
               0x030027f0u + trace_index * 4u,
               g_irq_dispatch_hist[trace_index]);
    }
    for (trace_index = 0;
         trace_index < sizeof(g_vblank_callback_hist) /
                         sizeof(g_vblank_callback_hist[0]);
         trace_index++)
    {
      if (g_vblank_callback_hist[trace_index])
        printf("block_hist kind=vblank_callback start=0x%08x count=%u\n",
               0x08000738u + trace_index * 2u,
               g_vblank_callback_hist[trace_index]);
    }
    for (trace_index = 0;
         trace_index < sizeof(g_vblank_wait_hist) /
                         sizeof(g_vblank_wait_hist[0]);
         trace_index++)
    {
      if (g_vblank_wait_hist[trace_index])
        printf("block_hist kind=vblank_wait start=0x%08x count=%u\n",
               0x080008acu + trace_index * 2u,
               g_vblank_wait_hist[trace_index]);
    }
  }
  if (ppm_path)
  {
    printf("result=%s command=ppm path=%s width=240 height=160\n",
           write_ppm(ppm_path) ? "PASS" : "FAIL", ppm_path);
  }
  basic_passed = g_video_frames == frames && g_frame_width == 240u &&
                 g_frame_height == 160u && g_frame_pitch == 480u &&
                 !pc_is_bios_reset(reg[REG_PC]);
  if (g_use_dynarec)
    basic_passed = basic_passed && stats.blocks_executed != 0u &&
                   stats.blocks_emitted != 0u &&
                   stats.interpreter_fallbacks == 0u &&
                   stats.bios_native_blocks_emitted != 0u &&
                   stats.bios_native_blocks_executed != 0u &&
                   stats.bios_interpreter_fallbacks == 0u &&
                   stats.bios_native_blocks_executed == g_bios_native_blocks;

  {
    unsigned progress_start = g_third_start_frame != UINT32_MAX ?
      g_third_start_frame : (g_second_start_frame != UINT32_MAX ?
        g_second_start_frame : g_start_frame);

    progress_passed = progress_start < frames && post_start_frames >= 300u &&
                      main_callback2 == expected_callback &&
                      (g_auto_a_until_callback == 0u ||
                       g_auto_a_reached_frame != UINT32_MAX);
  }
  passed = basic_passed && progress_passed;
  if (!basic_passed)
    reason = "full_frontend_diverged";
  else if ((g_third_start_frame != UINT32_MAX ? g_third_start_frame :
            (g_second_start_frame != UINT32_MAX ? g_second_start_frame :
                                                  g_start_frame)) >= frames ||
           post_start_frames < 300u)
    reason = "insufficient_post_start_window";
  else if (main_callback2 != expected_callback ||
           (g_auto_a_until_callback != 0u &&
            g_auto_a_reached_frame == UINT32_MAX))
    reason = "requested_callback_not_reached";
  else
    reason = g_use_dynarec ? "post_start_native_progress" :
                             "post_start_interpreter_progress";

  {
    uint32_t irq_flags = 0u;
    unsigned stable_frames = frames - 1u - g_last_frame_hash_change;
    const uint32_t palette_hash = fnv1a(palette_ram, sizeof(palette_ram));
    const uint32_t vram_hash = fnv1a(vram, sizeof(vram));
    const uint32_t oam_hash = fnv1a(oam_ram, sizeof(oam_ram));
    const uint32_t io_hash = fnv1a(io_registers, sizeof(io_registers));

    memcpy(&irq_flags, GPSP_IWRAM_DATA + 0x237cu, sizeof(irq_flags));

    printf("result=%s command=p-gba backend=%s harness_mode=full_gpSP "
           "frames=%u video_frames=%u pc=0x%08x cpsr=0x%08x "
           "frame_hash=0x%08x palette_hash=0x%08x vram_hash=0x%08x "
           "oam_hash=0x%08x io_hash=0x%08x dispcnt=0x%04x "
           "bgcnt=0x%04x,0x%04x,0x%04x,0x%04x "
           "stable_frames=%u start_frame=%u "
           "start_duration=%u second_start_frame=%u "
           "second_start_duration=%u second_start_hash=0x%08x "
           "second_start_auto=%u second_start_hash_streak=%u "
           "third_start_frame=%u third_start_duration=%u "
           "post_start_frames=%u wait_samples=%u "
           "wait_streak=%u max_wait_streak=%u dispstat=0x%04x "
           "vcount=%u ie=0x%04x "
           "if=0x%04x ime=0x%04x irq_flags=0x%08x force_dispatch=%u "
           "irq_flag_writes=%u bit0_set_writes=%u "
           "post_start_irq_flag_writes=%u post_start_bit0_set_writes=%u "
           "post_start_bit0_sample_frames=%u bit0_sample_transitions=%u "
           "last_bit0_set_frame=%u last_bit0_set_pc=0x%08x "
           "last_irq_flag_value=0x%04x "
           "irq_dispatch_blocks=%u vblank_callback_blocks=%u "
           "vcount_callback_blocks=%u vblank_wait_blocks=%u "
           "post_wait_entries=%u post_vblank_tail_blocks=%u "
           "main_callback2=0x%08x input_events=%u "
           "auto_a_reached_frame=%u "
           "input_event_active_frames=%u "
           "mapped_alu=%u fast_reads=%u fast_stores=%u entry_setup=%u "
           "state_helpers=%u validated_entry=%u indirect_cache=%u "
           "thumb_native_disable=0x%08x arm_native_disable=0x%08x "
           "native_blocks=%u bios_native_blocks=%u bios_hook_blocks=%u "
           "bios_blocks_emitted=%u bios_fallbacks=%u blocks_emitted=%u "
           "generated_code_bytes=%lu "
           "fallbacks=%u reason=%s\n",
           passed ? "PASS" : "FAIL", g_use_dynarec ? "rv32im" : "interp",
           frames, g_video_frames, reg[REG_PC], reg[REG_CPSR], g_frame_hash,
           palette_hash, vram_hash, oam_hash, io_hash,
           io_registers[REG_DISPCNT], io_registers[REG_BG0CNT],
           io_registers[REG_BG1CNT], io_registers[REG_BG2CNT],
           io_registers[REG_BG3CNT],
           stable_frames, g_start_frame, g_start_duration,
           g_second_start_frame, g_second_start_duration,
           g_second_start_hash, g_second_start_auto_triggered ? 1u : 0u,
           g_second_start_hash_streak,
           g_third_start_frame, g_third_start_duration,
           post_start_frames, wait_sample_frames, wait_streak, max_wait_streak,
           io_registers[REG_DISPSTAT], io_registers[REG_VCOUNT],
           io_registers[REG_IE], io_registers[REG_IF], io_registers[REG_IME],
           irq_flags, force_dispatch ? 1u : 0u,
           g_irq_flag_writes, g_irq_flag_bit0_set_writes,
           g_post_start_irq_flag_writes, g_post_start_bit0_set_writes,
           g_post_start_bit0_sample_frames,
           g_post_start_bit0_sample_transitions,
           g_last_bit0_set_frame, g_last_bit0_set_pc,
           g_last_irq_flag_value,
           g_irq_dispatch_blocks, g_vblank_callback_blocks,
           g_vcount_callback_blocks, g_vblank_wait_blocks,
           g_post_start_wait_entry_blocks,
           g_post_start_vblank_tail_blocks,
           main_callback2, g_input_event_count,
           g_auto_a_reached_frame,
           g_input_event_active_frames,
           !riscv_runtime_perf_disable_mapped_alu_fastpath,
           !riscv_runtime_perf_disable_fast_ram_reads,
           !riscv_runtime_perf_disable_fast_ram_stores,
           !riscv_runtime_perf_disable_entry_setup_opt,
           !riscv_runtime_perf_disable_state_helper_opt,
           !riscv_runtime_perf_disable_validated_entry_opt,
           !riscv_runtime_perf_disable_indirect_lookup_cache,
           g_thumb_native_disable_mask, g_arm_native_disable_mask,
           stats.blocks_executed,
           stats.bios_native_blocks_executed,
           g_bios_native_blocks,
           stats.bios_native_blocks_emitted,
           stats.bios_interpreter_fallbacks,
           stats.blocks_emitted,
           (unsigned long)((rom_translation_ptr - rom_translation_cache) +
                           (ram_translation_ptr - ram_translation_cache)),
           stats.interpreter_fallbacks,
           reason);
  }

  retro_unload_game();
  retro_deinit();
  return finish_process(passed ? 0 : 1);
}
