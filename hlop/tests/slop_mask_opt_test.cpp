//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// Differential test for the contiguous-range mask writes
//   Slop<N>::set_mask_op_opt(lo, hi, value)
//   Slop<N>::clear_mask_op_opt(lo, hi)
//
// These are set_mask_op specialized to the mask shape a packed-field write
// always has, and set_mask_op's fast path now DELEGATES to set_mask_op_opt --
// so checking one against the other would prove nothing. Every check here goes
// against RefSetRange(): an independent per-bit model of the contract ("bit
// lo+k of the result is bit k of value, everything else is untouched") built
// only from and/or/not/shl, which share no code with either mask op.
//
// The general set_mask_op(mask, value) is still compared where it is defined to
// agree -- i.e. when the [lo, hi) mask is NON-NEGATIVE at width N. That caveat
// is not a gap in the _opt ops, it is the representational limit of passing a
// mask through a fixed-width Slop: at N == 128 the mask for bits [64, 128) has
// storage bit 127 set, so set_mask_op reads it as the NEGATIVE mask "every bit
// except the low 64" and computes something else entirely. The _opt forms take
// the range directly and are immune, so they stay correct there too -- which is
// exactly what SetRangeNotRepresentableAsPositiveMask pins down.

#include <cstdint>
#include <random>
#include <string>
#include <vector>

#include "gtest/gtest.h"
#include "slop.hpp"

