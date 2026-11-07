#!/usr/bin/env python3
"""Teaches the GNU Blackfin simulator (GDB's sim/bfin/bfin-sim.c, a classic
Blackfin) the one Blackfin+ (BF70x) instruction the kits compile to that it
lacks: Rd = Rs * Rt with (T) or no option, a 32 x 32 fractional multiply,
saturating, truncating with (T) and rounding without (the SDK's
q31::multiply). Encoded as dsp32mult with op1 set, MM, w0, no P, no w1.
Counted as one instruction, as the others. Not part of any kit.
  python3 -I tools/perf/bfplus.py <gdb>/sim/bfin/bfin-sim.c"""
import sys

path = sys.argv[1]
s = open(path).read()
anchor = """  if (w1 == 0 && w0 == 0)
    illegal_instruction (cpu);
  if (((1 << mmod) & (P ? 0x313 : 0x1b57)) == 0)
    illegal_instruction (cpu);"""
assert s.count(anchor) == 1, "bfin-sim.c is not the one this patch knows"
patch = """  if (op1 != 0 && w0 && MM && !P && !w1 && (mmod == 0 || mmod == 1))
    {
      /* Blackfin+: Rd = Rs * Rt [(T)], 32 x 32 fractional, saturating.  */
      bs64 p = (bs64)(bs32)DREG (src0) * (bs64)(bs32)DREG (src1);
      bs64 r = mmod == 1 ? (p >> 31) : ((p + ((bs64)1 << 30)) >> 31);
      if (r > 0x7fffffff)
	r = 0x7fffffff;
      if (r < -(bs64)0x80000000)
	r = -(bs64)0x80000000;
      TRACE_INSN (cpu, "R%i = R%i * R%i%s;", dst, src0, src1, mmod == 1 ? " (T)" : "");
      STORE (DREG (dst), (bu32)r);
      return;
    }
"""
s = s.replace(anchor, patch + anchor)
open(path, "w").write(s)
print("bfin-sim.c: Blackfin+ 32-bit fractional multiply added")
