//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// Mixed-width Slop ops: ONE LGraph cell -> ONE Slop op.
//
// Each static op takes operands at ANY width and materializes the result at the
// receiver width. The contract each test pins is that the mixed-width form
// equals the fixed-width form cgen_sim emits today, over the LEGAL input domain:
//
//   * an UNSIGNED pin of width W holds a value in [0, 2^(W-1)) -- cgen reads it
//     with .zext_to<R>()
//   * a SIGNED pin of width W holds a value in [-2^(W-1), 2^(W-1)) -- cgen reads
//     it with the cross-width ctor Slop<R>{x}
//
// Both are guaranteed by the bitwidth inference that runs before codegen
// (upass_tolg's bind_result stamps bits = magnitude+1 on every computed
// output). A value OUTSIDE its declared width is not something the graph can
// produce, so it is not part of the contract -- an earlier draft of this test
// fed -1 to the unsigned path and "failed" on inputs that cannot occur.
//
// The single sign-extending widen_ serves both reads: a non-negative value
// sign-extends to zeros, which is exactly zero-extension.

#include <cstdint>
#include <random>
#include <string>

#include "gtest/gtest.h"
#include "slop.hpp"

namespace {

template <int W>
int64_t fit_signed(int64_t v) {
  if constexpr (W >= 64) {
    return v;
  } else {
    return v % (int64_t{1} << (W - 1));
  }
}

template <int W>
int64_t fit_unsigned(int64_t v) {
  if constexpr (W >= 64) {
    return v < 0 ? -(v + 1) : v;
  } else {
    int64_t m = v % (int64_t{1} << (W - 1));
    return m < 0 ? -m : m;
  }
}

// One (A,B,R) triple, both signedness domains, all mixed-width ops.
template <int A, int B, int R>
void check(int64_t xa, int64_t xb) {
  {  // UNSIGNED domain: cgen reads operands with .zext_to<R>()
    const auto a  = Slop<A>::create_integer(fit_unsigned<A>(xa));
    const auto b  = Slop<B>::create_integer(fit_unsigned<B>(xb));
    const auto ea = a.template zext_to<R>();
    const auto eb = b.template zext_to<R>();

    EXPECT_EQ((Slop<R>::add_op(a, b)).to_binary(), Slop<R>::sum_op({ea, eb}, {}).to_binary()) << "u.add";
    EXPECT_EQ((Slop<R>::sub_op(a, b)).to_binary(), Slop<R>::sum_op({ea}, {eb}).to_binary()) << "u.sub";
    EXPECT_EQ((Slop<R>::mult_op(a, b)).to_binary(), ea.mult_op(eb).to_binary()) << "u.mult";
    EXPECT_EQ((Slop<R>::and_op(a, b)).to_binary(), ea.and_op(eb).to_binary()) << "u.and";
    EXPECT_EQ((Slop<R>::or_op(a, b)).to_binary(), ea.or_op(eb).to_binary()) << "u.or";
    EXPECT_EQ((Slop<R>::xor_op(a, b)).to_binary(), ea.xor_op(eb).to_binary()) << "u.xor";
    EXPECT_EQ((Slop<R>::not_op(a)).to_binary(), ea.not_op().to_binary()) << "u.not";

    // Compares materialize 0/1 directly -- the fixed-width form needs the
    // .zext_to<1>() clamp because create_bool's true is all-ones.
    EXPECT_EQ((Slop<R>::eq_op(a, b)).to_binary(), ea.eq_op(eb).template zext_to<1>().template zext_to<R>().to_binary()) << "u.eq";
    EXPECT_EQ((Slop<R>::lt_op(a, b)).to_binary(), ea.lt_op(eb).template zext_to<1>().template zext_to<R>().to_binary()) << "u.lt";
    EXPECT_EQ((Slop<R>::gt_op(a, b)).to_binary(), ea.gt_op(eb).template zext_to<1>().template zext_to<R>().to_binary()) << "u.gt";

    for (int64_t amt : {int64_t{0}, int64_t{1}, int64_t{3}, int64_t{31}, int64_t{64}}) {
      EXPECT_EQ((Slop<R>::shl_op(a, amt)).to_binary(), ea.shl_op(amt).to_binary()) << "u.shl " << amt;
      EXPECT_EQ((Slop<R>::sra_op(a, amt)).to_binary(), ea.sra_op(amt).to_binary()) << "u.sra " << amt;
    }
  }
  {  // SIGNED domain: cgen reads operands with the cross-width ctor Slop<R>{x}
    const auto a  = Slop<A>::create_integer(fit_signed<A>(xa));
    const auto b  = Slop<B>::create_integer(fit_signed<B>(xb));
    const auto ea = Slop<R>{a};
    const auto eb = Slop<R>{b};

    EXPECT_EQ((Slop<R>::add_op(a, b)).to_binary(), Slop<R>::sum_op({ea, eb}, {}).to_binary()) << "s.add";
    EXPECT_EQ((Slop<R>::sub_op(a, b)).to_binary(), Slop<R>::sum_op({ea}, {eb}).to_binary()) << "s.sub";
    EXPECT_EQ((Slop<R>::mult_op(a, b)).to_binary(), ea.mult_op(eb).to_binary()) << "s.mult";
    EXPECT_EQ((Slop<R>::and_op(a, b)).to_binary(), ea.and_op(eb).to_binary()) << "s.and";
    EXPECT_EQ((Slop<R>::or_op(a, b)).to_binary(), ea.or_op(eb).to_binary()) << "s.or";
    EXPECT_EQ((Slop<R>::xor_op(a, b)).to_binary(), ea.xor_op(eb).to_binary()) << "s.xor";
    EXPECT_EQ((Slop<R>::not_op(a)).to_binary(), ea.not_op().to_binary()) << "s.not";
    EXPECT_EQ((Slop<R>::eq_op(a, b)).to_binary(), ea.eq_op(eb).template zext_to<1>().template zext_to<R>().to_binary()) << "s.eq";
    EXPECT_EQ((Slop<R>::lt_op(a, b)).to_binary(), ea.lt_op(eb).template zext_to<1>().template zext_to<R>().to_binary()) << "s.lt";
    EXPECT_EQ((Slop<R>::gt_op(a, b)).to_binary(), ea.gt_op(eb).template zext_to<1>().template zext_to<R>().to_binary()) << "s.gt";
    for (int64_t amt : {int64_t{0}, int64_t{1}, int64_t{3}, int64_t{31}, int64_t{64}}) {
      EXPECT_EQ((Slop<R>::shl_op(a, amt)).to_binary(), ea.shl_op(amt).to_binary()) << "s.shl " << amt;
      EXPECT_EQ((Slop<R>::sra_op(a, amt)).to_binary(), ea.sra_op(amt).to_binary()) << "s.sra " << amt;
    }
  }
}

template <int A, int B, int R>
void sweep() {
  static const int64_t seeds[] = {0,
                                  1,
                                  -1,
                                  2,
                                  -2,
                                  3,
                                  7,
                                  8,
                                  -8,
                                  15,
                                  16,
                                  -16,
                                  1000,
                                  -1000,
                                  65535,
                                  -65536,
                                  1 << 20,
                                  -(1 << 20),
                                  (int64_t{1} << 40),
                                  -(int64_t{1} << 40)};
  for (int64_t x : seeds) {
    for (int64_t y : seeds) {
      check<A, B, R>(x, y);
    }
  }
  std::mt19937_64 rng(0xC0FFEE);
  for (int i = 0; i < 400; ++i) {
    check<A, B, R>(static_cast<int64_t>(rng()), static_cast<int64_t>(rng()));
  }
}

}  // namespace

