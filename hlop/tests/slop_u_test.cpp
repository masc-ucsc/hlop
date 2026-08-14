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

// ── Slop_operand: a Slop_u drops into the mixed-width statics bare ───────────
//
// The statics used to take `const Slop<A>&`, so a Slop_u operand had to spell
// `.raw()` — and `.raw()` hands back a carrier whose canonicality the compiler
// cannot see, so widening it re-ran the runtime sign fill. `Slop_arg` gives the
// statics the width AND the canonicality, which is what lets `zero_widen_`
// replace `widen_`. Both must produce the same VALUE at every width: that is
// the whole correctness claim, because a canonical carrier's sign slot is zero,
// so sign-filling and zero-filling agree.

namespace {

// The invariant, checked from outside the class: the stored carrier is its own
// zext_to<N>. Every Slop_u a new entry point hands back must satisfy it.
template <int N>
bool canonical_ok(const Slop_u<N>& u) {
  return u.raw().identical(u.raw().template zext_to<N, N + 1>());
}

}  // namespace

TEST(Slop_u_test, slop_operand_matches_raw_one_word) {
  const Slop_u<8> a{200};
  const Slop_u<8> b{100};

  EXPECT_TRUE(Slop<10>::add_op(a, b).identical(Slop<10>::add_op(a.raw(), b.raw())));
  EXPECT_TRUE(Slop<10>::sub_op(a, b).identical(Slop<10>::sub_op(a.raw(), b.raw())));
  EXPECT_TRUE(Slop<20>::mult_op(a, b).identical(Slop<20>::mult_op(a.raw(), b.raw())));
  EXPECT_TRUE(Slop<9>::and_op(a, b).identical(Slop<9>::and_op(a.raw(), b.raw())));
  EXPECT_TRUE(Slop<9>::or_op(a, b).identical(Slop<9>::or_op(a.raw(), b.raw())));
  EXPECT_TRUE(Slop<9>::xor_op(a, b).identical(Slop<9>::xor_op(a.raw(), b.raw())));
  EXPECT_TRUE(Slop<2>::eq_op(a, b).identical(Slop<2>::eq_op(a.raw(), b.raw())));
  EXPECT_TRUE(Slop<2>::lt_op(a, b).identical(Slop<2>::lt_op(a.raw(), b.raw())));
  EXPECT_TRUE(Slop<2>::gt_op(a, b).identical(Slop<2>::gt_op(a.raw(), b.raw())));
  EXPECT_TRUE(Slop<12>::shl_op(a, 3).identical(Slop<12>::shl_op(a.raw(), 3)));
  EXPECT_TRUE(Slop<9>::sra_op(a, 3).identical(Slop<9>::sra_op(a.raw(), 3)));

  // Mixed: one canonical operand, one lazy — the common cgen shape.
  const Slop<9> lazy = Slop<9>::add_op(Slop<9>::create_integer(200), Slop<9>::create_integer(100));
  EXPECT_TRUE(Slop<11>::add_op(a, lazy).identical(Slop<11>::add_op(a.raw(), lazy)));
  EXPECT_TRUE(Slop<11>::add_op(lazy, a).identical(Slop<11>::add_op(lazy, a.raw())));
}

// zero_widen_ vs widen_: this is where the two paths actually diverge in code.
// A Slop_u<64>'s carrier is Slop<65> whose sign slot is zero, so the sign fill
// widen_ computes is all-zero — the value must be identical either way.
TEST(Slop_u_test, slop_operand_matches_raw_multiword) {
  const Slop_u<64>  a{Slop<65>::from_pyrope("0xffff_ffff_ffff_ffff")};
  const Slop_u<64>  b{Slop<65>::from_pyrope("0x8000_0000_0000_0001")};
  const Slop_u<127> w{Slop<128>::from_pyrope("0x7fff_ffff_ffff_ffff_ffff_ffff_ffff_ffff")};

  EXPECT_TRUE(a == Slop_u<64>{Slop<65>::from_pyrope("0xffff_ffff_ffff_ffff")});

  EXPECT_TRUE(Slop<130>::add_op(a, b).identical(Slop<130>::add_op(a.raw(), b.raw())));
  EXPECT_TRUE(Slop<130>::sub_op(a, b).identical(Slop<130>::sub_op(a.raw(), b.raw())));
  EXPECT_TRUE(Slop<130>::and_op(a, w).identical(Slop<130>::and_op(a.raw(), w.raw())));
  EXPECT_TRUE(Slop<130>::or_op(a, w).identical(Slop<130>::or_op(a.raw(), w.raw())));
  EXPECT_TRUE(Slop<2>::eq_op(a, w).identical(Slop<2>::eq_op(a.raw(), w.raw())));
  EXPECT_TRUE(Slop<2>::lt_op(a, w).identical(Slop<2>::lt_op(a.raw(), w.raw())));
  EXPECT_TRUE(Slop<200>::shl_op(a, 65).identical(Slop<200>::shl_op(a.raw(), 65)));

  // The value itself: a u64 all-ones widened into a 130-bit carrier must stay
  // POSITIVE. This is the assertion that fails if a canonical carrier is ever
  // sign-filled from the wrong bit.
  const Slop<130> wide = Slop<130>::add_op(a, Slop<130>::create_integer(1));
  EXPECT_TRUE(wide.identical(Slop<130>::from_pyrope("0x1_0000_0000_0000_0000")));
}

