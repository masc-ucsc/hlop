//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// Differential test: Slop<W> versus benchref/sint.hpp (and __int128 where
// SInt isn't reliable).
//
// The goal is to catch Slop bugs at the single-word↔multi-word transition
// (W ∈ {62..67}). Coverage strategy:
//
//   • W ≤ 64 (single-word): SInt<W> is a reliable oracle for every op
//     except its known firrtl-sig quirk on signed lt/le/gt/ge for two
//     negative operands. Native `int64_t` is the oracle for those.
//
//   • W > 64 (multi-word): SInt has a carry-chain bug in `core_add_sub`
//     (carry detection `result < operand` misses the equal-and-carry-in
//     case), so its multi-word add/sub/mult would emit false positives.
//     We oracle arithmetic with __int128 instead, which is well-defined
//     up to 128 bits. SInt is still used for bitwise ops (no cross-word
//     carries) at every width.
//
// Operations exercised:
//   bitwise:  and, or, xor, not
//   arith:    add, sub, neg, mult, div, mod   (wrapped/sign-extended to W)
//   compare:  eq, lt, le, gt, ge
//   reduce:   ror (orr)                       (rand_op / rxor_op skipped —
//                                              Slop's fixed-width-vs-Pyrope
//                                              semantics diverge from
//                                              SInt's fixed-width view)
//   shifts:   lsh, rsh by runtime amount in [0..W-1]
//
// Widths exercised: 62, 63, 64, 65, 66, 67.

#include <array>
#include <cstdint>
#include <limits>
#include <random>
#include <vector>

#include "gtest/gtest.h"
#include "sint.hpp"
#include "slop.hpp"

