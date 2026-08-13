//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// Tests for the two halves of the canonical-unsigned simulation path:
//
//   Slop_u<N>          -- a Slop whose storage is GUARANTEED to already be the
//                         result of zext_to<N>(). The invariant is the whole
//                         point, so most of what is checked here is that the
//                         mask really did happen at the write (construction),
//                         and that nothing after it can dirty the words.
//   concat_op(lanes)   -- n-ary, MSB-first, lane widths taken from the operand
//                         TYPES. The reference model is a per-bit one built
//                         only from bit_test/shl (RefConcat below), which
//                         shares no code with either implementation.
//
// The Dlop side of concat_op (runtime lane widths) is cross-checked against
// this one in slop_dlop_diff_test.cpp.

#include <cstdint>
#include <random>
#include <string>

#include "checkpoint.hpp"
#include "gtest/gtest.h"
#include "slop.hpp"
#include "vcd_writer.hpp"

namespace {

// Reference: a lane's W bits read LSB-first into a uint64_t. Built from
// bit_test only, so it agrees with neither concat implementation by
// construction. Callers keep W <= 64.
template <int W, typename L>
uint64_t RefLane(const L& lane) {
  uint64_t v = 0;
  for (int i = 0; i < W; ++i) {
    if (lane.bit_test(i)) {
      v |= uint64_t(1) << i;
    }
  }
  return v;
}

}  // namespace

// ── the invariant ───────────────────────────────────────────────────────────

TEST(Slop_u_test, ctor_masks_every_source) {
  EXPECT_TRUE(Slop_u<8>(300) == 44);                                  // int64_t
  EXPECT_TRUE(Slop_u<8>(-1) == 255);                                  // negative int64_t
  EXPECT_TRUE(Slop_u<8>(Slop<9>::create_integer(-1)) == 255);         // Slop
  EXPECT_TRUE(Slop_u<8>(Slop<4>::create_integer(-1)) == 15);          // narrower Slop: 4-bit -1
  EXPECT_TRUE(Slop_u<4>(Slop_u<8>(255)) == 15);                       // narrowing Slop_u
  EXPECT_TRUE(Slop_u<12>(Slop_u<8>(255)) == 255);                     // widening Slop_u
  EXPECT_TRUE(Slop_u<8>::from_pyrope("-1") == 255);                   // literal
  EXPECT_TRUE(Slop_u<8>::from_pyrope("0x1ff") == 255);                // over-wide literal
  EXPECT_TRUE(Slop_u<3>::from_binary("111", /*unsigned_result=*/true) == 7);
}

// Slop storage above bit N-1 is deliberately non-canonical; Slop_u's is not.
TEST(Slop_u_test, canonical_where_slop_is_dirty) {
  const Slop<8> dirty = Slop<8>::add_op(Slop<8>::create_integer(200), Slop<8>::create_integer(100));
  EXPECT_EQ(dirty.to_i64_low(), 300);  // the lazy op left 300 in the words

  const Slop_u<8> clean{dirty};
  EXPECT_TRUE(clean == 44);
  EXPECT_EQ(clean.raw().to_just_i64(), 44);  // raw() needs no further masking
  EXPECT_FALSE(clean.raw().is_negative());   // never negative, by construction

  // Every bit at and above N is zero in the carrier.
  for (int i = 8; i < 64; ++i) {
    EXPECT_FALSE(clean.raw().bit_test(i)) << "carrier bit " << i;
  }
}

TEST(Slop_u_test, wide_ctor_masks) {
  const Slop_u<65> u{Slop<200>::from_pyrope("0x3_ffff_ffff_ffff_ffff")};  // 66 bits set
  EXPECT_EQ(u.to_hex(), "1ffffffffffffffff");                            // masked to 65
  EXPECT_TRUE(u.bit_test(64));
  EXPECT_FALSE(u.bit_test(65));
  EXPECT_EQ(u.get_bits(), 66);  // magnitude + sign slot
}

// ── conversions ─────────────────────────────────────────────────────────────

TEST(Slop_u_test, conversion_to_signed) {
  const Slop_u<8> u{255};
  EXPECT_EQ(Slop<9>{u}.to_just_i64(), 255);  // wider carrier: the value
  EXPECT_EQ(Slop<8>{u}.to_just_i64(), -1);   // exact width: reinterpreted signed
  EXPECT_EQ(Slop<4>{u}.to_just_i64(), -1);   // narrower: low 4 bits, sign-fit

  // zext_to keeps the low min(N, W) bits at any carrier. Like Slop::zext_to it
  // hands back the MASKED value, not a sign-canonical one -- Slop<8>{u} above
  // is the spelling that re-reads those same bits as signed.
  EXPECT_EQ(u.zext_to<8>().to_just_i64(), 255);
  EXPECT_EQ((u.zext_to<8, 16>().to_just_i64()), 255);
  EXPECT_EQ(u.zext_to<4>().to_just_i64(), 15);  // truncation to 4 bits
  EXPECT_EQ((u.zext_to<4, 8>().to_just_i64()), 15);
}

