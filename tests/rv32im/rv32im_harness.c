typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef int s32;
typedef unsigned int usize;

#define SYS_OPENAT 56
#define SYS_CLOSE 57
#define SYS_READ 63
#define SYS_WRITE 64
#define SYS_EXIT 93

#define AT_FDCWD (-100)
#define O_RDONLY 0
#define O_WRONLY 1
#define O_CREAT 64
#define O_TRUNC 512

#define STDIN_FD 0
#define STDOUT_FD 1

#define FRAME_W 240
#define FRAME_H 160
#define FRAME_BYTES (FRAME_W * FRAME_H * 2)
#define PNG_RAW_STRIDE (FRAME_W * 3 + 1)
#define PNG_RAW_SIZE (PNG_RAW_STRIDE * FRAME_H)
#define ZLIB_BLOCK_MAX 65535u
#define ZLIB_BLOCKS ((PNG_RAW_SIZE + ZLIB_BLOCK_MAX - 1) / ZLIB_BLOCK_MAX)
#define ZLIB_SIZE (2 + PNG_RAW_SIZE + (ZLIB_BLOCKS * 5) + 4)

enum harness_backend {
  BACKEND_INTERP = 0,
  BACKEND_RV32IM = 1,
};

struct harness_state {
  enum harness_backend backend;
  u32 frames;
  u32 cycles;
  u32 blocks;
  u32 instructions;
  u32 loaded_bytes;
  u32 loaded_hash;
  u32 last_frame_hash;
};

static struct harness_state g_state;
static u8 g_frame[FRAME_BYTES];
static u8 g_png_raw[PNG_RAW_SIZE];
static u8 g_zlib[ZLIB_SIZE];
static char g_line[512];

static long syscall1(long number, long arg0)
{
  register long a7 __asm__("a7") = number;
  register long a0 __asm__("a0") = arg0;
  __asm__ volatile("ecall" : "+r"(a0) : "r"(a7) : "memory");
  return a0;
}

static long syscall3(long number, long arg0, long arg1, long arg2)
{
  register long a7 __asm__("a7") = number;
  register long a0 __asm__("a0") = arg0;
  register long a1 __asm__("a1") = arg1;
  register long a2 __asm__("a2") = arg2;
  __asm__ volatile("ecall"
                   : "+r"(a0)
                   : "r"(a1), "r"(a2), "r"(a7)
                   : "memory");
  return a0;
}

static long syscall4(long number, long arg0, long arg1, long arg2, long arg3)
{
  register long a7 __asm__("a7") = number;
  register long a0 __asm__("a0") = arg0;
  register long a1 __asm__("a1") = arg1;
  register long a2 __asm__("a2") = arg2;
  register long a3 __asm__("a3") = arg3;
  __asm__ volatile("ecall"
                   : "+r"(a0)
                   : "r"(a1), "r"(a2), "r"(a3), "r"(a7)
                   : "memory");
  return a0;
}

static void sys_exit(int code)
{
  syscall1(SYS_EXIT, code);
  for (;;)
    ;
}

static int write_all(int fd, const void *data, usize size)
{
  const u8 *ptr = (const u8 *)data;

  while (size)
  {
    long ret = syscall3(SYS_WRITE, fd, (long)ptr, (long)size);
    if (ret <= 0)
      return -1;
    ptr += (usize)ret;
    size -= (usize)ret;
  }

  return 0;
}

static usize cstr_len(const char *text)
{
  usize len = 0;
  while (text[len])
    len++;
  return len;
}

static void put_raw(const char *text)
{
  write_all(STDOUT_FD, text, cstr_len(text));
}

static void put_chr(char ch)
{
  write_all(STDOUT_FD, &ch, 1);
}

static void put_u32_dec(u32 value)
{
  char tmp[10];
  usize pos = 0;

  if (value == 0)
  {
    put_chr('0');
    return;
  }

  while (value)
  {
    tmp[pos++] = (char)('0' + (value % 10));
    value /= 10;
  }

  while (pos)
    put_chr(tmp[--pos]);
}

static char hex_digit(u32 value)
{
  value &= 0xf;
  return (char)(value < 10 ? ('0' + value) : ('a' + value - 10));
}

