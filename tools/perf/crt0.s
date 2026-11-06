/* Start-up for tools/perf/sim.cpp in the GNU Blackfin simulator's virtual
 * environment (libgloss system calls: P0 the call, R0 the address of its
 * arguments, then EXCPT 0). Turns the cycle counter on, clears .bss, runs
 * main and exits with its result. Not part of any kit. */
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
    SP += -12;
    CALL _main;
    SP += 12;
    [--SP] = R0;
    R0 = SP;
    P0 = 1;                   /* exit */
    EXCPT 0;
    NOP;
.Lhang:
    JUMP .Lhang;

/* sim_write (text, length): write (1, text, length). */
.section .text
.align 2
.global sim_write
.type sim_write, STT_FUNC
sim_write:
    [--SP] = R1;
    [--SP] = R0;
    R2 = 1;
    [--SP] = R2;
    R0 = SP;
    P0 = 5;                   /* write */
    EXCPT 0;
    NOP;
    SP += 12;
    RTS;