TEST(Slop_u_test, arithmetic_goes_through_raw) {
  const Slop_u<8> a{200};
  const Slop_u<8> b{100};
  // The mixed-width statics take .raw() at any width -- no zext_to anywhere.
  EXPECT_EQ(Slop<10>::add_op(a.raw(), b.raw()).to_just_i64(), 300);
  EXPECT_EQ(Slop<17>::mult_op(a.raw(), b.raw()).to_just_i64(), 20000);
  EXPECT_EQ(Slop<9>::sub_op(a.raw(), b.raw()).to_just_i64(), 100);
}

TEST(Slop_u_test, comparisons_are_unsigned) {
  const Slop_u<8> big{255};  // reads as -1 through a signed Slop<8>
  const Slop_u<8> one{1};
  EXPECT_TRUE(big > one);
  EXPECT_FALSE(big < one);
  EXPECT_TRUE(big != one);
  EXPECT_TRUE(big == Slop_u<8>(255));
  EXPECT_TRUE(big.gt_op(one) == 1);
  EXPECT_TRUE(big.lt_op(one) == 0);
  EXPECT_TRUE(big.ge_op(big) == 1);
  EXPECT_TRUE(big.eq_op(one) == 0);
  // Mixed widths compare by value.
  EXPECT_TRUE(Slop_u<16>(255) == big);
  EXPECT_TRUE(Slop_u<16>(256) > big);
  // int64_t comparison never masks the right-hand side.
  EXPECT_TRUE(big == 255);
  EXPECT_FALSE(big == 511);
  EXPECT_FALSE(big == -1);
}

// A compare must read every word of both carriers. The mixed-width Slop
// statics read their operands at the RESULT width, so naming a narrow result
// type would silently drop the high words (regression: Slop<2>::eq_op compared
// only bit [63:0] of a 100-bit operand, making 2^65 == 3*2^64).
TEST(Slop_u_test, comparisons_read_every_word) {
  const Slop_u<100> lo{Slop<128>::from_pyrope("0x1")};
  const Slop_u<100> hi{Slop<128>::from_pyrope("0x1_0000_0000_0000_0000")};  // 2^64
  EXPECT_TRUE(hi > lo);
  EXPECT_FALSE(hi < lo);
  EXPECT_TRUE(hi != lo);

  // Two values that differ ONLY above bit 63.
  const Slop_u<100> a{Slop<128>::from_pyrope("0x2_0000_0000_0000_0000")};
  const Slop_u<100> b{Slop<128>::from_pyrope("0x3_0000_0000_0000_0000")};
  EXPECT_FALSE(a == b);
  EXPECT_TRUE(a < b);
  EXPECT_TRUE(b > a);
  EXPECT_TRUE(a.eq_op(b) == 0);
  EXPECT_TRUE(a.lt_op(b) == 1);
  EXPECT_TRUE(b.ge_op(a) == 1);

  // Mixed widths, both multi-word.
  EXPECT_TRUE(Slop_u<200>(a) == a);
  EXPECT_TRUE(Slop_u<200>(b) > a);
}

TEST(Slop_u_test, slop_update_masks_once) {
  Slop_u<8> st{};
  EXPECT_TRUE(slop_update(st, Slop<9>::create_integer(5)));
  EXPECT_FALSE(slop_update(st, Slop<9>::create_integer(5)));
  EXPECT_FALSE(slop_update(st, Slop<9>::create_integer(261)));  // 261 & 0xff == 5
  EXPECT_TRUE(st == 5);
  EXPECT_TRUE(slop_update(st, Slop_u<4>(6)));
  EXPECT_TRUE(st == 6);
}

// ── concat_op ───────────────────────────────────────────────────────────────

