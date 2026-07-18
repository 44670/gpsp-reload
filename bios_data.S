
.globl _open_gba_bios_rom
.globl open_gba_bios_rom

#if defined(ESP_PLATFORM)
  /* Copied once to bios_rom during game load; keep the immutable source out
   * of the ESP32-S31's shared HP SRAM. */
  .section .rodata.open_gba_bios_rom,"a",%progbits
#else
  .data
#endif
_open_gba_bios_rom:
open_gba_bios_rom:
  .incbin "bios/open_gba_bios.bin"
  .size _open_gba_bios_rom, . - _open_gba_bios_rom
  .size open_gba_bios_rom, . - open_gba_bios_rom

#if defined(__linux__) && defined(__ELF__)
  .section .note.GNU-stack,"",%progbits
#endif