namespace {

using i128 = __int128;
using u128 = unsigned __int128;

// Subclass-friend trick to expose SInt<W>::raw_copy_out, which is protected.
template <int W>
struct SIntPeek : public SInt<W> {
  SIntPeek(const SInt<W>& s) : SInt<W>(s) {}
  using SInt<W>::raw_copy_out;
};

// Convert Slop<W> to a {lo, hi} pair covering bits [0..127] via bit_test.
template <int W>
std::array<uint64_t, 2> slop_low128(const Slop<W>& s) {
  std::array<uint64_t, 2> out{0, 0};
  for (int i = 0; i < 128; ++i) {
    if (s.bit_test(i)) {
      out[i / 64] |= uint64_t{1} << (i % 64);
    }
  }
  return out;
}

// Convert a SInt<W> to a {lo, hi} pair, sign-extended from bit W-1 up to
// bit 127. Used for bitwise-op cross-checks at every width.
template <int W>
std::array<uint64_t, 2> sint_low128(const SInt<W>& s) {
  constexpr int            nw = (W + 63) / 64;
  std::array<uint64_t, nw> raw{};
  SIntPeek<W>(s).raw_copy_out(raw.data());

  std::array<uint64_t, 2> out{0, 0};
  if constexpr (nw >= 1) {
    out[0] = raw[0];
  }
  if constexpr (nw >= 2) {
    out[1] = raw[1];
  }

  constexpr int sign_word = (W - 1) / 64;
  constexpr int sign_bit  = (W - 1) % 64;
  bool          neg       = (raw[sign_word] >> sign_bit) & 1;
  uint64_t      fill      = neg ? ~uint64_t{0} : 0;
  if constexpr (W < 64) {
    uint64_t low_keep = (uint64_t{1} << W) - 1;
    out[0]            = (out[0] & low_keep) | (fill & ~low_keep);
    out[1]            = fill;
  } else if constexpr (W == 64) {
    out[1] = fill;
  } else if constexpr (W < 128) {
    int      hi_bits = W - 64;
    uint64_t hi_keep = (uint64_t{1} << hi_bits) - 1;
    out[1]           = (out[1] & hi_keep) | (fill & ~hi_keep);
  }
  return out;
}

// Sign-extend a value to a W-bit signed __int128.
template <int W>
i128 sext_to_i128(i128 v) {
  if constexpr (W >= 128) {
    return v;
  } else {
    u128 mask = (u128{1} << W) - 1;
    u128 lo   = static_cast<u128>(v) & mask;
    bool neg  = (lo >> (W - 1)) & 1;
    return neg ? static_cast<i128>(lo | ~mask) : static_cast<i128>(lo);
  }
}

// Pack a W-bit-signed __int128 into a 128-bit two's-complement view.
std::array<uint64_t, 2> i128_to_words(i128 v) {
  u128 u = static_cast<u128>(v);
  return {static_cast<uint64_t>(u), static_cast<uint64_t>(u >> 64)};
}

template <int N>
bool uint_truth(const UInt<N>& u) {
  return u.as_single_word() != 0;
}

template <int W>
bool slop_truth(const Slop<W>& s) {
  return s.is_known_true();
}

template <int W>
int64_t mask_to_w(int64_t v) {
  if constexpr (W >= 64) {
    return v;
  } else {
    int64_t mask = (int64_t{1} << W) - 1;
    int64_t lo   = v & mask;
    bool    neg  = (lo >> (W - 1)) & 1;
    return neg ? (lo | ~mask) : lo;
  }
}

// Compare bits [0..W) of two 128-bit views; return -1 on match or the
// lowest mismatching bit position.
template <int W>
int first_diff_bit(std::array<uint64_t, 2> a, std::array<uint64_t, 2> b) {
  for (int i = 0; i < W; ++i) {
    bool ab = (a[i / 64] >> (i % 64)) & 1;
    bool bb = (b[i / 64] >> (i % 64)) & 1;
    if (ab != bb) {
      return i;
    }
  }
  return -1;
}

#define EXPECT_BITS_EQ(W_, GOT, WANT, OP)                                                                                    \
  do {                                                                                                                       \
    auto _g = (GOT);                                                                                                         \
    auto _w = (WANT);                                                                                                        \
    int  _b = first_diff_bit<W_>(_g, _w);                                                                                    \
    if (_b >= 0) {                                                                                                           \
      ADD_FAILURE() << "W=" << W_ << " iter=" << iter << " op=" << OP << " bit=" << _b << " a=0x" << std::hex                \
                    << static_cast<uint64_t>(a) << " b=0x" << static_cast<uint64_t>(b) << " got=0x" << _g[1] << ":" << _g[0] \
                    << " want=0x" << _w[1] << ":" << _w[0] << std::dec;                                                      \
      return;                                                                                                                \
    }                                                                                                                        \
  } while (0)

#define EXPECT_TRUTH_EQ(GOT, WANT, OP)                                                                               \
  do {                                                                                                               \
    bool _g = (GOT);                                                                                                 \
    bool _w = (WANT);                                                                                                \
    if (_g != _w) {                                                                                                  \
      ADD_FAILURE() << "iter=" << iter << " op=" << OP << " a=0x" << std::hex << static_cast<uint64_t>(a) << " b=0x" \
                    << static_cast<uint64_t>(b) << std::dec << " got=" << _g << " want=" << _w;                      \
      return;                                                                                                        \
    }                                                                                                                \
  } while (0)

template <int W>
void CheckPair(int64_t a_in, int64_t b_in, int iter) {
  int64_t a = mask_to_w<W>(a_in);
  int64_t b = mask_to_w<W>(b_in);

  Slop<W> a_sl(a), b_sl(b);
  SInt<W> a_si(a), b_si(b);

  i128 a128 = sext_to_i128<W>(static_cast<i128>(a));
  i128 b128 = sext_to_i128<W>(static_cast<i128>(b));

  // ----- Bitwise (SInt oracle: bit-local, no cross-word carries). -----
  EXPECT_BITS_EQ(W, slop_low128(a_sl.and_op(b_sl)), sint_low128<W>(SInt<W>(a_si & b_si)), "and");
  EXPECT_BITS_EQ(W, slop_low128(a_sl.or_op(b_sl)), sint_low128<W>(SInt<W>(a_si | b_si)), "or");
  EXPECT_BITS_EQ(W, slop_low128(a_sl.xor_op(b_sl)), sint_low128<W>(SInt<W>(a_si ^ b_si)), "xor");
  EXPECT_BITS_EQ(W, slop_low128(a_sl.not_op()), sint_low128<W>(SInt<W>(~a_si)), "not");

  // ----- Arithmetic (__int128 oracle — SInt's core_add_sub has a
  //       multi-word carry bug that produces wrong results for some
  //       edge cases at W>64). -----
  EXPECT_BITS_EQ(W, slop_low128(a_sl.add_op(b_sl)), i128_to_words(sext_to_i128<W>(a128 + b128)), "add");
  EXPECT_BITS_EQ(W, slop_low128(a_sl.sub_op(b_sl)), i128_to_words(sext_to_i128<W>(a128 - b128)), "sub");
  EXPECT_BITS_EQ(W, slop_low128(a_sl.neg_op()), i128_to_words(sext_to_i128<W>(-a128)), "neg");
  EXPECT_BITS_EQ(W, slop_low128(a_sl.mult_op(b_sl)), i128_to_words(sext_to_i128<W>(a128 * b128)), "mult");

  // ----- Div / mod — SInt supports only ≤64-bit, but native int64_t is
  //       a safe oracle for any W ≤ 64.  For W > 64, Slop's div_op uses
  //       Blop::div which is single-word only (asserts), so we skip. -----
  if constexpr (W <= 64) {
    if (b != 0 && !(a == std::numeric_limits<int64_t>::min() && b == -1)) {
      EXPECT_BITS_EQ(W, slop_low128(a_sl.div_op(b_sl)), i128_to_words(sext_to_i128<W>(a128 / b128)), "div");
      EXPECT_BITS_EQ(W, slop_low128(a_sl.mod_op(b_sl)), i128_to_words(sext_to_i128<W>(a128 % b128)), "mod");
    }
  }

  // ----- Comparisons (native int64_t oracle — already masked to W-bit
  //       signed above).  SInt's signed le/ge are unreliable for two
  //       negative operands; eq is bitwise so we still cross-check it. -----
  EXPECT_TRUTH_EQ(slop_truth(a_sl.eq_op(b_sl)), a == b, "eq");
  EXPECT_TRUTH_EQ(slop_truth(a_sl.lt_op(b_sl)), a < b, "lt");
  EXPECT_TRUTH_EQ(slop_truth(a_sl.le_op(b_sl)), a <= b, "le");
  EXPECT_TRUTH_EQ(slop_truth(a_sl.gt_op(b_sl)), a > b, "gt");
  EXPECT_TRUTH_EQ(slop_truth(a_sl.ge_op(b_sl)), a >= b, "ge");
  EXPECT_TRUTH_EQ(slop_truth(a_sl.eq_op(b_sl)), uint_truth(a_si == b_si), "eq_sint");

  // ----- Reductions.
  //   ror_op: any bit set ≡ value != 0. Matches SInt's orr at every width.
  //   rand_op / rxor_op: Slop's semantics count over its stored bits (so
  //     sign-extension affects parity for negative values), distinct from
  //     SInt's fixed-W view. Skipped — see header.
  EXPECT_TRUTH_EQ(slop_truth(a_sl.ror_op()), uint_truth(a_si.orr()), "orr");

  // ----- Shifts: runtime amount in [0..W-1].  Use __int128 for the
  //       oracle so wide widths work uniformly.  SInt is also fine here
  //       (single-word inputs → cross-word carries don't apply), but
  //       __int128 keeps the comparator simple. -----
  for (uint64_t shamt :
       {uint64_t{0}, uint64_t{1}, uint64_t{5}, uint64_t{16}, uint64_t{31}, uint64_t{32}, uint64_t{63}, uint64_t{W - 1}}) {
    if (shamt >= static_cast<uint64_t>(W)) {
      continue;
    }
    EXPECT_BITS_EQ(W, slop_low128(a_sl.shl_op(static_cast<int64_t>(shamt))), i128_to_words(sext_to_i128<W>(a128 << shamt)), "lsh");
    EXPECT_BITS_EQ(W, slop_low128(a_sl.sra_op(static_cast<int64_t>(shamt))), i128_to_words(sext_to_i128<W>(a128 >> shamt)), "rsh");
  }
}

std::vector<int64_t> CornerValues() {
  return {
      0,
      1,
      -1,
      2,
      -2,
      7,
      -7,
      255,
      -256,
      std::numeric_limits<int64_t>::min(),
      std::numeric_limits<int64_t>::max(),
      std::numeric_limits<int64_t>::min() + 1,
      std::numeric_limits<int64_t>::max() - 1,
      int64_t{1} << 32,
      int64_t{1} << 60,
      int64_t{1} << 61,
      int64_t{1} << 62,
      -(int64_t{1} << 62),
      static_cast<int64_t>(0xAAAAAAAAAAAAAAAAULL),
      static_cast<int64_t>(0x5555555555555555ULL),
  };
}

template <int W>
void RunCorner() {
  auto vals = CornerValues();
  int  iter = 0;
  for (int64_t a : vals) {
    for (int64_t b : vals) {
      CheckPair<W>(a, b, iter++);
      if (::testing::Test::HasFailure()) {
        return;
      }
    }
  }
}

template <int W>
void RunFuzz(int seed_xor) {
  std::mt19937_64 rng{0xC0FFEEDEADBEEFULL ^ static_cast<uint64_t>(seed_xor)};
  for (int iter = 0; iter < 500; ++iter) {
    int64_t a = static_cast<int64_t>(rng());
    int64_t b = static_cast<int64_t>(rng());
    CheckPair<W>(a, b, iter);
    if (::testing::Test::HasFailure()) {
      return;
    }
  }
}

}  // namespace

class SlopSintDiff : public ::testing::Test {};

TEST_F(SlopSintDiff, corner_W62) { RunCorner<62>(); }
TEST_F(SlopSintDiff, corner_W63) { RunCorner<63>(); }
TEST_F(SlopSintDiff, corner_W64) { RunCorner<64>(); }
TEST_F(SlopSintDiff, corner_W65) { RunCorner<65>(); }
TEST_F(SlopSintDiff, corner_W66) { RunCorner<66>(); }
TEST_F(SlopSintDiff, corner_W67) { RunCorner<67>(); }

TEST_F(SlopSintDiff, fuzz_W62) { RunFuzz<62>(62); }
TEST_F(SlopSintDiff, fuzz_W63) { RunFuzz<63>(63); }
TEST_F(SlopSintDiff, fuzz_W64) { RunFuzz<64>(64); }
TEST_F(SlopSintDiff, fuzz_W65) { RunFuzz<65>(65); }
TEST_F(SlopSintDiff, fuzz_W66) { RunFuzz<66>(66); }
TEST_F(SlopSintDiff, fuzz_W67) { RunFuzz<67>(67); }
