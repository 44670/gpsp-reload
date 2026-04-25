#ifndef XTENSA_STATE_H
#define XTENSA_STATE_H

#include <stddef.h>
#include <stdint.h>

enum
{
  OFF_R0 = 0x00,
  OFF_R1 = 0x04,
  OFF_R2 = 0x08,
  OFF_R3 = 0x0C,
  OFF_R4 = 0x10,
  OFF_R5 = 0x14,
  OFF_R6 = 0x18,
  OFF_R7 = 0x1C,
  OFF_R8 = 0x20,
  OFF_R9 = 0x24,
  OFF_R10 = 0x28,
  OFF_R11 = 0x2C,
  OFF_R12 = 0x30,
  OFF_R13 = 0x34,
  OFF_R14 = 0x38,
  OFF_R15 = 0x3C,
  OFF_PC = OFF_R15,
  OFF_CPSR = 0x40,
  OFF_CPU_MODE = 0x44,
  OFF_CPU_HALT_STATE = 0x48,
  OFF_BUS_VALUE = 0x4C,
  OFF_N_FLAG = 0x50,
  OFF_Z_FLAG = 0x54,
  OFF_C_FLAG = 0x58,
  OFF_V_FLAG = 0x5C,
  OFF_SLEEP_CYCLES = 0x60,
  OFF_OAM_UPDATED = 0x64,
  OFF_SAVE0 = 0x68,
  OFF_SAVE1 = 0x6C,
  OFF_SAVE2 = 0x70,
  OFF_SAVE3 = 0x74,
  OFF_SAVE4 = 0x78,
  OFF_SAVE5 = 0x7C,
  OFF_SPSR = 0x80,
  OFF_SPSR_END = 0x98,
  OFF_REG_MODE = 0x98,
  OFF_REG_MODE_END = 0x15C,
  OFF_JIT_CYCLES = 0x15C,
  OFF_JIT_ALERT = 0x160,
  OFF_EXIT_REASON = 0x164,
  OFF_STATE_SIZE = 0x168
};

typedef struct xtensa_jit_state
{
  uint32_t r[32];
  uint32_t spsr[6];
  uint32_t reg_mode[7][7];
  int32_t jit_cycles;
  uint32_t jit_alert;
  uint32_t exit_reason;
} xtensa_jit_state;

#define XTENSA_STATE_ASSERT(name, condition)                                  \
  typedef char xtensa_state_assert_##name[(condition) ? 1 : -1]

XTENSA_STATE_ASSERT(r0, offsetof(xtensa_jit_state, r) == OFF_R0);
XTENSA_STATE_ASSERT(r1, OFF_R1 == OFF_R0 + 1 * sizeof(uint32_t));
XTENSA_STATE_ASSERT(r2, OFF_R2 == OFF_R0 + 2 * sizeof(uint32_t));
XTENSA_STATE_ASSERT(r3, OFF_R3 == OFF_R0 + 3 * sizeof(uint32_t));
XTENSA_STATE_ASSERT(r4, OFF_R4 == OFF_R0 + 4 * sizeof(uint32_t));
XTENSA_STATE_ASSERT(r5, OFF_R5 == OFF_R0 + 5 * sizeof(uint32_t));
XTENSA_STATE_ASSERT(r6, OFF_R6 == OFF_R0 + 6 * sizeof(uint32_t));
XTENSA_STATE_ASSERT(r7, OFF_R7 == OFF_R0 + 7 * sizeof(uint32_t));
XTENSA_STATE_ASSERT(r8, OFF_R8 == OFF_R0 + 8 * sizeof(uint32_t));
XTENSA_STATE_ASSERT(r9, OFF_R9 == OFF_R0 + 9 * sizeof(uint32_t));
XTENSA_STATE_ASSERT(r10, OFF_R10 == OFF_R0 + 10 * sizeof(uint32_t));
XTENSA_STATE_ASSERT(r11, OFF_R11 == OFF_R0 + 11 * sizeof(uint32_t));
XTENSA_STATE_ASSERT(r12, OFF_R12 == OFF_R0 + 12 * sizeof(uint32_t));
XTENSA_STATE_ASSERT(r13, OFF_R13 == OFF_R0 + 13 * sizeof(uint32_t));
XTENSA_STATE_ASSERT(r14, OFF_R14 == OFF_R0 + 14 * sizeof(uint32_t));
XTENSA_STATE_ASSERT(r15, OFF_R15 == OFF_R0 + 15 * sizeof(uint32_t));
XTENSA_STATE_ASSERT(cpsr, OFF_CPSR == OFF_R0 + 16 * sizeof(uint32_t));
XTENSA_STATE_ASSERT(cpu_mode,
                    OFF_CPU_MODE == OFF_R0 + 17 * sizeof(uint32_t));
