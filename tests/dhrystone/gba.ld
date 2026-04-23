ENTRY(_start)

MEMORY
{
  ROM   (rx)  : ORIGIN = 0x08000000, LENGTH = 4M
  IWRAM (rwx) : ORIGIN = 0x03000000, LENGTH = 32K
}

SECTIONS
{
  .text : ALIGN(4)
  {
    KEEP(*(.text.start))
    *(.text*)
    *(.rodata*)
    . = ALIGN(4);
  } > ROM

  __data_load_start = LOADADDR(.data);
  .data : ALIGN(4)
  {
    __data_start__ = .;
    *(.data*)
    . = ALIGN(4);
    __data_end__ = .;
  } > IWRAM AT > ROM

  .bss (NOLOAD) : ALIGN(4)
  {
    __bss_start__ = .;
    *(.bss*)
    *(COMMON)
    . = ALIGN(4);
    __bss_end__ = .;
  } > IWRAM

  /DISCARD/ :
  {
    *(.comment*)
    *(.eh_frame*)
    *(.note*)
  }
}
