
.globl _open_gba_bios_rom
.globl open_gba_bios_rom

.data
_open_gba_bios_rom:
open_gba_bios_rom:
  .incbin "bios/open_gba_bios.bin"
  .size _open_gba_bios_rom, . - _open_gba_bios_rom
  .size open_gba_bios_rom, . - open_gba_bios_rom

#if defined(__linux__) && defined(__ELF__)
  .section .note.GNU-stack,"",%progbits
#endif