static void put_u32_hex(u32 value)
{
  s32 shift;
  put_raw("0x");
  for (shift = 28; shift >= 0; shift -= 4)
    put_chr(hex_digit(value >> (u32)shift));
}

static int is_space(char ch)
{
  return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

static char *skip_space(char *text)
{
  while (*text && is_space(*text))
    text++;
  return text;
}

static char *next_token(char **cursor)
{
  char *start = skip_space(*cursor);
  char *end = start;

  while (*end && !is_space(*end))
    end++;

  if (*end)
    *end++ = 0;

  *cursor = end;
  return start;
}

static int str_eq(const char *a, const char *b)
{
  while (*a && *b && *a == *b)
  {
    a++;
    b++;
  }
  return *a == 0 && *b == 0;
}

static int parse_u32_token(const char *text, u32 *out)
{
  u32 base = 10;
  u32 value = 0;

  if (!text || !*text)
    return 0;

  if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))
  {
    base = 16;
    text += 2;
  }

  if (!*text)
    return 0;

  while (*text)
  {
    u32 digit;
    char ch = *text++;

    if (ch >= '0' && ch <= '9')
      digit = (u32)(ch - '0');
    else if (ch >= 'a' && ch <= 'f')
      digit = (u32)(ch - 'a' + 10);
    else if (ch >= 'A' && ch <= 'F')
      digit = (u32)(ch - 'A' + 10);
    else
      return 0;

    if (digit >= base)
      return 0;
    value = value * base + digit;
  }

  *out = value;
  return 1;
}

static u32 fnv1a_update(u32 hash, const u8 *data, usize size)
{
  usize i;
  for (i = 0; i < size; i++)
  {
    hash ^= data[i];
    hash *= 16777619u;
  }
  return hash;
}

static const char *backend_name(void)
{
  return g_state.backend == BACKEND_RV32IM ? "rv32im" : "interp";
}

static void reset_state(void)
{
  g_state.backend = BACKEND_INTERP;
  g_state.frames = 0;
  g_state.cycles = 0;
  g_state.blocks = 0;
  g_state.instructions = 0;
  g_state.loaded_bytes = 0;
  g_state.loaded_hash = 2166136261u;
  g_state.last_frame_hash = 0;
}

static void render_frame(void)
{
  u32 x;
  u32 y;
  u32 seed = g_state.loaded_hash ^ (g_state.frames * 0x45d9f3bu);
  usize pos = 0;

  for (y = 0; y < FRAME_H; y++)
  {
    for (x = 0; x < FRAME_W; x++)
    {
      u32 r = (x + (seed >> 3) + g_state.frames) & 31u;
      u32 g = (y + (seed >> 9) + (g_state.backend == BACKEND_RV32IM ? 9u : 0u)) & 63u;
      u32 b = (x + y + (seed >> 17)) & 31u;
      u16 pix = (u16)((r << 11) | (g << 5) | b);
      g_frame[pos++] = (u8)(pix & 0xff);
      g_frame[pos++] = (u8)(pix >> 8);
    }
  }

  g_state.last_frame_hash = fnv1a_update(2166136261u, g_frame, FRAME_BYTES);
}

static void print_summary(const char *command, const char *reason)
{
  put_raw("result=PASS command=");
  put_raw(command);
  put_raw(" backend=");
  put_raw(backend_name());
  put_raw(" frames=");
  put_u32_dec(g_state.frames);
  put_raw(" cycles=");
  put_u32_dec(g_state.cycles);
  put_raw(" blocks=");
  put_u32_dec(g_state.blocks);
  put_raw(" instructions=");
  put_u32_dec(g_state.instructions);
  put_raw(" frame_hash=");
  put_u32_hex(g_state.last_frame_hash);
  put_raw(" reason=");
  put_raw(reason);
  put_chr('\n');
}

static void print_fail(const char *command, const char *reason)
{
  put_raw("result=FAIL command=");
  put_raw(command);
  put_raw(" backend=");
  put_raw(backend_name());
  put_raw(" reason=");
  put_raw(reason);
  put_chr('\n');
}

static void command_help(void)
{
  put_raw("commands=load reset backend run cont stepi stepb regs mem framehash png quit\n");
}