namespace {

// Storage is a whole number of 64-bit words; bits in [N, storage) are the
// sign-extension region that the ops must carry through untouched, so every
// comparison below walks all of them.
template <int W>
constexpr int storage_bits() {
  return ((W + 63) / 64) * 64;
}

// A uniformly random storage-wide value. Built from 32-bit chunks so that
// create_integer() only ever sees a positive int64 (no sign fill to undo); the
// top chunk still lands on the sign bit, so roughly half the pool is negative.
template <int W>
Slop<W> RandomValue(std::mt19937_64& rng) {
  Slop<W> r = Slop<W>::create_integer(0);
  for (int i = 0; i < storage_bits<W>(); i += 32) {
    r = r.shl_op(32).or_op(Slop<W>::create_integer(static_cast<int64_t>(rng() & 0xFFFFFFFFull)));
  }
  return r;
}

// The positive contiguous mask with exactly bits [lo, hi) set.
template <int W>
Slop<W> BitMask(int lo, int hi) {
  Slop<W> m = Slop<W>::create_integer(0);
  for (int b = lo; b < hi; ++b) {
    m = m.or_op(Slop<W>::create_integer(1).shl_op(b));
  }
  return m;
}

// Independent model: overwrite bits [lo, hi) one at a time, taking value bits
// LSB-first. bit_test() sign-extends past storage, which is how a range wider
// than the value's significant bits gets sign-filled.
template <int W>
Slop<W> RefSetRange(const Slop<W>& x, int lo, int hi, const Slop<W>& value) {
  Slop<W> r = x;
  for (int i = lo; i < hi; ++i) {
    const Slop<W> bit = Slop<W>::create_integer(1).shl_op(i);
    r                 = value.bit_test(i - lo) ? r.or_op(bit) : r.and_op(bit.not_op());
  }
  return r;
}

template <int W>
::testing::AssertionResult SameBits(const Slop<W>& got, const Slop<W>& want) {
  for (int i = 0; i < storage_bits<W>(); ++i) {
    if (got.bit_test(i) != want.bit_test(i)) {
      return ::testing::AssertionFailure() << "bit " << i << ": got " << got.bit_test(i) << " want " << want.bit_test(i)
                                           << "\n  got  " << got.to_pyrope() << "\n  want " << want.to_pyrope();
    }
  }
  return ::testing::AssertionSuccess();
}

template <int W>
std::vector<Slop<W>> BuildPool(std::mt19937_64& rng, int n_random) {
  std::vector<Slop<W>> pool;
  pool.push_back(Slop<W>::create_integer(0));
  pool.push_back(Slop<W>::create_integer(-1));  // sign fill: every unwritten value bit is 1
  pool.push_back(Slop<W>::create_integer(1));
  for (int i = 0; i < n_random; ++i) {
    pool.push_back(RandomValue<W>(rng));
  }
  return pool;
}

// One (lo, hi, x, value) case, checked every way it can be checked.
template <int W>
void CheckRange(int lo, int hi, const Slop<W>& x, const Slop<W>& value, const Slop<W>& mask) {
  const Slop<W> zero = Slop<W>::create_integer(0);

  EXPECT_TRUE(SameBits(x.set_mask_op_opt(lo, hi, value), RefSetRange(x, lo, hi, value)))
      << "set_mask_op_opt W=" << W << " [" << lo << "," << hi << ")";
  EXPECT_TRUE(SameBits(x.clear_mask_op_opt(lo, hi), RefSetRange(x, lo, hi, zero)))
      << "clear_mask_op_opt W=" << W << " [" << lo << "," << hi << ")";

  // The _opt ops keep the operand's type tag, exactly as set_mask_op does.
  EXPECT_EQ(x.set_mask_op_opt(lo, hi, value).is_integer(), x.is_integer());
  EXPECT_EQ(x.clear_mask_op_opt(lo, hi).is_integer(), x.is_integer());

  // Against the general op, wherever the mask survives the round trip through
  // a width-W Slop as a positive value (see the file header).
  if (!mask.is_negative()) {
    EXPECT_TRUE(SameBits(x.set_mask_op_opt(lo, hi, value), x.set_mask_op(mask, value)))
        << "set_mask_op vs _opt W=" << W << " [" << lo << "," << hi << ")";
    EXPECT_TRUE(SameBits(x.clear_mask_op_opt(lo, hi), x.set_mask_op(mask, zero)))
        << "set_mask_op vs clear_opt W=" << W << " [" << lo << "," << hi << ")";
  }
}

// Every (lo, hi) pair with 0 <= lo <= hi <= W, against every ordered pair of
// pool values. Only for widths where that is cheap.
template <int W>
void RunExhaustive(uint64_t seed) {
  std::mt19937_64 rng{seed};
  const auto      pool = BuildPool<W>(rng, 2);

  for (int lo = 0; lo <= W; ++lo) {
    for (int hi = lo; hi <= W; ++hi) {
      const Slop<W> mask = BitMask<W>(lo, hi);
      for (const auto& x : pool) {
        for (const auto& v : pool) {
          CheckRange<W>(lo, hi, x, v, mask);
        }
      }
    }
  }
}

// Word boundaries, the two bits either side of each, the extremes, and a few
// random positions -- the places a word-wise splice goes wrong. Wide types have
// too many boundaries to take every one (the pair count is quadratic and every
// check runs an O(W) reference model), so they are strided; the first and last
// word are always kept, since those are where the partial-word masks live.
template <int W>
std::vector<int> SamplePositions(std::mt19937_64& rng, int max_boundaries, int n_random) {
  std::vector<int> p;
  auto             add = [&](int v) {
    if (v >= 0 && v <= W) {
      p.push_back(v);
    }
  };
  auto add_around = [&](int b) {
    add(b - 1);
    add(b);
    add(b + 1);
  };

  const int n_bounds = storage_bits<W>() / 64 + 1;  // 0, 64, ... storage
  const int stride   = (n_bounds + max_boundaries - 1) / max_boundaries;
  for (int i = 0; i < n_bounds; i += stride) {
    add_around(i * 64);
  }
  add_around(storage_bits<W>());
  add(0);
  add(1);
  add(W - 1);
  add(W);
  for (int i = 0; i < n_random; ++i) {
    add(static_cast<int>(rng() % (W + 1)));
  }
  std::sort(p.begin(), p.end());
  p.erase(std::unique(p.begin(), p.end()), p.end());
  return p;
}

template <int W>
void RunSampled(uint64_t seed, int max_boundaries = 8, int n_pool_random = 2, int n_random_pos = 6) {
  std::mt19937_64 rng{seed};
  const auto      pool = BuildPool<W>(rng, n_pool_random);
  const auto      pos  = SamplePositions<W>(rng, max_boundaries, n_random_pos);

  for (size_t a = 0; a < pos.size(); ++a) {
    for (size_t b = a; b < pos.size(); ++b) {
      const Slop<W> mask = BitMask<W>(pos[a], pos[b]);
      for (const auto& x : pool) {
        for (const auto& v : pool) {
          CheckRange<W>(pos[a], pos[b], x, v, mask);
        }
      }
    }
  }
}

// =========================================================================
// Exhaustive widths: sub-word, exactly one word, and just over one word.
// =========================================================================
TEST(Slop_mask_opt, exhaustive_tiny_widths) {
  RunExhaustive<1>(0xA1);
  RunExhaustive<2>(0xA2);
  RunExhaustive<3>(0xA3);
  RunExhaustive<7>(0xA7);
  RunExhaustive<8>(0xA8);
}

TEST(Slop_mask_opt, exhaustive_word_boundary_widths) {
  RunExhaustive<31>(0xB1);
  RunExhaustive<32>(0xB2);
  RunExhaustive<33>(0xB3);
  RunExhaustive<63>(0xB4);
  RunExhaustive<64>(0xB5);
  RunExhaustive<65>(0xB6);
}

// =========================================================================
// Sampled widths: multi-word, including the 544 the motivating design uses.
// =========================================================================
TEST(Slop_mask_opt, sampled_multiword_widths) {
  RunSampled<96>(0xC1);
  RunSampled<127>(0xC2);
  RunSampled<128>(0xC3);
  RunSampled<129>(0xC4);
  RunSampled<192>(0xC5);
  RunSampled<256>(0xC6);
  // 544 is the width of the design this optimization came from.
  RunSampled<544>(0xC7, /*max_boundaries=*/5, /*n_pool_random=*/1, /*n_random_pos=*/4);
  RunSampled<1024>(0xC8, /*max_boundaries=*/4, /*n_pool_random=*/1, /*n_random_pos=*/3);
}

// =========================================================================
// Targeted cases
// =========================================================================

// The generated line this optimization exists for: a 544-bit vector clearing
// bits [64, 256) via a 40-byte from_pyrope mask constant and a zero operand.
TEST(Slop_mask_opt, motivating_544bit_clear) {
  using S544 = Slop<544>;

  const auto v = S544::from_pyrope("0x1234567890abcdef1234567890abcdef1234567890abcdef1234567890abcdef");
  const auto m = S544::from_pyrope(
      "0x00000000000000000ffffffffffffffffffffffffffffffffffffffffffffffff0000000000000000");

  // The literal really is bits [64, 256): low 64 bits survive, nothing else.
  EXPECT_TRUE(SameBits(v.clear_mask_op_opt(64, 256), v.set_mask_op(m, S544::create_integer(0))));
  EXPECT_EQ(v.clear_mask_op_opt(64, 256).to_pyrope(), "0x1234567890abcdef");

  // And the set form against the same mask, with a value that must sign-fill
  // the top of the range (-1 is 1 bit wide; the range is 192).
  const auto val = S544::create_integer(-1);
  EXPECT_TRUE(SameBits(v.set_mask_op_opt(64, 256, val), v.set_mask_op(m, val)));
}

// An empty range is a no-op, matching set_mask_op's zero-mask early-out. The
// lo == hi == 0 case is the one that would run a word of work if the loop
// bound were derived from (hi - 1) / 64 without an emptiness guard.
TEST(Slop_mask_opt, empty_range_is_identity) {
  using S = Slop<200>;
  std::mt19937_64 rng{0xE0};
  const auto      x = RandomValue<200>(rng);
  const auto      v = S::create_integer(-1);

  for (int p : {0, 1, 63, 64, 65, 127, 128, 199, 200}) {
    EXPECT_TRUE(SameBits(x.set_mask_op_opt(p, p, v), x));
    EXPECT_TRUE(SameBits(x.clear_mask_op_opt(p, p), x));
    EXPECT_TRUE(SameBits(x.set_mask_op_opt(p, p - 1, v), x));  // hi < lo
    EXPECT_TRUE(SameBits(x.clear_mask_op_opt(p, p - 1), x));
  }
}

// A range whose bits are all ABOVE the declared width writes nothing, and one
// that straddles N writes only its in-width part. set_mask_op caps the write at
// out_bits (<= N); the delegation has to keep that cap or bits in the
// sign-extension region would start changing.
TEST(Slop_mask_opt, set_mask_op_caps_range_at_declared_width) {
  using S = Slop<100>;  // 100 significant bits, 128 bits of storage
  std::mt19937_64 rng{0xF0};
  const auto      x = RandomValue<100>(rng);
  const auto      v = S::create_integer(-1);

  // Mask bits [96, 120): positive at this width (storage bit 127 is clear) but
  // reaching past N == 100, so set_mask_op writes only [96, 100).
  const auto mask = BitMask<100>(96, 120);
  ASSERT_FALSE(mask.is_negative());
  EXPECT_TRUE(SameBits(x.set_mask_op(mask, v), RefSetRange<100>(x, 96, 100, v)));

  // Entirely above N: nothing is written at all.
  const auto above = BitMask<100>(104, 120);
  ASSERT_FALSE(above.is_negative());
  EXPECT_TRUE(SameBits(x.set_mask_op(above, v), x));
}

// Where set_mask_op cannot be asked the question but the _opt form can: at
// N == 128 the [64, 128) mask has storage bit 127 set, so set_mask_op sees a
// NEGATIVE mask and computes "every bit except the low 64" instead.
TEST(Slop_mask_opt, set_range_not_representable_as_positive_mask) {
  using S = Slop<128>;
  std::mt19937_64 rng{0xD0};
  const auto      x    = RandomValue<128>(rng);
  const auto      v    = S::create_integer(0x5a5a5a5a);
  const auto      mask = BitMask<128>(64, 128);

  ASSERT_TRUE(mask.is_negative()) << "premise: the top-half mask is negative at width 128";

  // _opt writes the intended high half.
  EXPECT_TRUE(SameBits(x.set_mask_op_opt(64, 128, v), RefSetRange<128>(x, 64, 128, v)));

  // The mask form cannot: reading that same constant as negative turns it into
  // "every bit except the low 64", so the write lands somewhere else entirely.
  // Asserted as a difference rather than as a specific value -- the negative
  // path's exact extent depends on out_bits (and so on the operands' widths),
  // and pinning it here would only duplicate set_mask_op's own tests.
  EXPECT_FALSE(SameBits(x.set_mask_op(mask, v), x.set_mask_op_opt(64, 128, v)));
}

}  // namespace