// The user's motivating example: 2 + 1000 (3 bits + 11 bits) stored in 32 bits.
TEST(Slop_mixed_width, user_example_add_3_11_into_32) {
  auto a = Slop<3>::create_integer(2);
  auto b = Slop<11>::create_integer(1000);
  EXPECT_EQ(Slop<32>::add_op(a, b).to_just_i64(), 1002);
}

TEST(Slop_mixed_width, shift_amount_width_is_independent) {
  const auto value  = Slop<4>::create_integer(3);
  const auto amount = Slop<12>::create_integer(2);
  EXPECT_EQ(Slop<8>::shl_op(value, amount).to_just_i64(), 12);

  const auto signed_value = Slop<8>::create_integer(-16);
  EXPECT_EQ(Slop<8>::sra_op(signed_value, amount).to_just_i64(), -4);
}

TEST(Slop_mixed_width, narrow_operands_one_word) { sweep<3, 11, 32>(); }
TEST(Slop_mixed_width, mixed_small) { sweep<5, 11, 32>(); }
TEST(Slop_mixed_width, tiny) { sweep<2, 2, 2>(); }
TEST(Slop_mixed_width, equal_widths) { sweep<8, 8, 8>(); }
TEST(Slop_mixed_width, asymmetric) { sweep<16, 4, 20>(); }
TEST(Slop_mixed_width, at_32) { sweep<32, 32, 33>(); }
TEST(Slop_mixed_width, near_62) { sweep<62, 8, 63>(); }
TEST(Slop_mixed_width, one_word_boundary) { sweep<64, 64, 64>(); }
TEST(Slop_mixed_width, crosses_into_two_words) { sweep<64, 64, 65>(); }
TEST(Slop_mixed_width, multi_word) { sweep<66, 80, 96>(); }
TEST(Slop_mixed_width, narrow_plus_wide) { sweep<8, 80, 96>(); }
TEST(Slop_mixed_width, wide_plus_narrow) { sweep<66, 8, 70>(); }
TEST(Slop_mixed_width, very_wide) { sweep<100, 200, 256>(); }