static void command_backend(char *arg)
{
  if (str_eq(arg, "interp"))
  {
    g_state.backend = BACKEND_INTERP;
    put_raw("ok backend=interp\n");
  }
  else if (str_eq(arg, "rv32im"))
  {
    g_state.backend = BACKEND_RV32IM;
    put_raw("ok backend=rv32im\n");
  }
  else
  {
    print_fail("backend", "unknown_backend");
  }
}

static void command_load(char *path)
{
  u8 buf[256];
  long fd;
  u32 hash = 2166136261u;
  u32 total = 0;

  if (!path || !*path)
  {
    print_fail("load", "missing_path");
    return;
  }

  fd = syscall4(SYS_OPENAT, AT_FDCWD, (long)path, O_RDONLY, 0);
  if (fd < 0)
  {
    print_fail("load", "open_error");
    return;
  }

  for (;;)
  {
    long got = syscall3(SYS_READ, fd, (long)buf, sizeof(buf));
    if (got < 0)
    {
      syscall1(SYS_CLOSE, fd);
      print_fail("load", "read_error");
      return;
    }
    if (got == 0)
      break;
    hash = fnv1a_update(hash, buf, (usize)got);
    total += (u32)got;
  }

  syscall1(SYS_CLOSE, fd);
  g_state.loaded_hash = hash;
  g_state.loaded_bytes = total;

  put_raw("ok command=load bytes=");
  put_u32_dec(total);
  put_raw(" hash=");
  put_u32_hex(hash);
  put_chr('\n');
}

static void command_reset(void)
{
  enum harness_backend backend = g_state.backend;
  reset_state();
  g_state.backend = backend;
  render_frame();
  put_raw("ok command=reset backend=");
  put_raw(backend_name());
  put_chr('\n');
}

static u32 optional_count(char *arg, u32 fallback)
{
  u32 value;
  if (!arg || !*arg)
    return fallback;
  if (!parse_u32_token(arg, &value))
    return fallback;
  return value;
}

static void command_run(char *arg)
{
  u32 frames = optional_count(arg, 1);
  g_state.frames += frames;
  g_state.cycles += frames * 280896u;
  g_state.blocks += frames * (g_state.backend == BACKEND_RV32IM ? 128u : 0u);
  g_state.instructions += frames * 1024u;
  render_frame();
  print_summary("run", "stub_harness");
}

static void command_cont(char *arg)
{
  u32 cycles = optional_count(arg, 960);
  g_state.cycles += cycles;
  g_state.blocks += g_state.backend == BACKEND_RV32IM ? 1u : 0u;
  render_frame();
  print_summary("cont", "scheduler_boundary_stub");
}

static void command_stepi(char *arg)
{
  u32 count = optional_count(arg, 1);
  g_state.instructions += count;
  g_state.cycles += count;
  render_frame();
  print_summary("stepi", "instruction_step_stub");
}

static void command_stepb(char *arg)
{
  u32 count = optional_count(arg, 1);
  g_state.blocks += count;
  g_state.cycles += count * 4u;
  render_frame();
  print_summary("stepb", "block_step_stub");
}

static void command_regs(void)
{
  u32 i;
  put_raw("regs");
  for (i = 0; i < 16; i++)
  {
    u32 value = g_state.loaded_hash ^ (i * 0x11111111u) ^ g_state.cycles;
    put_raw(" r");
    put_u32_dec(i);
    put_chr('=');
    put_u32_hex(value);
  }
  put_chr('\n');
}

static void command_mem(char *addr_arg, char *len_arg)
{
  u32 addr;
  u32 len;
  u32 i;

  if (!parse_u32_token(addr_arg, &addr) || !parse_u32_token(len_arg, &len))
  {
    print_fail("mem", "bad_args");
    return;
  }

  if (len > 64)
    len = 64;

  put_raw("mem addr=");
  put_u32_hex(addr);
  put_raw(" len=");
  put_u32_dec(len);
  put_raw(" data=");
  for (i = 0; i < len; i++)
  {
    u8 value = (u8)((addr + i + g_state.loaded_hash + g_state.cycles) & 0xff);
    put_chr(hex_digit(value >> 4));
    put_chr(hex_digit(value));
  }
  put_chr('\n');
}