XTENSA_STATE_ASSERT(cpu_halt_state,
                    OFF_CPU_HALT_STATE == OFF_R0 + 18 * sizeof(uint32_t));
XTENSA_STATE_ASSERT(bus_value,
                    OFF_BUS_VALUE == OFF_R0 + 19 * sizeof(uint32_t));
XTENSA_STATE_ASSERT(n_flag, OFF_N_FLAG == OFF_R0 + 20 * sizeof(uint32_t));
XTENSA_STATE_ASSERT(z_flag, OFF_Z_FLAG == OFF_R0 + 21 * sizeof(uint32_t));
XTENSA_STATE_ASSERT(c_flag, OFF_C_FLAG == OFF_R0 + 22 * sizeof(uint32_t));
XTENSA_STATE_ASSERT(v_flag, OFF_V_FLAG == OFF_R0 + 23 * sizeof(uint32_t));
XTENSA_STATE_ASSERT(sleep_cycles,
                    OFF_SLEEP_CYCLES == OFF_R0 + 24 * sizeof(uint32_t));
XTENSA_STATE_ASSERT(oam_updated,
                    OFF_OAM_UPDATED == OFF_R0 + 25 * sizeof(uint32_t));
XTENSA_STATE_ASSERT(save0, OFF_SAVE0 == OFF_R0 + 26 * sizeof(uint32_t));
XTENSA_STATE_ASSERT(save1, OFF_SAVE1 == OFF_R0 + 27 * sizeof(uint32_t));
XTENSA_STATE_ASSERT(save2, OFF_SAVE2 == OFF_R0 + 28 * sizeof(uint32_t));
XTENSA_STATE_ASSERT(save3, OFF_SAVE3 == OFF_R0 + 29 * sizeof(uint32_t));
XTENSA_STATE_ASSERT(save4, OFF_SAVE4 == OFF_R0 + 30 * sizeof(uint32_t));
XTENSA_STATE_ASSERT(save5, OFF_SAVE5 == OFF_R0 + 31 * sizeof(uint32_t));
XTENSA_STATE_ASSERT(spsr, offsetof(xtensa_jit_state, spsr) == OFF_SPSR);
XTENSA_STATE_ASSERT(spsr_end,
                    OFF_SPSR_END == OFF_SPSR + 6 * sizeof(uint32_t));
XTENSA_STATE_ASSERT(reg_mode,
                    offsetof(xtensa_jit_state, reg_mode) == OFF_REG_MODE);
XTENSA_STATE_ASSERT(reg_mode_end,
                    OFF_REG_MODE_END == OFF_REG_MODE +
                      7 * 7 * sizeof(uint32_t));
XTENSA_STATE_ASSERT(jit_cycles,
                    offsetof(xtensa_jit_state, jit_cycles) ==
                      OFF_JIT_CYCLES);
XTENSA_STATE_ASSERT(jit_alert,
                    offsetof(xtensa_jit_state, jit_alert) == OFF_JIT_ALERT);
XTENSA_STATE_ASSERT(exit_reason,
                    offsetof(xtensa_jit_state, exit_reason) ==
                      OFF_EXIT_REASON);
XTENSA_STATE_ASSERT(state_size, sizeof(xtensa_jit_state) == OFF_STATE_SIZE);

#undef XTENSA_STATE_ASSERT

#endif