TEST(Slop_u_test, slop_operand_get_mask_and_mux) {
  const Slop_u<8> v{0xa5};

  EXPECT_TRUE(Slop<9>::get_mask_op(v, Slop<9>::create_integer(0x0f))
                  .identical(Slop<9>::get_mask_op(v.raw(), Slop<9>::create_integer(0x0f))));
  EXPECT_TRUE(Slop<9>::get_mask_op(v, Slop_u<8>{0x0f}).identical(Slop<9>::get_mask_op(v.raw(), Slop<9>::create_integer(0x0f))));

  const Slop_u<8> arm0{11};
  const Slop_u<8> arm1{22};
  EXPECT_TRUE(Slop<9>::mux_op(Slop<2>::create_integer(0), arm0, arm1).identical(Slop<9>::create_integer(11)));
  EXPECT_TRUE(Slop<9>::mux_op(Slop<2>::create_integer(1), arm0, arm1).identical(Slop<9>::create_integer(22)));
  EXPECT_TRUE(Slop<9>::mux_op(Slop_u<1>{1}, arm0, arm1).identical(Slop<9>::create_integer(22)));
}

// ── zext_to_u: naming the value zext_to already produced ────────────────────

TEST(Slop_u_test, zext_to_u_matches_the_masking_ctor) {
  const Slop<65> dirty = Slop<65>::create_integer(-1);  // every stored bit set

  EXPECT_TRUE(dirty.zext_to_u<64>() == Slop_u<64>{dirty});
  EXPECT_TRUE(dirty.zext_to_u<8>() == Slop_u<8>{255});
  EXPECT_TRUE(dirty.zext_to_u<1>() == Slop_u<1>{1});
  EXPECT_TRUE(canonical_ok(dirty.zext_to_u<64>()));
  EXPECT_TRUE(canonical_ok(dirty.zext_to_u<8>()));
  EXPECT_TRUE(canonical_ok(dirty.zext_to_u<1>()));

  // Slop_u -> Slop_u: widening is a word copy, narrowing masks once. Both must
  // agree with the masking ctor, which is the only other way to build one.
  const Slop_u<8> u{0xa5};
  EXPECT_TRUE(u.zext_to_u<12>() == Slop_u<12>{u});
  EXPECT_TRUE(u.zext_to_u<8>() == u);
  EXPECT_TRUE(u.zext_to_u<4>() == Slop_u<4>{u});
  EXPECT_TRUE(u.zext_to_u<4>() == 5);
  EXPECT_TRUE(canonical_ok(u.zext_to_u<12>()));
  EXPECT_TRUE(canonical_ok(u.zext_to_u<4>()));

  // A NEGATIVE source is the case the ctor exists for: the mask turns it into
  // its two's-complement window, and zext_to_u must do exactly the same.
  EXPECT_TRUE(Slop<4>::create_integer(-1).zext_to_u<3>() == 7);
  EXPECT_TRUE(Slop<9>::create_integer(-56).zext_to_u<8>() == Slop_u<8>{Slop<9>::create_integer(-56)});
}

TEST(Slop_u_test, land_is_the_checked_landing) {
  // `land` requires the source to be at the DECLARED carrier width N+1; the
  // implicit ctor accepts any width and masks. Same value where both apply.
  const Slop<9> v = Slop<9>::create_integer(200);
  EXPECT_TRUE(Slop_u<8>::land(v) == Slop_u<8>{v});
  EXPECT_TRUE(Slop_u<8>::land(v) == 200);
  EXPECT_TRUE(canonical_ok(Slop_u<8>::land(v)));

  // A carrier holding more than its declared width still lands masked.
  const Slop<9> over = Slop<9>::add_op(Slop<9>::create_integer(200), Slop<9>::create_integer(100));
  EXPECT_TRUE(Slop_u<8>::land(over) == 44);
  EXPECT_TRUE(canonical_ok(Slop_u<8>::land(over)));
}

// ── the canonical-preserving statics ────────────────────────────────────────
//
// Each returns a Slop_u with NO mask, so each one's precondition is load
// bearing: the result must satisfy the invariant, and it must equal the lazy
// computation read back at the same width.