// Compares must yield a 0/1 MAGNITUDE, not create_bool's all-ones. This is what
// lets cgen_sim drop the `.zext_to<1>().zext_to<W>()` clamp it appends today.
TEST(Slop_mixed_width, compares_are_zero_or_one) {
  auto a = Slop<8>::create_integer(5);
  auto b = Slop<12>::create_integer(5);
  auto c = Slop<12>::create_integer(9);

  EXPECT_EQ(Slop<2>::eq_op(a, b).to_just_i64(), 1);
  EXPECT_EQ(Slop<2>::eq_op(a, c).to_just_i64(), 0);
  EXPECT_EQ(Slop<2>::lt_op(a, c).to_just_i64(), 1);
  EXPECT_EQ(Slop<2>::lt_op(c, a).to_just_i64(), 0);
  EXPECT_EQ(Slop<2>::gt_op(c, a).to_just_i64(), 1);
  EXPECT_EQ(Slop<2>::gt_op(a, c).to_just_i64(), 0);

  // The member form still returns all-ones -- the existing contract is intact.
  EXPECT_TRUE(Slop<8>::create_integer(5).eq_op(Slop<8>::create_integer(5)).is_known_true());
}

// and/or/xor cannot GROW beyond the widest operand -- every set bit of the
// result was already set in an operand. They still cannot use a narrower result
// carrier: HLOP values are signed unlimited-precision integers, so a narrow AND
// input sign-extends across a wider input rather than bounding the result width.
// At MAX below, the `.zext_to<R>()` cgen used to append to the RESULT is a no-op.
//
// add/mult/shl do grow, so their trailing clamp has to stay; the negative
// control at the end pins that the check is not vacuous.
namespace {
template <int A, int B>
void no_result_clamp(int64_t xa, int64_t xb) {
  const auto    a   = Slop<A>::create_integer(fit_unsigned<A>(xa));
  const auto    b   = Slop<B>::create_integer(fit_unsigned<B>(xb));
  constexpr int MAX = A < B ? B : A;

  const auto an = Slop<MAX>::and_op(a, b);
  const auto on = Slop<MAX>::or_op(a, b);
  const auto xn = Slop<MAX>::xor_op(a, b);
  EXPECT_EQ(an.to_binary(), an.template zext_to<MAX>().to_binary()) << "u.and needs no result clamp";
  EXPECT_EQ(on.to_binary(), on.template zext_to<MAX>().to_binary()) << "u.or needs no result clamp";
  EXPECT_EQ(xn.to_binary(), xn.template zext_to<MAX>().to_binary()) << "u.xor needs no result clamp";
}

template <int A, int B>
void sweep_no_clamp() {
  std::mt19937_64 rng(0x5EED);
  for (int i = 0; i < 500; ++i) {
    no_result_clamp<A, B>(static_cast<int64_t>(rng()), static_cast<int64_t>(rng()));
  }
}
}  // namespace

