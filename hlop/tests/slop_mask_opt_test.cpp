//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// Differential test for the contiguous-range mask reads and writes
//   Slop<N>::set_mask_op_opt(lo, hi, value)
//   Slop<N>::clear_mask_op_opt(lo, hi)
//   Slop<N>::get_mask_op_opt(x, lo, hi)
//   Slop_u<N>::get_mask_op_opt(x, lo, hi)
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

// =========================================================================
// get_mask_op_opt -- the READ side
// =========================================================================

// Independent model: bit k of the result is bit lo+k of the source, taken with
// bit_test (which reads storage word by word and sign-extends past it, so a
// range reaching beyond the source's words picks up its sign). Everything above
// the range is zero -- EXCEPT when the range fills the result width, where the
// extracted top bit IS the result's sign and gets replicated. Built from
// or/shl only, sharing no code with the extraction under test.
template <int R, int A>
Slop<R> RefGetRange(const Slop<A>& x, int lo, int hi) {
  const int     len = hi - lo;
  const Slop<R> one = Slop<R>::create_integer(1);
  Slop<R>       r   = Slop<R>::create_integer(0);
  for (int k = 0; k < storage_bits<R>(); ++k) {
    const bool bit = (k < len) ? x.bit_test(lo + k) : (len == R && x.bit_test(hi - 1));
    if (bit) {
      r = r.or_op(one.shl_op(k));
    }
  }
  return r;
}

// One (lo, hi) read at result width R, checked every way it can be checked.
// `MW` is the width the equivalent Get_mask CONSTANT is built at: wide enough
// that the [lo, hi) mask stays positive there, which is what the general
// get_mask_op needs to agree (see the file header).
template <int R, int MW, int A>
void CheckGetRange(const Slop<A>& x, int lo, int hi) {
  const int len = hi - lo;
  ASSERT_LE(len, R);

  const Slop<R> got  = Slop<R>::get_mask_op_opt(x, lo, hi);
  const Slop<R> want = RefGetRange<R>(x, lo, hi);
  EXPECT_TRUE(SameBits(got, want)) << "get_mask_op_opt R=" << R << " A=" << A << " [" << lo << "," << hi << ")";
  EXPECT_TRUE(got.is_integer()) << "get_mask_op_opt result is an Integer";

  // The unsigned landing: same bits, in a carrier one bit wider than any range
  // it accepts, so it never takes the signed arm and is canonical by
  // construction.
  const Slop_u<R> gotu = Slop_u<R>::get_mask_op_opt(x, lo, hi);
  EXPECT_TRUE(SameBits(gotu.raw(), RefGetRange<R + 1>(x, lo, hi)))
      << "Slop_u get_mask_op_opt R=" << R << " A=" << A << " [" << lo << "," << hi << ")";
  EXPECT_FALSE(gotu.raw().is_negative()) << "Slop_u get_mask_op_opt stayed canonical";

  // Against the general Get_mask, wherever the two are defined to agree: the
  // mask form is a pure zero-extending pack, so it cannot express the signed
  // landing at len == R.
  if (len < R && hi <= MW) {
    const Slop<MW> mask = BitMask<MW>(lo, hi);
    ASSERT_FALSE(mask.is_negative()) << "premise: the mask is positive at width " << MW;
    EXPECT_TRUE(SameBits(got, Slop<R>::get_mask_op(x, mask)))
        << "get_mask_op vs _opt R=" << R << " A=" << A << " [" << lo << "," << hi << ")";
  }
}

// Positions worth reading from: every word boundary and its neighbours, the
// declared width, and PAST the storage -- where the source reads as its sign.
template <int A>
std::vector<int> ReadPositions(std::mt19937_64& rng, int n_random) {
  std::vector<int> p;
  auto             add = [&](int v) {
    if (v >= 0 && v <= storage_bits<A>() + 8) {
      p.push_back(v);
    }
  };
  for (int b = 0; b <= storage_bits<A>(); b += 64) {
    add(b - 1);
    add(b);
    add(b + 1);
  }
  add(A - 1);
  add(A);
  add(A + 1);
  add(storage_bits<A>() + 8);
  for (int i = 0; i < n_random; ++i) {
    add(static_cast<int>(rng() % (storage_bits<A>() + 9)));
  }
  std::sort(p.begin(), p.end());
  p.erase(std::unique(p.begin(), p.end()), p.end());
  return p;
}

// Every interesting (lo, len) against a pool of sources, at result width R.
template <int A, int R, int MW = (A > R ? A : R) + 1>
void RunGetSweep(uint64_t seed, int n_pool_random = 2, int n_random_pos = 5) {
  std::mt19937_64 rng{seed};
  const auto      pool = BuildPool<A>(rng, n_pool_random);
  const auto      pos  = ReadPositions<A>(rng, n_random_pos);

  std::vector<int> lens;
  for (int l : {1, 2, 3, 63, 64, 65, 127, 128, 129, R - 1, R}) {
    if (l >= 1 && l <= R) {
      lens.push_back(l);
    }
  }
  std::sort(lens.begin(), lens.end());
  lens.erase(std::unique(lens.begin(), lens.end()), lens.end());

  for (int lo : pos) {
    for (int len : lens) {
      for (const auto& x : pool) {
        CheckGetRange<R, MW>(x, lo, lo + len);
      }
    }
  }
}

