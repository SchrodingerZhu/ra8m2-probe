/* RA8M2 bench: code in flash, data in SRAM, RTT + stack in DTCM (never cached,
 * so the debugger reads it coherently while D-cache is enabled). */
MEMORY
{
  FLASH (rx)  : ORIGIN = 0x02000000, LENGTH = 1M
  DTCM  (rw)  : ORIGIN = 0x20000000, LENGTH = 128K
  RAM   (rwx) : ORIGIN = 0x22000000, LENGTH = 0x1A0000
}
ENTRY(Reset_Handler)
__stack_top = ORIGIN(DTCM) + LENGTH(DTCM);
SECTIONS
{
  .vectors : { KEEP(*(.vectors)) } > FLASH
  .text : { *(.text*) *(.rodata*) . = ALIGN(4); } > FLASH
  .ARM.exidx : { *(.ARM.exidx* .gnu.linkonce.armexidx.*) } > FLASH
  __data_load = LOADADDR(.data);
  .data : { . = ALIGN(4); __data_start = .; *(.data*) . = ALIGN(4); __data_end = .; } > RAM AT > FLASH
  .bss (NOLOAD) : { . = ALIGN(16); __bss_start = .; *(.bss*) *(COMMON) . = ALIGN(4); __bss_end = .; } > RAM
  /* bench buffers: cacheable SRAM and DTCM variants */
  .sram_buf (NOLOAD) : { . = ALIGN(64); *(.sram_buf) } > RAM
  .dtcm (NOLOAD) : { . = ALIGN(64); *(.rtt) *(.dtcm_buf) } > DTCM
  /DISCARD/ : { *(.note*) *(.comment) }
}
