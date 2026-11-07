/* Start-up for tools/perf/sim.cpp in the GNU Blackfin simulator. Turns the
 * cycle counter on, clears .bss, runs main and stops the simulator (HLT, the
 * simulator's own pseudo-instruction). Output goes out a character at a
 * time by OUTC, the simulator's other one: its system calls did not answer.
 * Not part of any kit. */
.section .text.start
.align 2
.global start
.type start, STT_FUNC
start:
    SP.H = 0x07ff;
    SP.L = 0xfff0;
    FP = 0;
    R0 = SYSCFG;
    BITSET (R0, 1);           /* CCEN: count CYCLES */
    SYSCFG = R0;
    CSYNC;
    P0.H = __bss_start;
    P0.L = __bss_start;
    P1.H = __bss_end;
    P1.L = __bss_end;
    R0 = 0;
.Lclear:
    CC = P0 < P1 (IU);
    IF !CC JUMP .Lcleared;
    [P0++] = R0;
    JUMP .Lclear;
.Lcleared:
    OUTC 0x73;                /* "s\n": started */
    OUTC 0x0a;
    SP += -12;
    CALL _main;
    SP += 12;
    HLT;
.Lhang:
    JUMP .Lhang;

/* sim_write (text, length): each character by OUTC. */
.section .text
.align 2
.global sim_write
.type sim_write, STT_FUNC
sim_write:
    P0 = R0;
    CC = R1 == 0;
    IF CC JUMP .Ldone;
.Lnext:
    R2 = B[P0++] (Z);
    OUTC R2;
    R1 += -1;
    CC = R1 == 0;
    IF !CC JUMP .Lnext;
.Ldone:
    RTS;