TEST(Slop_u_test, concat_is_msb_first) {
  const Slop<3>   a = Slop<3>::create_integer(-1);  // 0b111
  const Slop_u<5> b{5};                             // 0b00101

  EXPECT_TRUE(Slop_u<8>::concat_op(a, b) == 0b111'00101);
  EXPECT_TRUE(Slop_u<8>::concat_op(b, a) == 0b00101'111);  // order matters
}

TEST(Slop_u_test, concat_landing_signed_vs_unsigned) {
  const Slop<3>   a = Slop<3>::create_integer(-1);
  const Slop_u<5> b{31};
  // 8 assembled bits, all ones.
  EXPECT_TRUE(Slop_u<8>::concat_op(a, b) == 255);          // zero-extended
  EXPECT_EQ(Slop<8>::concat_op(a, b).to_just_i64(), -1);   // sign-extended from bit 7
  EXPECT_EQ(Slop<10>::concat_op(a, b).to_just_i64(), -1);  // ... and it stays negative
  EXPECT_TRUE(Slop_u<10>::concat_op(a, b) == 255);         // ... while this stays 255

  // A top lane whose MSB is clear lands non-negative either way.
  const Slop<3> c = Slop<3>::create_integer(3);  // 0b011
  EXPECT_EQ(Slop<10>::concat_op(c, b).to_just_i64(), 0b011'11111);
  EXPECT_TRUE(Slop_u<10>::concat_op(c, b) == 0b011'11111);
}

TEST(Slop_u_test, concat_masks_lazy_lanes_only) {
  // A Slop lane carries dirty upper bits; the mask is part of the lane read.
  const Slop<8> dirty = Slop<8>::add_op(Slop<8>::create_integer(200), Slop<8>::create_integer(100));
  EXPECT_TRUE(Slop_u<16>::concat_op(dirty, Slop_u<8>{0xff}) == ((300 & 0xff) << 8 | 0xff));

  // Slop<3>{-1} has all 64 bits set; only the low 3 reach the window.
  EXPECT_TRUE(Slop_u<6>::concat_op(Slop<3>::create_integer(-1), Slop<3>::create_integer(0)) == 0b111'000);
}

TEST(Slop_u_test, concat_lane_counts) {
  EXPECT_TRUE(Slop_u<4>::concat_op(Slop_u<4>{9}) == 9);  // one lane
  EXPECT_TRUE(Slop_u<9>::concat_op(Slop_u<3>{7}, Slop_u<2>{1}, Slop_u<4>{9}) == ((7 << 6) | (1 << 4) | 9));
  // Five 1-bit lanes: the packed-bit-ring shape.
  EXPECT_TRUE(Slop_u<5>::concat_op(Slop_u<1>{1}, Slop_u<1>{0}, Slop_u<1>{1}, Slop_u<1>{1}, Slop_u<1>{0}) == 0b10110);
}

TEST(Slop_u_test, concat_wider_result_than_lanes) {
  // The result carrier may exceed the lane sum; the extra bits stay zero for
  // an unsigned landing.
  const Slop_u<32> r = Slop_u<32>::concat_op(Slop_u<3>{5}, Slop_u<5>{3});
  EXPECT_TRUE(r == ((5 << 5) | 3));
  EXPECT_EQ(r.get_bits(), 9);  // 163 -> 8 magnitude bits + sign slot
  for (int i = 8; i < 32; ++i) {
    EXPECT_FALSE(r.bit_test(i));
  }
}

TEST(Slop_u_test, concat_multiword) {
  // Lanes crossing word boundaries, both directions.
  const auto w = Slop_u<130>::concat_op(Slop_u<65>{-1}, Slop_u<65>{-1});
  EXPECT_EQ(w.to_hex(), "3ffffffffffffffffffffffffffffffff");
  EXPECT_TRUE(w.bit_test(0));
  EXPECT_TRUE(w.bit_test(64));
  EXPECT_TRUE(w.bit_test(129));

  EXPECT_EQ(Slop_u<128>::concat_op(Slop_u<64>{1}, Slop_u<64>{3}).to_hex(), "10000000000000003");
  // A lane that starts exactly on a word boundary.
  EXPECT_EQ(Slop_u<128>::concat_op(Slop_u<64>{-1}, Slop_u<64>{0}).to_hex(), "ffffffffffffffff0000000000000000");
  // ... and one that does not.
  EXPECT_EQ(Slop_u<100>::concat_op(Slop_u<36>{-1}, Slop_u<64>{0}).to_hex(), "fffffffff0000000000000000");
}

// The signed landing sign-extends from bit total-1, including when that bit is
// the top of a word (where a missed sext would leave a positive value).
TEST(Slop_u_test, concat_signed_landing_on_word_boundary) {
  const auto s64 = Slop<64>::concat_op(Slop<32>::create_integer(-1), Slop_u<32>{0});
  EXPECT_TRUE(s64.is_negative());
  EXPECT_EQ(s64.to_i64_low(), static_cast<int64_t>(0xffffffff00000000ULL));
  EXPECT_EQ(s64.to_hex(), "-100000000");  // magnitude of -2^32

  const auto s128 = Slop<128>::concat_op(Slop<64>::create_integer(-1), Slop_u<64>{0});
  EXPECT_TRUE(s128.is_negative());
  EXPECT_EQ(s128.to_hex(), "-10000000000000000");

  // The same bits with an unsigned landing stay positive.
  EXPECT_EQ(Slop_u<64>::concat_op(Slop<32>::create_integer(-1), Slop_u<32>{0}).to_hex(), "ffffffff00000000");
  EXPECT_EQ(Slop_u<128>::concat_op(Slop<64>::create_integer(-1), Slop_u<64>{0}).to_hex(), "ffffffffffffffff0000000000000000");
}

// Per-bit reference model: bit (offset_i + k) of the result is bit k of lane i.
TEST(Slop_u_test, concat_matches_per_bit_reference) {
  std::mt19937_64 rng{0xC04CA7ULL};
  for (int iter = 0; iter < 2000; ++iter) {
    const Slop<7>    a = Slop<7>::create_integer(static_cast<int64_t>(rng()));
    const Slop_u<11> b{static_cast<int64_t>(rng())};
    const Slop<13>   c = Slop<13>::create_integer(static_cast<int64_t>(rng()));

    const uint64_t ra = RefLane<7>(a);
    const uint64_t rb = RefLane<11>(b);
    const uint64_t rc = RefLane<13>(c);
    const uint64_t expected = (ra << 24) | (rb << 13) | rc;

    const auto got = Slop_u<31>::concat_op(a, b, c);
    ASSERT_TRUE(got == static_cast<int64_t>(expected))
        << "iter " << iter << " got " << got.to_hex() << " want " << std::hex << expected;

    // The signed landing is the same bits, sign-extended from bit 30.
    const auto signed_got = Slop<31>::concat_op(a, b, c);
    const int64_t want_signed = static_cast<int64_t>(expected << 33) >> 33;
    ASSERT_EQ(signed_got.to_just_i64(), want_signed) << "iter " << iter;
  }
}

// ── ABI surfaces: checkpoint + VCD ──────────────────────────────────────────

TEST(Slop_u_test, checkpoint_pyrope_roundtrip) {
  for (int64_t v : {0, 1, 5, 127, 128, 254, 255}) {
    const Slop_u<8> u{v};
    const Slop_u<8> back = Slop_u<8>::from_pyrope(u.to_pyrope());
    EXPECT_TRUE(back == u) << "v=" << v << " pyrope=" << u.to_pyrope();
  }
  // A wide value round-trips through the multi-word hex form.
  const Slop_u<130> w = Slop_u<130>::concat_op(Slop_u<65>{-1}, Slop_u<65>{7});
  EXPECT_TRUE(Slop_u<130>::from_pyrope(w.to_pyrope()) == w);
}

TEST(Slop_u_test, checkpoint_mem_hex_roundtrip) {
  const char* t    = std::getenv("TEST_TMPDIR");
  std::string path = std::string(t != nullptr ? t : "/tmp") + "/slop_u_mem.hex";

  std::array<Slop_u<12>, 4> mem{Slop_u<12>{0}, Slop_u<12>{0xfff}, Slop_u<12>{0x123}, Slop_u<12>{0x800}};
  hlop::ckpt::write_mem_hex(path, mem);

  std::array<Slop_u<12>, 4> back{};
  ASSERT_TRUE(hlop::ckpt::read_mem_hex(path, back));
  for (std::size_t i = 0; i < mem.size(); ++i) {
    EXPECT_TRUE(back[i] == mem[i]) << "entry " << i;
  }

  // The format does not record signedness: the same file loads into Slop<12>.
  std::array<Slop<12>, 4> as_signed{};
  ASSERT_TRUE(hlop::ckpt::read_mem_hex(path, as_signed));
  for (std::size_t i = 0; i < mem.size(); ++i) {
    EXPECT_EQ(hlop::ckpt::slop_to_hex(as_signed[i]), hlop::ckpt::slop_to_hex(mem[i])) << "entry " << i;
  }
}

TEST(Slop_u_test, vcd_bits) {
  EXPECT_EQ(vcd::to_vcd_bits(Slop_u<8>{5}, 8), "b00000101");
  EXPECT_EQ(vcd::to_vcd_bits(Slop_u<1>{1}, 1), "1");
  EXPECT_EQ(vcd::to_vcd_bits(Slop_u<1>{0}, 1), "0");
  // to_binary() prints the DECLARED width (no sign slot).
  EXPECT_EQ(Slop_u<8>{255}.to_binary(), "11111111");
  EXPECT_EQ(Slop_u<3>{2}.to_binary(), "010");
}
