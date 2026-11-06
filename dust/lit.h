#pragma once
// Literal constants folded by the compiler: log2 of a literal in Q16 and a
// literal in Q31. No tables and no floating point reach the device: each is
// evaluated at compile time where it is used.
#include <opxy/types.hpp>

namespace dust {
namespace lit {
constexpr double ln2 = 0.6931471805599453;
constexpr double series(double y2, double term, int k) { return k > 81 ? 0.0 : term / k + series(y2, term * y2, k + 2); }
constexpr double ln_near(double x) { return 2.0 * series(((x - 1) / (x + 1)) * ((x - 1) / (x + 1)), (x - 1) / (x + 1), 1); }
constexpr double ln(double x) { return x >= 2.0 ? ln2 + ln(x / 2.0) : x < 1.0 ? -ln2 + ln(x * 2.0) : ln_near(x); }
constexpr opxy::i32 round(double v) { return v >= 0 ? static_cast<opxy::i32>(v + 0.5) : -static_cast<opxy::i32>(-v + 0.5); }
}
// log2(x) in Q16 (65536 an octave), for a positive literal.
constexpr opxy::i32 L(double x) { return lit::round(lit::ln(x) / lit::ln2 * 65536.0); }
// A literal fraction in Q31, |x| < 1 (1 and over: the largest Q31).
constexpr opxy::i32 Q31(double x) { return x >= 1.0 ? 0x7fffffff : lit::round(x * 2147483648.0); }
// A literal in Q16.
constexpr opxy::i32 Q16(double x) { return lit::round(x * 65536.0); }
// Semitones and cents as log2 Q16.
constexpr opxy::i32 ST(double s) { return lit::round(s * 65536.0 / 12.0); }
}