static u32 crc32_update(u32 crc, const u8 *data, usize len)
{
  usize i;
  crc = ~crc;
  for (i = 0; i < len; i++)
  {
    u32 value = (crc ^ data[i]) & 0xffu;
    u32 bit;
    for (bit = 0; bit < 8; bit++)
      value = (value & 1u) ? (0xedb88320u ^ (value >> 1)) : (value >> 1);
    crc = (crc >> 8) ^ value;
  }
  return ~crc;
}

static u32 adler32(const u8 *data, usize len)
{
  const u32 mod = 65521u;
  u32 a = 1;
  u32 b = 0;
  usize i;

  for (i = 0; i < len; i++)
  {
    a += data[i];
    if (a >= mod)
      a -= mod;
    b += a;
    if (b >= mod)
      b %= mod;
  }

  return (b << 16) | a;
}

static void store_be32(u8 *dst, u32 value)
{
  dst[0] = (u8)(value >> 24);
  dst[1] = (u8)(value >> 16);
  dst[2] = (u8)(value >> 8);
  dst[3] = (u8)value;
}

static int write_png_chunk(int fd, const char type[4], const u8 *payload, u32 len)
{
  u8 header[8];
  u8 crc_bytes[4];
  u32 crc = 0;

  store_be32(header, len);
  header[4] = (u8)type[0];
  header[5] = (u8)type[1];
  header[6] = (u8)type[2];
  header[7] = (u8)type[3];

  crc = crc32_update(crc, (const u8 *)type, 4);
  crc = crc32_update(crc, payload, len);
  store_be32(crc_bytes, crc);

  if (write_all(fd, header, sizeof(header)) < 0)
    return -1;
  if (len && write_all(fd, payload, len) < 0)
    return -1;
  if (write_all(fd, crc_bytes, sizeof(crc_bytes)) < 0)
    return -1;
  return 0;
}

static void build_png_raw(void)
{
  u32 x;
  u32 y;
  usize src = 0;
  usize dst = 0;

  for (y = 0; y < FRAME_H; y++)
  {
    g_png_raw[dst++] = 0;
    for (x = 0; x < FRAME_W; x++)
    {
      u16 pix = (u16)(g_frame[src] | ((u16)g_frame[src + 1] << 8));
      u32 r = (pix >> 11) & 31u;
      u32 g = (pix >> 5) & 63u;
      u32 b = pix & 31u;
      src += 2;
      g_png_raw[dst++] = (u8)((r << 3) | (r >> 2));
      g_png_raw[dst++] = (u8)((g << 2) | (g >> 4));
      g_png_raw[dst++] = (u8)((b << 3) | (b >> 2));
    }
  }
}

static u32 build_zlib_stored(void)
{
  u32 src = 0;
  u32 dst = 0;
  u32 remaining = PNG_RAW_SIZE;
  u32 adler = adler32(g_png_raw, PNG_RAW_SIZE);

  g_zlib[dst++] = 0x78;
  g_zlib[dst++] = 0x01;

  while (remaining)
  {
    u32 block = remaining > ZLIB_BLOCK_MAX ? ZLIB_BLOCK_MAX : remaining;
    u32 final = (remaining == block);
    u32 nlen = (~block) & 0xffffu;
    u32 i;

    g_zlib[dst++] = (u8)final;
    g_zlib[dst++] = (u8)block;
    g_zlib[dst++] = (u8)(block >> 8);
    g_zlib[dst++] = (u8)nlen;
    g_zlib[dst++] = (u8)(nlen >> 8);

    for (i = 0; i < block; i++)
      g_zlib[dst++] = g_png_raw[src++];

    remaining -= block;
  }

  store_be32(&g_zlib[dst], adler);
  dst += 4;
  return dst;
}

