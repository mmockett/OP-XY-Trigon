/* write(1, "probe\n", 6), then exit(0). */
.section .text.start
.global start
start:
    SP.H = 0x07ff;
    SP.L = 0xfff0;
    P1.H = text;
    P1.L = text;
    R0 = 6;
    [--SP] = R0;
    R0 = P1;
    [--SP] = R0;
    R0 = 1;
    [--SP] = R0;
    R0 = SP;
    P0 = 5;
    EXCPT 0;
    NOP;
    R0 = 0;
    [--SP] = R0;
    R0 = SP;
    P0 = 1;
    EXCPT 0;
    NOP;
.Lhang:
    JUMP .Lhang;
.section .rodata
text:
    .ascii "probe\n"
