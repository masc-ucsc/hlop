//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "slop.hpp"

#include <string>

#include "gtest/gtest.h"

class Slop_test : public ::testing::Test {};

// Use Slop<128> for most tests — enough for typical values
using S = Slop<128>;

// =========================================================================
// Factory / parsing tests
// =========================================================================
TEST_F(Slop_test, create_integer) {
  auto a = S::create_integer(42);
  EXPECT_EQ(a.to_i(), 42);
  EXPECT_TRUE(a.is_i());

  auto b = S::create_integer(-7);
  EXPECT_EQ(b.to_i(), -7);
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
  static_assert(three.to_i() == 3);
  static constexpr auto neg = Slop<8>::from_pyrope("-7");
  static_assert(neg.to_i() == -7);
  static constexpr auto hex = Slop<32>::from_pyrope("0x1f");
  static_assert(hex.to_i() == 0x1f);
  static constexpr auto bin = Slop<8>::from_pyrope("0b1010");
  static_assert(bin.to_i() == 0b1010);
}

TEST_F(Slop_test, from_pyrope_decimal) {
  auto a = S::from_pyrope("123");
  EXPECT_EQ(a.to_i(), 123);

  auto b = S::from_pyrope("-456");
  EXPECT_EQ(b.to_i(), -456);
}

TEST_F(Slop_test, from_pyrope_hex) {
  auto a = S::from_pyrope("0xdeadbeef");
  EXPECT_EQ(a.to_i(), 0xdeadbeef);

  auto b = S::from_pyrope("-0xff");
  EXPECT_EQ(b.to_i(), -0xff);
}

TEST_F(Slop_test, from_pyrope_binary) {
  auto a = S::from_pyrope("0b1010");
  EXPECT_EQ(a.to_i(), 10);

  auto sb = S::from_pyrope("0sb1010");
  EXPECT_EQ(sb.to_i(), -6);
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
  EXPECT_EQ(c.to_i(), 300);
}

TEST_F(Slop_test, add_op_negative) {
  auto a = S::from_pyrope("10");
  auto b = S::from_pyrope("-20");
  auto c = a.add_op(b);
  EXPECT_EQ(c.to_i(), -10);
}

TEST_F(Slop_test, sub_op) {
  auto a = S::from_pyrope("100");
  auto b = S::from_pyrope("30");
  auto c = a.sub_op(b);
  EXPECT_EQ(c.to_i(), 70);
}

TEST_F(Slop_test, mult_op) {
  auto a = S::from_pyrope("7");
  auto b = S::from_pyrope("6");
  auto c = a.mult_op(b);
  EXPECT_EQ(c.to_i(), 42);
}

TEST_F(Slop_test, mult_op_negative) {
  auto a = S::from_pyrope("-3");
  auto b = S::from_pyrope("4");
  auto c = a.mult_op(b);
  EXPECT_EQ(c.to_i(), -12);
}

TEST_F(Slop_test, div_op) {
  auto a = S::from_pyrope("42");
  auto b = S::from_pyrope("6");
  auto c = a.div_op(b);
  EXPECT_EQ(c.to_i(), 7);
}

TEST_F(Slop_test, neg_op) {
  auto a = S::from_pyrope("42");
  auto b = a.neg_op();
  EXPECT_EQ(b.to_i(), -42);
}

// =========================================================================
// Bitwise tests
// =========================================================================
TEST_F(Slop_test, or_op) {
  auto a = S::from_pyrope("0b1010");
  auto b = S::from_pyrope("0b0101");
  auto c = a.or_op(b);
  EXPECT_EQ(c.to_i(), 0xF);
}

TEST_F(Slop_test, and_op) {
  auto a = S::from_pyrope("0b1110");
  auto b = S::from_pyrope("0b1011");
  auto c = a.and_op(b);
  EXPECT_EQ(c.to_i(), 0b1010);
}

TEST_F(Slop_test, xor_op) {
  auto a = S::from_pyrope("0b1100");
  auto b = S::from_pyrope("0b1010");
  auto c = a.xor_op(b);
  EXPECT_EQ(c.to_i(), 0b0110);
}

TEST_F(Slop_test, not_op) {
  auto a = S(0);
  auto b = a.not_op();
  EXPECT_EQ(b.to_i(), -1);

  auto c = S(5);
  auto d = c.not_op();
  EXPECT_EQ(d.to_i(), -6);
}

// =========================================================================
// Shift tests
// =========================================================================
TEST_F(Slop_test, lsh_op) {
  auto a = S::from_pyrope("1");
  auto b = a.lsh_op(4);
  EXPECT_EQ(b.to_i(), 16);
}

TEST_F(Slop_test, rsh_op) {
  auto a = S::from_pyrope("0xff");
  auto b = a.rsh_op(4);
  EXPECT_EQ(b.to_i(), 0xf);
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
  EXPECT_EQ(S::from_pyrope("0b1010").popcount(), 2);
  EXPECT_EQ(S::from_pyrope("0xFF").popcount(), 8);
}

// =========================================================================
// Small Slop sizes
// =========================================================================
TEST_F(Slop_test, slop_8bit) {
  auto a = Slop<8>::from_pyrope("5");
  auto b = Slop<8>::from_pyrope("3");
  auto c = a.add_op(b);
  EXPECT_EQ(c.to_i(), 8);
}

TEST_F(Slop_test, slop_1bit) {
  auto a = Slop<1>(0);
  auto b = Slop<1>(1);
  EXPECT_EQ(a.to_i(), 0);
  EXPECT_EQ(b.to_i(), 1);
}
