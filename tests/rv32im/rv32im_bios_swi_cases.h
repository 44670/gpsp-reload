#ifndef RV32IM_BIOS_SWI_CASES_H
#define RV32IM_BIOS_SWI_CASES_H

#define RV32IM_BIOS_SWI_PC 0x08010000u
#define RV32IM_BIOS_SWI_SOURCE 0x02000000u
#define RV32IM_BIOS_SWI_HEADER 0x02000200u
#define RV32IM_BIOS_SWI_DEST 0x02001000u
#define RV32IM_BIOS_SWI_VRAM_DEST 0x06000000u

typedef enum
{
  RV32IM_BIOS_FIXTURE_NONE = 0,
  RV32IM_BIOS_FIXTURE_INTR_READY,
  RV32IM_BIOS_FIXTURE_BIT_UNPACK,
  RV32IM_BIOS_FIXTURE_LZ77,
  RV32IM_BIOS_FIXTURE_HUFF,
  RV32IM_BIOS_FIXTURE_RL,
  RV32IM_BIOS_FIXTURE_DIFF8,
  RV32IM_BIOS_FIXTURE_DIFF16,
  RV32IM_BIOS_FIXTURE_MIDI
} rv32im_bios_fixture;

typedef struct
{
  const char *name;
  u32 swi;
  u32 r0;
  u32 r1;
  u32 r2;
  u32 r3;
  rv32im_bios_fixture fixture;
} rv32im_bios_swi_case;

static const rv32im_bios_swi_case rv32im_bios_swi_cases[] =
{
  { "register_ram_reset_zero", 0x01u, 0u, 0u, 0u, 0u,
    RV32IM_BIOS_FIXTURE_NONE },
  { "halt", 0x02u, 0u, 0u, 0u, 0u, RV32IM_BIOS_FIXTURE_NONE },
  { "stop", 0x03u, 0u, 0u, 0u, 0u, RV32IM_BIOS_FIXTURE_NONE },
  { "intr_wait_ready", 0x04u, 0u, 1u, 0u, 0u,
    RV32IM_BIOS_FIXTURE_INTR_READY },
  { "sqrt", 0x08u, 0x00fedcbau, 0u, 0u, 0u,
    RV32IM_BIOS_FIXTURE_NONE },
  { "arctan", 0x09u, 0x00002000u, 0u, 0u, 0u,
    RV32IM_BIOS_FIXTURE_NONE },
  { "arctan2", 0x0au, 0x00003000u, 0xffffe000u, 0u, 0u,
    RV32IM_BIOS_FIXTURE_NONE },
  { "get_bios_checksum", 0x0du, 0u, 0u, 0u, 0u,
    RV32IM_BIOS_FIXTURE_NONE },
  { "bit_unpack", 0x10u, RV32IM_BIOS_SWI_SOURCE,
    RV32IM_BIOS_SWI_DEST, RV32IM_BIOS_SWI_HEADER, 0u,
    RV32IM_BIOS_FIXTURE_BIT_UNPACK },
  { "lz77_wram", 0x11u, RV32IM_BIOS_SWI_SOURCE,
    RV32IM_BIOS_SWI_DEST, 0u, 0u, RV32IM_BIOS_FIXTURE_LZ77 },
  { "lz77_vram", 0x12u, RV32IM_BIOS_SWI_SOURCE,
    RV32IM_BIOS_SWI_VRAM_DEST, 0u, 0u, RV32IM_BIOS_FIXTURE_LZ77 },
  { "huff", 0x13u, RV32IM_BIOS_SWI_SOURCE,
    RV32IM_BIOS_SWI_DEST, 0u, 0u, RV32IM_BIOS_FIXTURE_HUFF },
  { "rl_wram", 0x14u, RV32IM_BIOS_SWI_SOURCE,
    RV32IM_BIOS_SWI_DEST, 0u, 0u, RV32IM_BIOS_FIXTURE_RL },
  { "rl_vram", 0x15u, RV32IM_BIOS_SWI_SOURCE,
    RV32IM_BIOS_SWI_VRAM_DEST, 0u, 0u, RV32IM_BIOS_FIXTURE_RL },
  { "diff8_wram", 0x16u, RV32IM_BIOS_SWI_SOURCE,
    RV32IM_BIOS_SWI_DEST, 0u, 0u, RV32IM_BIOS_FIXTURE_DIFF8 },
  { "diff8_vram", 0x17u, RV32IM_BIOS_SWI_SOURCE,
    RV32IM_BIOS_SWI_VRAM_DEST, 0u, 0u, RV32IM_BIOS_FIXTURE_DIFF8 },
  { "diff16", 0x18u, RV32IM_BIOS_SWI_SOURCE,
    RV32IM_BIOS_SWI_DEST, 0u, 0u, RV32IM_BIOS_FIXTURE_DIFF16 },
  { "invalid_sound_bias", 0x19u, 0x12345678u, 0x9abcdef0u, 0u, 0u,
    RV32IM_BIOS_FIXTURE_NONE },
  { "midi_key_to_freq", 0x1fu, RV32IM_BIOS_SWI_SOURCE,
    60u, 0x80u, 0u, RV32IM_BIOS_FIXTURE_MIDI },
  { "music_player_continue", 0x23u, 0x02000000u, 0u, 0u, 0u,
    RV32IM_BIOS_FIXTURE_NONE },
  { "music_player_fade_out", 0x24u, 0x02000000u, 7u, 0u, 0u,
    RV32IM_BIOS_FIXTURE_NONE },
  { "custom_halt", 0x27u, 0x55u, 0u, 0u, 0u,
    RV32IM_BIOS_FIXTURE_NONE },
  { "sound_get_jump_list", 0x2au, RV32IM_BIOS_SWI_DEST,
    0u, 0u, 0u, RV32IM_BIOS_FIXTURE_NONE },
};

#define RV32IM_BIOS_SWI_CASE_COUNT \
  (sizeof(rv32im_bios_swi_cases) / sizeof(rv32im_bios_swi_cases[0]))

#endif
