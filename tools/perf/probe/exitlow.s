/* The simulator's smallest program: exit(7), the stack at 1 MiB. */
.section .text.start
.global start
start:
    SP.H = 0x0010;
    SP.L = 0x0000;
    R0 = 7;
    [--SP] = R0;
    R0 = SP;
    P0 = 1;
    EXCPT 0;
    NOP;
.Lhang:
    JUMP .Lhang;
