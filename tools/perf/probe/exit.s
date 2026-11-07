/* The simulator's smallest program: exit(7) by a libgloss system call. */
.section .text.start
.global start
start:
    SP.H = 0x07ff;
    SP.L = 0xfff0;
    R0 = 7;
    [--SP] = R0;
    R0 = SP;
    P0 = 1;
    EXCPT 0;
    NOP;
.Lhang:
    JUMP .Lhang;
