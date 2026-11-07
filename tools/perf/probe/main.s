/* A main that returns at once, for crt0.s alone, and a bss to clear. */
.section .text
.global _main
_main:
    OUTC 0x6d;
    OUTC 0x0a;
    R0 = 0;
    RTS;
.section .bss
.align 4
pad:
    .space 4096
