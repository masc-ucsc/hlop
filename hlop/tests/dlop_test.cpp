//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "dlop.hpp"

#include <string>

#include "fmt/format.h"
#include "gtest/gtest.h"
#include "lconst.hpp"

class Dlop_test : public ::testing::Test {};

// =========================================================================
// Factory / parsing tests
// =========================================================================
TEST_F(Dlop_test, create_integer) {
  auto a = Dlop::create_integer(42);
  EXPECT_EQ(a->to_i(), 42);
  EXPECT_TRUE(a->is_i());

  auto b = Dlop::create_integer(-7);
  EXPECT_EQ(b->to_i(), -7);
  EXPECT_TRUE(b->is_negative());
}

TEST_F(Dlop_test, create_bool) {
  auto t = Dlop::create_bool(true);
  EXPECT_TRUE(t->is_known_true());
  EXPECT_TRUE(t->is_bool());

  auto f = Dlop::create_bool(false);
  EXPECT_TRUE(f->is_known_false());
}

TEST_F(Dlop_test, from_pyrope_decimal) {
  auto a = Dlop::from_pyrope("123");
  EXPECT_EQ(a->to_i(), 123);

  auto b = Dlop::from_pyrope("-456");
  EXPECT_EQ(b->to_i(), -456);
}

TEST_F(Dlop_test, from_pyrope_hex) {
  auto a = Dlop::from_pyrope("0xdeadbeef");
  EXPECT_EQ(a->to_i(), 0xdeadbeef);

  auto b = Dlop::from_pyrope("-0xff");
  EXPECT_EQ(b->to_i(), -0xff);
}

TEST_F(Dlop_test, from_pyrope_binary) {
  auto a = Dlop::from_pyrope("0b1010");
  EXPECT_EQ(a->to_i(), 10);

  auto sb = Dlop::from_pyrope("0sb1010");
  EXPECT_EQ(sb->to_i(), -6);
}

TEST_F(Dlop_test, from_pyrope_bool) {
  auto t = Dlop::from_pyrope("true");
  EXPECT_TRUE(t->is_known_true());

  auto f = Dlop::from_pyrope("false");
  EXPECT_TRUE(f->is_known_false());
}

TEST_F(Dlop_test, from_pyrope_string) {
  auto s = Dlop::from_pyrope("'hello'");
  EXPECT_TRUE(s->is_string());
  EXPECT_EQ(s->to_string(), "hello");
}

// =========================================================================
// Arithmetic tests
// =========================================================================
TEST_F(Dlop_test, add_op) {
  auto a = Dlop::from_pyrope("100");
  auto b = Dlop::from_pyrope("200");
  auto c = a->add_op(b);
  EXPECT_EQ(c->to_i(), 300);
}

TEST_F(Dlop_test, add_op_negative) {
  auto a = Dlop::from_pyrope("10");
  auto b = Dlop::from_pyrope("-20");
  auto c = a->add_op(b);
  EXPECT_EQ(c->to_i(), -10);
}

TEST_F(Dlop_test, sub_op) {
  auto a = Dlop::from_pyrope("100");
  auto b = Dlop::from_pyrope("30");
  auto c = a->sub_op(b);
  EXPECT_EQ(c->to_i(), 70);
}

TEST_F(Dlop_test, mult_op) {
  auto a = Dlop::from_pyrope("7");
  auto b = Dlop::from_pyrope("6");
  auto c = a->mult_op(b);
  EXPECT_EQ(c->to_i(), 42);
}

TEST_F(Dlop_test, mult_op_negative) {
  auto a = Dlop::from_pyrope("-3");
  auto b = Dlop::from_pyrope("4");
  auto c = a->mult_op(b);
  EXPECT_EQ(c->to_i(), -12);
}

TEST_F(Dlop_test, div_op) {
  auto a = Dlop::from_pyrope("42");
  auto b = Dlop::from_pyrope("6");
  auto c = a->div_op(b);
  EXPECT_EQ(c->to_i(), 7);
}

TEST_F(Dlop_test, neg_op) {
  auto a = Dlop::from_pyrope("42");
  auto b = a->neg_op();
  EXPECT_EQ(b->to_i(), -42);
}

// =========================================================================
// Bitwise tests
// =========================================================================
TEST_F(Dlop_test, or_op) {
  auto a = Dlop::from_pyrope("0b1010");
  auto b = Dlop::from_pyrope("0b0101");
  auto c = a->or_op(b);
  EXPECT_EQ(c->to_i(), 0xF);
}

TEST_F(Dlop_test, and_op) {
  auto a = Dlop::from_pyrope("0b1110");
  auto b = Dlop::from_pyrope("0b1011");
  auto c = a->and_op(b);
  EXPECT_EQ(c->to_i(), 0b1010);
}

TEST_F(Dlop_test, xor_op) {
  auto a = Dlop::from_pyrope("0b1100");
  auto b = Dlop::from_pyrope("0b1010");
  auto c = a->xor_op(b);
  EXPECT_EQ(c->to_i(), 0b0110);
}

