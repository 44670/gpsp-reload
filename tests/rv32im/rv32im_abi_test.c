typedef unsigned int u32;
typedef unsigned char u8;
typedef unsigned int usize;

#define SYS_WRITE 64
#define SYS_EXIT 93
#define STDOUT_FD 1

struct abi_state
{
  u32 magic;
  u32 cycles_seen;
  u32 calls;
  u32 checksum;
};

extern u32 rv32im_abi_probe(u32 cycles, struct abi_state *state,
                            u32 (*block)(struct abi_state *, u32),
                            u32 expected_return);

static struct abi_state g_state;

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

static void sys_exit(int code)
{
  syscall1(SYS_EXIT, code);
  for (;;)
    ;
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
  syscall3(SYS_WRITE, STDOUT_FD, (long)text, (long)cstr_len(text));
}

static void put_chr(char ch)
{
  syscall3(SYS_WRITE, STDOUT_FD, (long)&ch, 1);
}

static char hex_digit(u32 value)
{
  value &= 0xf;
  return (char)(value < 10 ? ('0' + value) : ('a' + value - 10));
}

static void put_u32_hex(u32 value)
{
  int shift;
  put_raw("0x");
  for (shift = 28; shift >= 0; shift -= 4)
    put_chr(hex_digit(value >> (u32)shift));
}

static void put_u32_dec(u32 value)
{
  char buf[10];
  usize pos = 0;

  if (value == 0)
  {
    put_chr('0');
    return;
  }

  while (value)
  {
    buf[pos++] = (char)('0' + (value % 10));
    value /= 10;
  }

  while (pos)
    put_chr(buf[--pos]);
}

__attribute__((noinline))
u32 rv32im_abi_block(struct abi_state *state, u32 cycles)
{
  state->cycles_seen = cycles;
  state->calls++;
  state->checksum = state->magic ^ cycles ^ 0x13572468u;
  return (cycles - 17u) | 0x40000000u;
}

void _start(void)
{
  const u32 cycles = 4096u;
  const u32 expected_return = (cycles - 17u) | 0x40000000u;
  u32 fail_mask;
  u32 state_fail = 0;

  g_state.magic = 0xace01234u;
  g_state.cycles_seen = 0;
  g_state.calls = 0;
  g_state.checksum = 0;

  fail_mask = rv32im_abi_probe(cycles, &g_state, rv32im_abi_block,
                               expected_return);

  if (g_state.cycles_seen != cycles)
    state_fail |= 0x1;
  if (g_state.calls != 1)
    state_fail |= 0x2;
  if (g_state.checksum != (g_state.magic ^ cycles ^ 0x13572468u))
    state_fail |= 0x4;

  if (fail_mask || state_fail)
  {
    put_raw("result=FAIL command=abi_stub fail_mask=");
    put_u32_hex(fail_mask);
    put_raw(" state_fail=");
    put_u32_hex(state_fail);
    put_raw(" reason=abi_mismatch\n");
    sys_exit(1);
  }

  put_raw("result=PASS command=abi_stub cycles=");
  put_u32_dec(cycles);
  put_raw(" return=");
  put_u32_hex(expected_return);
  put_raw(" fail_mask=");
  put_u32_hex(fail_mask);
  put_raw(" state_fail=");
  put_u32_hex(state_fail);
  put_raw(" reason=callee_saved_and_state_args_ok\n");
  sys_exit(0);
}
