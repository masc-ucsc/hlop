//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "slop.hpp"

#include <string>
#include <vector>

#include "gtest/gtest.h"

class Slop_test : public ::testing::Test {};

// Use Slop<128> for most tests — enough for typical values
using S = Slop<128>;

// =========================================================================
// Factory / parsing tests
// =========================================================================
TEST_F(Slop_test, create_integer) {
  auto a = S::create_integer(42);
  EXPECT_EQ(a.to_just_i64(), 42);
  EXPECT_TRUE(a.is_just_i64());

  auto b = S::create_integer(-7);
  EXPECT_EQ(b.to_just_i64(), -7);
  EXPECT_TRUE(b.is_negative());
}

TEST_F(Slop_test, create_bool) {
  auto t = S::create_bool(true);
  EXPECT_TRUE(t.is_known_true());

  auto f = S::create_bool(false);
  EXPECT_TRUE(f.is_known_false());
}

// from_pyrope is constexpr — these static_asserts confirm the parser folds
// at compile time for typical literal forms (decimal, hex, binary, signed).
TEST_F(Slop_test, from_pyrope_constexpr) {
  static constexpr auto three = Slop<8>::from_pyrope("3");
  static_assert(three.to_just_i64() == 3);
  static constexpr auto neg = Slop<8>::from_pyrope("-7");
  static_assert(neg.to_just_i64() == -7);
  static constexpr auto hex = Slop<32>::from_pyrope("0x1f");
  static_assert(hex.to_just_i64() == 0x1f);
  static constexpr auto bin = Slop<8>::from_pyrope("0ub1010");
  static_assert(bin.to_just_i64() == 0b1010);
}

TEST_F(Slop_test, from_pyrope_decimal) {
  auto a = S::from_pyrope("123");
  EXPECT_EQ(a.to_just_i64(), 123);

  auto b = S::from_pyrope("-456");
  EXPECT_EQ(b.to_just_i64(), -456);
}

TEST_F(Slop_test, from_pyrope_hex) {
  auto a = S::from_pyrope("0xdeadbeef");
  EXPECT_EQ(a.to_just_i64(), 0xdeadbeef);

  auto b = S::from_pyrope("-0xff");
  EXPECT_EQ(b.to_just_i64(), -0xff);
}

TEST_F(Slop_test, from_pyrope_binary) {
  auto a = S::from_pyrope("0ub1010");
  EXPECT_EQ(a.to_just_i64(), 10);

  auto sb = S::from_pyrope("0sb1010");
  EXPECT_EQ(sb.to_just_i64(), -6);
}

TEST_F(Slop_test, from_pyrope_string) {
  auto s = S::from_pyrope("'hello'");
  EXPECT_TRUE(s.is_string());
  EXPECT_EQ(s.to_string(), "hello");
}

// =========================================================================
// Arithmetic tests
// =========================================================================
TEST_F(Slop_test, add_op) {
  auto a = S::from_pyrope("100");
  auto b = S::from_pyrope("200");
  auto c = a.add_op(b);
  EXPECT_EQ(c.to_just_i64(), 300);
}

TEST_F(Slop_test, add_op_negative) {
  auto a = S::from_pyrope("10");
  auto b = S::from_pyrope("-20");
  auto c = a.add_op(b);
  EXPECT_EQ(c.to_just_i64(), -10);
}

TEST_F(Slop_test, sub_op) {
  auto a = S::from_pyrope("100");
  auto b = S::from_pyrope("30");
  auto c = a.sub_op(b);
  EXPECT_EQ(c.to_just_i64(), 70);
}

TEST_F(Slop_test, sum_op) {
  std::vector<S> a{S::from_pyrope("100"), S::from_pyrope("30")};
  std::vector<S> b{S::from_pyrope("7"), S::from_pyrope("3")};
  auto           c = S::sum_op(a, b);
  EXPECT_EQ(c.to_just_i64(), 120);

  EXPECT_EQ(S::sum_op({S::create_integer(1), S::create_integer(2)}, {}).to_just_i64(), 3);
}

TEST_F(Slop_test, mult_op) {
  auto a = S::from_pyrope("7");
  auto b = S::from_pyrope("6");
  auto c = a.mult_op(b);
  EXPECT_EQ(c.to_just_i64(), 42);
}