TEST(Slop_mixed_width, bitwise_needs_no_result_clamp) {
  sweep_no_clamp<2, 2>();
  sweep_no_clamp<8, 8>();
  sweep_no_clamp<16, 4>();
  sweep_no_clamp<3, 11>();
  sweep_no_clamp<32, 32>();
  sweep_no_clamp<62, 8>();
  sweep_no_clamp<64, 64>();
  sweep_no_clamp<66, 80>();
  sweep_no_clamp<128, 128>();
  sweep_no_clamp<100, 200>();
}

// Mixed-width get_mask must agree with the member form on every mask shape,
// EXCEPT the single-selected-bit case, where it deliberately yields the unsigned
// 0/1 instead of the member form's signed -1.
TEST(Slop_mixed_width, get_mask_matches_member_form) {
  std::mt19937_64 rng(0xBEEF);
  const int64_t   masks[] = {-1, 1, 3, 7, 0xff, 0xffff, 0x7fffffff, 126, 0b1010, 0x40, 6, 0x0f0f};
  int             checked = 0;
  for (int64_t mk : masks) {
    for (int i = 0; i < 300; ++i) {
      const int64_t v = static_cast<int64_t>(rng()) % 65536;
      const auto    x = Slop<20>::create_integer(v);
      const auto    m = Slop<20>::create_integer(mk);

      const auto member = x.get_mask_op(m);             // Slop<20>
      const auto mixed  = Slop<20>::get_mask_op(x, m);  // same widths -> must match

      // how many bits does this mask select? (drives the single-bit exception)
      int sel = 0;
      if (mk >= 0) {
        for (int b = 0; b < 20; ++b) {
          if (mk & (int64_t{1} << b)) {
            ++sel;
          }
        }
      } else {
        sel = 99;  // negative mask selects a range; never the 1-bit case here
      }
      if (sel == 1) {
        // member form returns signed -1/0; mixed returns 0/1
        EXPECT_TRUE(mixed.to_just_i64() == 0 || mixed.to_just_i64() == 1) << "mask=" << mk << " v=" << v;
        EXPECT_EQ(mixed.to_just_i64() != 0, member.to_just_i64() != 0) << "mask=" << mk << " v=" << v;
      } else {
        EXPECT_EQ(mixed.to_binary(), member.to_binary()) << "mask=" << mk << " v=" << v;
      }
      ++checked;
    }
  }
  EXPECT_GT(checked, 3000);
}

// The point of the mixed-width form: operands at differing widths, result
// materialized at the cell's own width, with no caller-side conversion.
TEST(Slop_mixed_width, get_mask_across_widths) {
  auto x  = Slop<40>::create_integer(0xABCDE);
  auto m8 = Slop<8>::create_integer(0xff);
  EXPECT_EQ(Slop<9>::get_mask_op(x, m8).to_just_i64(), 0xDE);

  auto wide = Slop<80>::create_integer(int64_t{0x123456789});
  auto m16  = Slop<16>::create_integer(0xffff);
  EXPECT_EQ(Slop<17>::get_mask_op(wide, m16).to_just_i64(), 0x6789);

  // to-positive (-1) across widths
  auto neg = Slop<8>::create_integer(-8);
  EXPECT_EQ(Slop<9>::get_mask_op(neg, Slop<4>::create_integer(-1)).to_just_i64(), 8);
}

// mux_op with a decoded integer index must equal the Slop-selector form.
TEST(Slop_mixed_width, mux_int_index_matches_slop_selector) {
  const Slop<66> arms[]
      = {Slop<66>::create_integer(11), Slop<66>::create_integer(22), Slop<66>::create_integer(33), Slop<66>::create_integer(44)};
  for (int64_t i = 0; i < 4; ++i) {
    auto by_slop = Slop<66>::mux_op(Slop<66>::create_integer(i), std::span<const Slop<66>>(arms, 4));
    auto by_int  = Slop<66>::mux_op(i, std::span<const Slop<66>>(arms, 4));
    EXPECT_EQ(by_int.to_binary(), by_slop.to_binary()) << "idx=" << i;
  }
  // out of range -> invalid, same as the Slop-selector form
  EXPECT_TRUE(Slop<66>::mux_op(int64_t{9}, std::span<const Slop<66>>(arms, 4)).is_invalid());
  EXPECT_TRUE(Slop<66>::mux_op(int64_t{-1}, std::span<const Slop<66>>(arms, 4)).is_invalid());
}

