/* tools/perf/sim.cpp in the GNU Blackfin simulator's 128 MiB of memory at 0.
 * Not part of any kit. */
ENTRY(start)
SECTIONS
{
    . = 0x1000;
    .text : { *(.text.start) *(.text .text.*) }
    . = ALIGN(4);
    .rodata : { *(.rodata .rodata.*) }
    . = ALIGN(4);
    .data : { *(.data .data.*) }
    . = ALIGN(4);
    .bss : { __bss_start = .; *(.bss .bss.* COMMON) . = ALIGN(4); __bss_end = .; }
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
