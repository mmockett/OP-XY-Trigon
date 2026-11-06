/* "ok\n" by OUTC, then HLT: the simulator's own pseudo-instructions. */
.section .text.start
.global start
start:
    OUTC 0x6f;
    OUTC 0x6b;
    OUTC 0x0a;
    HLT;