TEST(Slop_u_test, canonical_preserving_statics) {
  const Slop_u<8> a{0xa5};
  const Slop_u<8> b{0x3c};

  const auto and_r = Slop_u<8>::and_op(a, b);
  const auto or_r  = Slop_u<8>::or_op(a, b);
  const auto xor_r = Slop_u<8>::xor_op(a, b);
  EXPECT_TRUE(and_r == (0xa5 & 0x3c));
  EXPECT_TRUE(or_r == (0xa5 | 0x3c));
  EXPECT_TRUE(xor_r == (0xa5 ^ 0x3c));
  EXPECT_TRUE(canonical_ok(and_r));
  EXPECT_TRUE(canonical_ok(or_r));
  EXPECT_TRUE(canonical_ok(xor_r));

  // and_op needs only ONE canonical operand: a lazy (dirty, even negative)
  // second operand cannot lift a bit above the canonical one's width.
  const Slop<9> dirty = Slop<9>::create_integer(-1);
  const auto    mixed = Slop_u<8>::and_op(a, dirty);
  EXPECT_TRUE(mixed == 0xa5);
  EXPECT_TRUE(canonical_ok(mixed));

  // add_op keeps canonicality only with a carry bit of headroom: 255 + 255
  // needs 9 magnitude bits.
  const auto sum = Slop_u<9>::add_op(Slop_u<8>{255}, Slop_u<8>{255});
  EXPECT_TRUE(sum == 510);
  EXPECT_TRUE(canonical_ok(sum));

  // sra_op of a non-negative value is a logical shift and stays in range. The
  // width bound must hold at amount 0, which is why it is M <= N, not M <= N+1.
  EXPECT_TRUE(Slop_u<8>::sra_op(Slop_u<8>{0xa5}, 0) == 0xa5);
  EXPECT_TRUE(Slop_u<8>::sra_op(Slop_u<8>{0xa5}, 4) == 0x0a);
  EXPECT_TRUE(canonical_ok(Slop_u<8>::sra_op(Slop_u<8>{0xa5}, 0)));
  EXPECT_TRUE(canonical_ok(Slop_u<64>::sra_op(Slop_u<64>{Slop<65>::create_integer(-1)}, 1)));
}

// The static compares rebuild their 0/1 with create_integer rather than landing
// a Slop<cw> through the cross-width ctor. At cw == 1 the true value's only bit
// IS the sign slot, so the ctor would sign-extend it to -1 and deposit a value
// that breaks the invariant.
TEST(Slop_u_test, static_compares_are_canonical_at_every_width) {
  EXPECT_TRUE(Slop_u<1>::eq_op(Slop_u<8>{7}, Slop_u<8>{7}) == 1);
  EXPECT_TRUE(Slop_u<1>::eq_op(Slop_u<8>{7}, Slop_u<8>{8}) == 0);
  EXPECT_TRUE(Slop_u<1>::lt_op(Slop_u<8>{7}, Slop_u<8>{8}) == 1);
  EXPECT_TRUE(Slop_u<1>::gt_op(Slop_u<8>{9}, Slop_u<8>{8}) == 1);

  // cw == 1: both operands are one-bit carriers.
  const auto one_bit_eq = Slop_u<1>::eq_op(Slop<1>::create_integer(0), Slop<1>::create_integer(0));
  EXPECT_TRUE(one_bit_eq == 1);
  EXPECT_FALSE(one_bit_eq.is_negative());
  EXPECT_TRUE(canonical_ok(one_bit_eq));
  EXPECT_TRUE(Slop_u<1>::eq_op(Slop_u<1>{1}, Slop_u<1>{1}) == 1);
  EXPECT_TRUE(canonical_ok(Slop_u<1>::eq_op(Slop_u<1>{1}, Slop_u<1>{0})));

  // Wide operands: every word participates (the regression comparisons_read_
  // every_word guards for the member form).
  const Slop_u<127> big{Slop<128>::from_pyrope("0x7fff_ffff_ffff_ffff_ffff_ffff_ffff_ffff")};
  const Slop_u<127> big2{Slop<128>::from_pyrope("0x7fff_ffff_ffff_ffff_ffff_ffff_ffff_fffe")};
  EXPECT_TRUE(Slop_u<1>::eq_op(big, big) == 1);
  EXPECT_TRUE(Slop_u<1>::eq_op(big, big2) == 0);
  EXPECT_TRUE(Slop_u<1>::gt_op(big, big2) == 1);
  EXPECT_TRUE(canonical_ok(Slop_u<1>::eq_op(big, big2)));
}

// ── slop_update in both directions ──────────────────────────────────────────

TEST(Slop_u_test, slop_update_canonical_into_lazy) {
  Slop<9>         dst = Slop<9>::create_integer(0);
  const Slop_u<8> v{200};

  EXPECT_TRUE(slop_update(dst, v));            // changed
  EXPECT_TRUE(dst.identical(Slop<9>{v}));      // the free word-copy conversion
  EXPECT_TRUE(Slop<2>::eq_op(dst, Slop<9>::create_integer(200)).is_known_true());
  EXPECT_FALSE(slop_update(dst, v));           // idempotent: the change gate holds

  // The destination is WIDER than the source by construction (DstBits > SrcBits
  // is a static_assert), which is what keeps Slop<N>{Slop_u<M>} from
  // re-canonicalizing the sign: Slop<9>{Slop_u<8>{255}} is 255, not -1.
  Slop<9> all_ones = Slop<9>::create_integer(0);
  EXPECT_TRUE(slop_update(all_ones, Slop_u<8>{255}));
  EXPECT_FALSE(all_ones.is_negative());
  EXPECT_TRUE(Slop<2>::eq_op(all_ones, Slop<9>::create_integer(255)).is_known_true());
}