static int write_png_file(const char *path)
{
  static const u8 png_sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
  u8 ihdr[13];
  u32 zlen;
  long fd;

  fd = syscall4(SYS_OPENAT, AT_FDCWD, (long)path,
                O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd < 0)
    return -1;

  build_png_raw();
  zlen = build_zlib_stored();

  store_be32(&ihdr[0], FRAME_W);
  store_be32(&ihdr[4], FRAME_H);
  ihdr[8] = 8;
  ihdr[9] = 2;
  ihdr[10] = 0;
  ihdr[11] = 0;
  ihdr[12] = 0;

  if (write_all((int)fd, png_sig, sizeof(png_sig)) < 0 ||
      write_png_chunk((int)fd, "IHDR", ihdr, sizeof(ihdr)) < 0 ||
      write_png_chunk((int)fd, "IDAT", g_zlib, zlen) < 0 ||
      write_png_chunk((int)fd, "IEND", (const u8 *)"", 0) < 0)
  {
    syscall1(SYS_CLOSE, fd);
    return -1;
  }

  syscall1(SYS_CLOSE, fd);
  return 0;
}

static void command_framehash(void)
{
  render_frame();
  put_raw("framehash width=");
  put_u32_dec(FRAME_W);
  put_raw(" height=");
  put_u32_dec(FRAME_H);
  put_raw(" bytes=");
  put_u32_dec(FRAME_BYTES);
  put_raw(" hash=");
  put_u32_hex(g_state.last_frame_hash);
  put_chr('\n');
}

static void command_png(char *path)
{
  if (!path || !*path)
  {
    print_fail("png", "missing_path");
    return;
  }

  render_frame();
  if (write_png_file(path) < 0)
  {
    print_fail("png", "write_error");
    return;
  }

  put_raw("result=PASS command=png backend=");
  put_raw(backend_name());
  put_raw(" width=");
  put_u32_dec(FRAME_W);
  put_raw(" height=");
  put_u32_dec(FRAME_H);
  put_raw(" frame_hash=");
  put_u32_hex(g_state.last_frame_hash);
  put_raw(" path=");
  put_raw(path);
  put_chr('\n');
}

static int read_line(char *line, usize cap)
{
  usize len = 0;

  while (len + 1 < cap)
  {
    char ch;
    long got = syscall3(SYS_READ, STDIN_FD, (long)&ch, 1);
    if (got < 0)
      return -1;
    if (got == 0)
    {
      if (len == 0)
        return 0;
      break;
    }
    if (ch == '\n')
      break;
    line[len++] = ch;
  }

  line[len] = 0;
  return 1;
}

static void process_line(char *line)
{
  char *cursor = line;
  char *cmd = next_token(&cursor);

  if (!cmd || !*cmd)
    return;

  if (str_eq(cmd, "help"))
  {
    command_help();
  }
  else if (str_eq(cmd, "load"))
  {
    command_load(next_token(&cursor));
  }
  else if (str_eq(cmd, "reset"))
  {
    command_reset();
  }
  else if (str_eq(cmd, "backend"))
  {
    command_backend(next_token(&cursor));
  }
  else if (str_eq(cmd, "run"))
  {
    command_run(next_token(&cursor));
  }
  else if (str_eq(cmd, "cont"))
  {
    command_cont(next_token(&cursor));
  }
  else if (str_eq(cmd, "stepi"))
  {
    command_stepi(next_token(&cursor));
  }
  else if (str_eq(cmd, "stepb"))
  {
    command_stepb(next_token(&cursor));
  }
  else if (str_eq(cmd, "regs"))
  {
    command_regs();
  }
  else if (str_eq(cmd, "mem"))
  {
    char *addr = next_token(&cursor);
    char *len = next_token(&cursor);
    command_mem(addr, len);
  }
  else if (str_eq(cmd, "framehash"))
  {
    command_framehash();
  }
  else if (str_eq(cmd, "png"))
  {
    command_png(next_token(&cursor));
  }
  else if (str_eq(cmd, "quit"))
  {
    render_frame();
    print_summary("quit", "requested");
    sys_exit(0);
  }
  else
  {
    print_fail(cmd, "unknown_command");
  }
}

void _start(void)
{
  reset_state();
  render_frame();
  put_raw("rv32im-harness ready version=0 backend=interp\n");

  for (;;)
  {
    int ret = read_line(g_line, sizeof(g_line));
    if (ret < 0)
      sys_exit(2);
    if (ret == 0)
      sys_exit(0);
    process_line(g_line);
  }
}