// Sub-word widths, exhaustively: every (lo, hi) the asserts admit.
TEST(Slop_mask_opt, get_exhaustive_tiny_widths) {
  std::mt19937_64 rng{0x11};
  const auto      pool = BuildPool<8>(rng, 3);
  for (int lo = 0; lo <= 72; ++lo) {
    for (int len = 1; len <= 8; ++len) {
      for (const auto& x : pool) {
        CheckGetRange<8, 96>(x, lo, lo + len);   // range can fill the result -> signed landing
        CheckGetRange<20, 96>(x, lo, lo + len);  // range always narrower -> zero-filled
      }
    }
  }
}

// One word, the width every scalar signal lands at.
TEST(Slop_mask_opt, get_single_word_widths) {
  RunGetSweep<64, 64>(0x21);
  RunGetSweep<64, 20>(0x22);
  RunGetSweep<64, 1>(0x23);
  RunGetSweep<32, 32>(0x24);
  RunGetSweep<65, 64>(0x25);
  RunGetSweep<65, 65>(0x26);
}

// Multi-word sources -- the motivating shape is a wide one whose field fits a
// single word, but the multi-word result has to be right too.
TEST(Slop_mask_opt, get_multiword_widths) {
  RunGetSweep<128, 20>(0x31);
  RunGetSweep<128, 128>(0x32);
  RunGetSweep<200, 64>(0x33);
  RunGetSweep<200, 200>(0x34, /*n_pool_random=*/1, /*n_random_pos=*/3);
  RunGetSweep<544, 20>(0x35, /*n_pool_random=*/1, /*n_random_pos=*/3);
  RunGetSweep<544, 100>(0x36, /*n_pool_random=*/1, /*n_random_pos=*/3);
}

// A Slop_u source: canonical, so every bit at and above its width is zero and
// the fill above storage is a compile-time 0 rather than a sign read.
TEST(Slop_mask_opt, get_from_canonical_source) {
  std::mt19937_64 rng{0x41};
  for (int i = 0; i < 8; ++i) {
    const Slop_u<100> x{RandomValue<100>(rng)};
    for (int lo : {0, 1, 31, 63, 64, 65, 90, 99, 100, 101, 120, 128}) {
      for (int len : {1, 8, 20}) {
        const auto got = Slop_u<20>::get_mask_op_opt(x, lo, lo + len);
        // Read through the carrier: same contract, same model.
        EXPECT_TRUE(SameBits(got.raw(), RefGetRange<21>(x.raw(), lo, lo + len)))
            << "Slop_u source [" << lo << "," << lo + len << ")";
      }
    }
  }
}

// The shape this exists for, spelled out: a sub-word field of a one-word value.
TEST(Slop_mask_opt, get_field_extract_matches_shift_and_mask) {
  std::mt19937_64 rng{0x51};
  for (int i = 0; i < 64; ++i) {
    const auto     raw = static_cast<int64_t>(rng());
    const Slop<64> x   = Slop<64>::create_integer(raw);

    // Zero-filled: the result is wider than the field.
    const auto narrow = Slop<20>::get_mask_op_opt(x, 20, 30);
    EXPECT_EQ(narrow.to_just_i64(), (raw >> 20) & 0x3ff);

    // Signed: the field FILLS the result, so bit 29 lands as the sign.
    const auto exact = Slop<10>::get_mask_op_opt(x, 20, 30);
    EXPECT_EQ(exact.to_just_i64(), static_cast<int64_t>(static_cast<uint64_t>(raw) << 34) >> 54);
    EXPECT_EQ(exact.is_negative(), ((raw >> 29) & 1) != 0);

    // Unsigned: same bits, never negative.
    const auto u = Slop_u<10>::get_mask_op_opt(x, 20, 30);
    EXPECT_EQ(u.to_just_i64(), (raw >> 20) & 0x3ff);

    // A single-bit range is the UNSIGNED 0/1 -- the static Get_mask contract,
    // not the member form's signed -1.
    for (int b : {0, 1, 33, 62, 63}) {
      EXPECT_EQ(Slop<20>::get_mask_op_opt(x, b, b + 1).to_just_i64(), (raw >> b) & 1) << "bit " << b;
    }
  }
}

// A range reaching PAST the source's storage reads its sign, exactly as
// bit_test does -- the one place the extraction has to look at the value.
TEST(Slop_mask_opt, get_past_storage_reads_the_sign) {
  const auto neg = Slop<64>::create_integer(-1);
  const auto pos = Slop<64>::create_integer(1);

  EXPECT_EQ(Slop<20>::get_mask_op_opt(neg, 60, 70).to_just_i64(), 0x3ff);
  EXPECT_EQ(Slop<20>::get_mask_op_opt(pos, 60, 70).to_just_i64(), 0);
  EXPECT_EQ(Slop<20>::get_mask_op_opt(neg, 64, 72).to_just_i64(), 0xff);
  EXPECT_EQ(Slop<20>::get_mask_op_opt(pos, 64, 72).to_just_i64(), 0);

  // A canonical source has no sign to read: it is zero up there by invariant.
  const Slop_u<64> u{neg};
  EXPECT_EQ(Slop<20>::get_mask_op_opt(u, 60, 70).to_just_i64(), 0xf);  // bits 60..63 set, 64..69 zero
}

}  // namespace