TEST_F(Slop_test, mult_op_negative) {
  auto a = S::from_pyrope("-3");
  auto b = S::from_pyrope("4");
  auto c = a.mult_op(b);
  EXPECT_EQ(c.to_just_i64(), -12);
}

TEST_F(Slop_test, div_op) {
  auto a = S::from_pyrope("42");
  auto b = S::from_pyrope("6");
  auto c = a.div_op(b);
  EXPECT_EQ(c.to_just_i64(), 7);
}

TEST_F(Slop_test, neg_op) {
  auto a = S::from_pyrope("42");
  auto b = a.neg_op();
  EXPECT_EQ(b.to_just_i64(), -42);
}

// =========================================================================
// Bitwise tests
// =========================================================================
TEST_F(Slop_test, or_op) {
  auto a = S::from_pyrope("0ub1010");
  auto b = S::from_pyrope("0ub0101");
  auto c = a.or_op(b);
  EXPECT_EQ(c.to_just_i64(), 0xF);
}

TEST_F(Slop_test, and_op) {
  auto a = S::from_pyrope("0ub1110");
  auto b = S::from_pyrope("0ub1011");
  auto c = a.and_op(b);
  EXPECT_EQ(c.to_just_i64(), 0b1010);
}

TEST_F(Slop_test, xor_op) {
  auto a = S::from_pyrope("0ub1100");
  auto b = S::from_pyrope("0ub1010");
  auto c = a.xor_op(b);
  EXPECT_EQ(c.to_just_i64(), 0b0110);
}

TEST_F(Slop_test, not_op) {
  auto a = S(0);
  auto b = a.not_op();
  EXPECT_EQ(b.to_just_i64(), -1);

  auto c = S(5);
  auto d = c.not_op();
  EXPECT_EQ(d.to_just_i64(), -6);
}

// =========================================================================
// Shift tests
// =========================================================================
TEST_F(Slop_test, shl_op) {
  auto a = S::from_pyrope("1");
  auto b = a.shl_op(4);
  EXPECT_EQ(b.to_just_i64(), 16);
}

TEST_F(Slop_test, sra_op) {
  auto a = S::from_pyrope("0xff");
  auto b = a.sra_op(4);
  EXPECT_EQ(b.to_just_i64(), 0xf);
}

// =========================================================================
// Comparison tests
// =========================================================================
TEST_F(Slop_test, comparisons) {
  auto a = S::from_pyrope("10");
  auto b = S::from_pyrope("20");

  EXPECT_TRUE(a.lt_op(b).is_known_true());
  EXPECT_TRUE(a.le_op(b).is_known_true());
  EXPECT_TRUE(a.gt_op(b).is_known_false());
  EXPECT_TRUE(a.ge_op(b).is_known_false());
  EXPECT_FALSE(a.is_known_eq(b));
  EXPECT_FALSE(a.same_repr(b));
}

TEST_F(Slop_test, eq_op) {
  auto a = S::from_pyrope("42");
  auto b = S::from_pyrope("42");
  auto c = a.eq_op(b);
  EXPECT_TRUE(c.is_known_true());

  auto d = S::from_pyrope("43");
  auto e = a.eq_op(d);
  EXPECT_TRUE(e.is_known_false());
}

// =========================================================================
// Query tests
// =========================================================================
TEST_F(Slop_test, get_bits) {
  EXPECT_EQ(S(0).get_bits(), 0);
  EXPECT_EQ(S(1).get_bits(), 2);
  EXPECT_EQ(S(-1).get_bits(), 1);
  EXPECT_EQ(S(7).get_bits(), 4);
  EXPECT_EQ(S(-8).get_bits(), 4);
}

TEST_F(Slop_test, is_mask) {
  EXPECT_TRUE(S::from_pyrope("0xF").is_mask());
  EXPECT_TRUE(S::from_pyrope("0xFF").is_mask());
  EXPECT_TRUE(S::from_pyrope("1").is_mask());
  EXPECT_FALSE(S::from_pyrope("0").is_mask());
  EXPECT_FALSE(S::from_pyrope("6").is_mask());
}

TEST_F(Slop_test, is_power2) {
  EXPECT_TRUE(S::from_pyrope("1").is_power2());
  EXPECT_TRUE(S::from_pyrope("0x100").is_power2());
  EXPECT_FALSE(S::from_pyrope("3").is_power2());
  EXPECT_FALSE(S::from_pyrope("0").is_power2());
}

