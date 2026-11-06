/* exit(9) after turning the cycle counter on. */
.section .text.start
.global start
start:
    SP.H = 0x07ff;
    SP.L = 0xfff0;
    R0 = SYSCFG;
    BITSET (R0, 1);
    SYSCFG = R0;
    CSYNC;
    R0 = 9;
    [--SP] = R0;
    R0 = SP;
    P0 = 1;
    EXCPT 0;
    NOP;
.Lhang:
    JUMP .Lhang;
