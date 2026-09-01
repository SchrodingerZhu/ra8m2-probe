/* Renesas RA8M2 (R7KA8M2AF) — Cortex-M85
 * Code flash : 0x02000000, 1 MiB  (Renesas.RA_DFP 6.5.1)
 * SRAM       : 0x22000000, 0x1A0000
 * ITCM       : 0x00000000, 128 KiB   DTCM: 0x20000000, 128 KiB
 */
MEMORY
{
  FLASH (rx)  : ORIGIN = 0x02000000, LENGTH = 1M
  RAM   (rwx) : ORIGIN = 0x22000000, LENGTH = 0x1A0000
}

ENTRY(Reset_Handler)

/* Keep the stack well inside the first 256 KiB of SRAM. */
__stack_top = ORIGIN(RAM) + 0x40000;

SECTIONS
{
  .vectors :
  {
    KEEP(*(.vectors))
  } > FLASH

  .text :
  {
    *(.text*)
    *(.rodata*)
    . = ALIGN(4);
  } > FLASH

  .ARM.exidx :
  {
    *(.ARM.exidx* .gnu.linkonce.armexidx.*)
  } > FLASH

  __data_load = LOADADDR(.data);
  .data :
  {
    . = ALIGN(4);
    __data_start = .;
    *(.data*)
    . = ALIGN(4);
    __data_end = .;
  } > RAM AT > FLASH

  .bss (NOLOAD) :
  {
    . = ALIGN(4);
    __bss_start = .;
    *(.bss*)
    *(COMMON)
    . = ALIGN(4);
    __bss_end = .;
  } > RAM

  /DISCARD/ : { *(.note*) *(.comment) }
}