TEST_F(Slop_test, popcount_test) {
  EXPECT_EQ(S::from_pyrope("0ub1010").popcount(), 2);
  EXPECT_EQ(S::from_pyrope("0xFF").popcount(), 8);
}

TEST_F(Slop_test, popcount_op_test) {
  // Slop has no unknowns, so popcount_op is always the exact count as a value.
  EXPECT_EQ(S::from_pyrope("0ub1010").popcount_op().to_just_i64(), 2);
  EXPECT_EQ(S::from_pyrope("0xFF").popcount_op().to_just_i64(), 8);
  EXPECT_EQ(S::from_pyrope("0").popcount_op().to_just_i64(), 0);
}

TEST_F(Slop_test, popcount_op_negative_test) {
  // Unlike Dlop, Slop has a fixed width and a concrete bit pattern even when
  // negative, so popcount_op returns the exact count of set bits across the
  // full width. S is Slop<128>, so -1 (all 128 bits set) → 128.
  EXPECT_TRUE(S::from_pyrope("-1").is_negative());
  EXPECT_EQ(S::from_pyrope("-1").popcount_op().to_just_i64(), 128);
  // -2 = ...11111110 → all bits set except bit 0 → 127.
  EXPECT_EQ(S::from_pyrope("-2").popcount_op().to_just_i64(), 127);
}

// =========================================================================
// Small Slop sizes
// =========================================================================
TEST_F(Slop_test, slop_8bit) {
  auto a = Slop<8>::from_pyrope("5");
  auto b = Slop<8>::from_pyrope("3");
  auto c = a.add_op(b);
  EXPECT_EQ(c.to_just_i64(), 8);
}

TEST_F(Slop_test, slop_1bit) {
  auto a = Slop<1>(0);
  auto b = Slop<1>(1);
  EXPECT_EQ(a.to_just_i64(), 0);
  EXPECT_EQ(b.to_just_i64(), 1);
}

// =========================================================================
// Mux / Hotmux / LUT tests (computing cells mirrored from livehd cell.*)
// =========================================================================
TEST_F(Slop_test, mux_op) {
  using S8 = Slop<8>;
  std::vector<S8> vals{S8::from_pyrope("0x11"), S8::from_pyrope("0x22"), S8::from_pyrope("0x33")};

  EXPECT_EQ(S8::mux_op(S8::create_integer(0), vals).to_just_i64(), 0x11);
  EXPECT_EQ(S8::mux_op(S8::create_integer(2), vals).to_just_i64(), 0x33);
  // Brace-list overload.
  EXPECT_EQ(S8::mux_op(S8::create_integer(1), {S8::create_integer(5), S8::create_integer(9)}).to_just_i64(), 9);
}

TEST_F(Slop_test, hotmux_op) {
  using S8 = Slop<8>;
  std::vector<S8> vals{S8::from_pyrope("0x11"), S8::from_pyrope("0x22"), S8::from_pyrope("0x33")};

  EXPECT_EQ(S8::hotmux_op(S8::create_integer(0b001), vals).to_just_i64(), 0x11);
  EXPECT_EQ(S8::hotmux_op(S8::create_integer(0b010), vals).to_just_i64(), 0x22);
  EXPECT_EQ(S8::hotmux_op(S8::create_integer(0b100), vals).to_just_i64(), 0x33);
}

TEST_F(Slop_test, lut_op) {
  using S8   = Slop<8>;
  auto table = S8::from_pyrope("0ub1010");  // bit0=0 bit1=1 bit2=0 bit3=1

  EXPECT_TRUE(S8::lut_op(table, S8::create_integer(0)).is_known_false());
  EXPECT_TRUE(S8::lut_op(table, S8::create_integer(1)).is_known_true());
  EXPECT_TRUE(S8::lut_op(table, S8::create_integer(2)).is_known_false());
  EXPECT_TRUE(S8::lut_op(table, S8::create_integer(3)).is_known_true());
}

// get_mask_op(mask) — multi-bit mask packs the selected bits LSB-first.
TEST_F(Slop_test, get_mask_op_multibit_packed) {
  using S32 = Slop<32>;
  auto v    = S32::create_integer(0xABCD);
  // 0xff selects the low byte -> 0xCD.
  EXPECT_EQ(v.get_mask_op(S32::create_integer(0xff)).to_just_i64(), 0xCD);
  // 0xf00 selects bits 8..11 -> 0xB, packed down to the low nibble.
  EXPECT_EQ(v.get_mask_op(S32::create_integer(0xf00)).to_just_i64(), 0xB);
}