TEST_F(Dlop_test, not_op) {
  auto a = Dlop::create_integer(0);
  auto b = a->not_op();
  EXPECT_EQ(b->to_i(), -1);

  auto c = Dlop::create_integer(5);
  auto d = c->not_op();
  EXPECT_EQ(d->to_i(), -6);
}

// =========================================================================
// Shift tests
// =========================================================================
TEST_F(Dlop_test, lsh_op) {
  auto a = Dlop::from_pyrope("1");
  auto b = a->lsh_op(4);
  EXPECT_EQ(b->to_i(), 16);
}

TEST_F(Dlop_test, rsh_op) {
  auto a = Dlop::from_pyrope("0xff");
  auto b = a->rsh_op(4);
  EXPECT_EQ(b->to_i(), 0xf);
}

// =========================================================================
// Comparison tests
// =========================================================================
TEST_F(Dlop_test, comparisons) {
  auto a = Dlop::from_pyrope("10");
  auto b = Dlop::from_pyrope("20");

  EXPECT_TRUE(*a < *b);
  EXPECT_TRUE(*a <= *b);
  EXPECT_FALSE(*a > *b);
  EXPECT_FALSE(*a >= *b);
  EXPECT_TRUE(*a != *b);
  EXPECT_FALSE(*a == *b);
}

TEST_F(Dlop_test, eq_op) {
  auto a = Dlop::from_pyrope("42");
  auto b = Dlop::from_pyrope("42");
  auto c = a->eq_op(b);
  EXPECT_TRUE(c->is_known_true());

  auto d = Dlop::from_pyrope("43");
  auto e = a->eq_op(d);
  EXPECT_TRUE(e->is_known_false());
}

// =========================================================================
// Query tests
// =========================================================================
TEST_F(Dlop_test, get_bits) {
  EXPECT_EQ(Dlop::create_integer(0)->get_bits(), 0);
  EXPECT_EQ(Dlop::create_integer(1)->get_bits(), 2);
  EXPECT_EQ(Dlop::create_integer(-1)->get_bits(), 1);
  EXPECT_EQ(Dlop::create_integer(7)->get_bits(), 4);
  EXPECT_EQ(Dlop::create_integer(-8)->get_bits(), 4);
}

TEST_F(Dlop_test, is_mask) {
  EXPECT_TRUE(Dlop::from_pyrope("0xF")->is_mask());
  EXPECT_TRUE(Dlop::from_pyrope("0xFF")->is_mask());
  EXPECT_TRUE(Dlop::from_pyrope("1")->is_mask());
  EXPECT_FALSE(Dlop::from_pyrope("0")->is_mask());
  EXPECT_FALSE(Dlop::from_pyrope("6")->is_mask());
}

TEST_F(Dlop_test, is_power2) {
  EXPECT_TRUE(Dlop::from_pyrope("1")->is_power2());
  EXPECT_TRUE(Dlop::from_pyrope("0x100")->is_power2());
  EXPECT_FALSE(Dlop::from_pyrope("3")->is_power2());
  EXPECT_FALSE(Dlop::from_pyrope("0")->is_power2());
}

TEST_F(Dlop_test, popcount_test) {
  EXPECT_EQ(Dlop::from_pyrope("0b1010")->popcount(), 2);
  EXPECT_EQ(Dlop::from_pyrope("0xFF")->popcount(), 8);
}

TEST_F(Dlop_test, to_pyrope_roundtrip) {
  auto check = [](std::string_view txt) {
    auto d = Dlop::from_pyrope(txt);
    auto l = Lconst::from_pyrope(txt);
    EXPECT_EQ(d->to_pyrope(), l.to_pyrope()) << "mismatch for input: " << txt;
  };

  check("0");
  check("1");
  check("-1");
  check("42");
  check("-42");
  check("0xff");
  check("-0xff");
  check("0b1010");
  // "true"/"false" differ: Dlop has Boolean type returning "true"/"false",
  // Lconst stores as numeric -1/0 returning "-1"/"0"
}

// =========================================================================
// Unknown propagation tests
// =========================================================================
TEST_F(Dlop_test, unknown_basic) {
  auto a = Dlop::from_pyrope("0b1?0");
  EXPECT_TRUE(a->has_unknowns());

  auto bin = a->to_binary();
  EXPECT_GE(bin.size(), 3u);  // at least 3 bits
}

TEST_F(Dlop_test, and_unknown) {
  // 0 AND ? = 0 (known)
  // 1 AND ? = ? (unknown)
  auto a = Dlop::from_pyrope("0b10");
  auto b = Dlop::from_pyrope("0b??");
  auto c = a->and_op(b);
  // bit 0: 0 AND ? = 0 (known 0)
  // bit 1: 1 AND ? = ? (unknown)
  EXPECT_TRUE(c->has_unknowns());
}

TEST_F(Dlop_test, or_unknown) {
  // 1 OR ? = 1 (known)
  // 0 OR ? = ? (unknown)
  auto a = Dlop::from_pyrope("0b10");
  auto b = Dlop::from_pyrope("0b??");
  auto c = a->or_op(b);
  // bit 0: 0 OR ? = ? (unknown)
  // bit 1: 1 OR ? = 1 (known 1)
  EXPECT_TRUE(c->has_unknowns());
}