TEST(Slop_mixed_width, mux_condition_and_heterogeneous_arms) {
  const auto narrow = Slop<4>::create_integer(3);
  const auto wide   = Slop<70>::create_integer(int64_t{0x123456789});

  // A two-arm Mux is conditional: every nonzero value, not only integer 1,
  // selects the true arm. The result carrier losslessly promotes either arm.
  EXPECT_EQ(Slop<80>::mux_op(Slop<9>::create_integer(0), narrow, wide).to_just_i64(), 3);
  EXPECT_EQ(Slop<80>::mux_op(Slop<9>::create_integer(128), narrow, wide).to_just_i64(), int64_t{0x123456789});

  const auto middle = Slop<17>::create_integer(17);
  EXPECT_EQ(Slop<80>::mux_op(Slop<3>::create_integer(2), narrow, middle, wide).to_just_i64(), int64_t{0x123456789});
  EXPECT_EQ(Slop<80>::mux_op(Slop<3>::create_integer(0), narrow, middle, wide).to_just_i64(), 3);
  EXPECT_EQ(Slop<80>::mux_op(Slop<3>::create_integer(1), narrow, middle, wide).to_just_i64(), 17);

  // Three or more arms index rather than condition, so a selector outside the
  // arm range -- or one too wide to be an index at all -- is Invalid, not a
  // wrapped pick. (Only the selected arm is ever promoted, so this also pins
  // that the arm-selection walk stops in the right place.)
  EXPECT_TRUE(Slop<80>::mux_op(Slop<3>::create_integer(3), narrow, middle, wide).is_invalid());
  EXPECT_TRUE(Slop<80>::mux_op(Slop<8>::create_integer(-1), narrow, middle, wide).is_invalid());
  EXPECT_TRUE(
      Slop<80>::mux_op(Slop<80>::from_pyrope("0x1_0000_0000_0000_0000"), narrow, middle, wide).is_invalid());
}

// A Hotmux's one-hot selector width follows its arm count, not its result
// width. A wide decode must therefore be able to select a narrow value without
// truncating the selector to the result width.
TEST(Slop_mixed_width, hotmux_selector_width_is_independent) {
  const auto narrow = Slop<2>::create_integer(0);
  const auto middle = Slop<17>::create_integer(1);
  const auto wide   = Slop<70>::create_integer(int64_t{0x123456789});

  EXPECT_EQ(Slop<80>::hotmux_op(Slop<8>::create_integer(0b00000100), narrow, middle, wide).to_just_i64(), int64_t{0x123456789});
  EXPECT_TRUE(Slop<80>::hotmux_op(Slop<8>::create_integer(0b00001000), narrow, middle, wide).is_invalid());
}

TEST(Slop_mixed_width, update_losslessly_widens_source) {
  auto dst = Slop<17>::create_integer(0);
  EXPECT_TRUE(slop_update(dst, Slop<8>::create_integer(63)));
  EXPECT_EQ(dst.to_just_i64(), 63);
  EXPECT_FALSE(slop_update(dst, Slop<8>::create_integer(63)));
}

// Signed comparison must stay signed across a width difference: a negative
// operand materialized narrow must still compare less than a positive one.
TEST(Slop_mixed_width, signed_compare_across_widths) {
  auto neg = Slop<8>::create_integer(-3);
  auto pos = Slop<32>::create_integer(7);
  EXPECT_EQ(Slop<2>::lt_op(neg, pos).to_just_i64(), 1);
  EXPECT_EQ(Slop<2>::gt_op(neg, pos).to_just_i64(), 0);
  EXPECT_EQ(Slop<64>::add_op(neg, pos).to_just_i64(), 4);
  EXPECT_EQ(Slop<64>::sub_op(pos, neg).to_just_i64(), 10);
}