// get_mask_op(mask) — a NEGATIVE source is sign-extended past its minimal
// width before extraction (the result is unsigned). Regression: -1 has a
// minimal width of one sign bit, so capping extraction at src_bits wrongly
// returned 1 instead of 0xff for get_mask(-1, 0xff). Unlike Dlop, Slop is
// fixed width and stores the full sign extension, so bit_test handles it.
TEST_F(Slop_test, get_mask_op_negative_source_sign_extends) {
  using S32 = Slop<32>;
  auto neg1 = S32::create_integer(-1);
  EXPECT_EQ(neg1.get_mask_op(S32::create_integer(0xff)).to_just_i64(), 0xff);    // low 8 bits of ...1111
  EXPECT_EQ(neg1.get_mask_op(S32::create_integer(0xf)).to_just_i64(), 0xf);      // low 4 bits
  EXPECT_EQ(neg1.get_mask_op(S32::create_integer(0xffff)).to_just_i64(), 0xffff);

  // -2 == ...11111110 -> low byte is 0xfe.
  auto neg2 = S32::create_integer(-2);
  EXPECT_EQ(neg2.get_mask_op(S32::create_integer(0xff)).to_just_i64(), 0xfe);

  // Positive sources are unaffected (sign bit is 0 above their width).
  auto p511 = S32::create_integer(0x1ff);
  EXPECT_EQ(p511.get_mask_op(S32::create_integer(0xff)).to_just_i64(), 0xff);
}

// get_mask_op(mask) — single-bit mask returns the signed 1-bit integer
// (-1 if the bit is set, 0 if clear), not the unsigned 1/0.
TEST_F(Slop_test, get_mask_op_single_bit) {
  using S32 = Slop<32>;
  auto v    = S32::create_integer(0b1010);
  EXPECT_EQ(v.get_mask_op(S32::create_integer(0b0010)).to_just_i64(), -1);  // bit set
  EXPECT_EQ(v.get_mask_op(S32::create_integer(0b0001)).to_just_i64(), 0);   // bit clear
}

// Pyrope `nil` / `null` literals parse to Type::Nil (parity with Dlop), while
// the quoted form is the string "nil".
TEST_F(Slop_test, nil_null_literals) {
  EXPECT_TRUE(S::from_pyrope("nil").is_nil());
  EXPECT_TRUE(S::from_pyrope("null").is_nil());
  EXPECT_TRUE(S::from_pyrope("NULL").is_nil());
  EXPECT_TRUE(S::from_pyrope("Nil").is_nil());
  EXPECT_FALSE(S::from_pyrope("'nil'").is_nil());  // quoted -> string
}

// The signed/unsigned binary prefixes are the full 0sb / 0ub. A bare/short 0s
// or 0u must error cleanly, not read the base char past the string_view.
TEST_F(Slop_test, from_pyrope_short_sign_prefix_rejected) {
  EXPECT_THROW(S::from_pyrope(std::string_view{"0sb", 2}), std::runtime_error);  // "0s"
  EXPECT_THROW(S::from_pyrope(std::string_view{"0ub", 2}), std::runtime_error);  // "0u"
  EXPECT_THROW(S::from_pyrope("0s"), std::runtime_error);
  EXPECT_THROW(S::from_pyrope("0u"), std::runtime_error);
  EXPECT_EQ(S::from_pyrope("0sb1010").to_just_i64(), -6);
  EXPECT_EQ(S::from_pyrope("0ub1010").to_just_i64(), 10);
}

// is_mask must accept INT64_MAX (the 2^63-1 all-ones run) in the scalar
// (n_words==1) path without signed overflow on base_[0]+1.
TEST_F(Slop_test, is_mask_scalar_boundary) {
  using S64 = Slop<64>;
  EXPECT_TRUE(S64::create_integer(0x7FFFFFFFFFFFFFFFLL).is_mask());  // 2^63-1
  EXPECT_TRUE(S64::create_integer(0xFF).is_mask());
  EXPECT_FALSE(S64::create_integer(0xF0).is_mask());  // not anchored at bit 0
  EXPECT_FALSE(S64::create_integer(0).is_mask());
}
