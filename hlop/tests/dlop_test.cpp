//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "dlop.hpp"

#include <string>

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

TEST_F(Dlop_test, concat_op_string) {
  // String ++ string is text concat, not numeric bit-concat.
  auto hello = Dlop::from_pyrope("'hello'");
  auto world = Dlop::from_pyrope("' world'");
  auto bang  = Dlop::from_pyrope("'!'");

  auto hw = hello->concat_op(*world);
  EXPECT_TRUE(hw->is_string());
  EXPECT_EQ(hw->to_string(), "hello world");

  auto hwb = hw->concat_op(*bang);
  EXPECT_TRUE(hwb->is_string());
  EXPECT_EQ(hwb->to_string(), "hello world!");

  // Empty-string identity: "" ++ "x" = "x" ; "x" ++ "" = "x".
  auto empty = Dlop::from_pyrope("''");
  EXPECT_TRUE(empty->is_string());
  auto a = empty->concat_op(*hello);
  EXPECT_TRUE(a->is_string());
  EXPECT_EQ(a->to_string(), "hello");
  auto b = hello->concat_op(*empty);
  EXPECT_TRUE(b->is_string());
  EXPECT_EQ(b->to_string(), "hello");
}

TEST_F(Dlop_test, concat_op_integer_unchanged) {
  // Integer ++ integer stays a numeric bit-concat (signed-positive
  // integers carry a leading-zero sign bit; 0b1010 occupies 5 bits in
  // pyrope, 0b11 occupies 3 bits, so the concat is (10 << 3) | 3 = 83).
  auto a = Dlop::from_pyrope("0b1010");
  auto b = Dlop::from_pyrope("0b11");
  auto c = a->concat_op(*b);
  EXPECT_FALSE(c->is_string());
  EXPECT_EQ(c->to_i(), 83);
}

TEST_F(Dlop_test, from_pyrope_nil) {
  // Bare nil/null tokens are the Pyrope nil literal — case-insensitive.
  for (auto* txt : {"nil", "Nil", "NIL", "NiL", "null", "Null", "NULL", "nUlL"}) {
    auto n = Dlop::from_pyrope(txt);
    EXPECT_TRUE(n->is_nil()) << "from_pyrope(\"" << txt << "\") should be nil";
    EXPECT_FALSE(n->is_string()) << "from_pyrope(\"" << txt << "\") should not be string";
  }

  // Quoted form keeps the original token as a string.
  auto q = Dlop::from_pyrope("'nil'");
  EXPECT_FALSE(q->is_nil());
  EXPECT_TRUE(q->is_string());
  EXPECT_EQ(q->to_string(), "nil");
}

TEST_F(Dlop_test, from_pyrope_bool_case_insensitive) {
  for (auto* txt : {"true", "True", "TRUE", "tRuE"}) {
    auto t = Dlop::from_pyrope(txt);
    EXPECT_TRUE(t->is_known_true()) << "from_pyrope(\"" << txt << "\") should be true";
  }
  for (auto* txt : {"false", "False", "FALSE", "fAlSe"}) {
    auto f = Dlop::from_pyrope(txt);
    EXPECT_TRUE(f->is_known_false()) << "from_pyrope(\"" << txt << "\") should be false";
  }
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
  EXPECT_FALSE(a->is_known_eq(*b));
  EXPECT_FALSE(a->same_repr(*b));
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

// =========================================================================
// Nil — tagged-unit
// =========================================================================
TEST_F(Dlop_test, is_known_zero_basic) {
  // Integer zero is is_known_zero; nonzero, nil, invalid, string are not.
  EXPECT_TRUE(Dlop::create_integer(0)->is_known_zero());
  EXPECT_FALSE(Dlop::create_integer(1)->is_known_zero());
  EXPECT_FALSE(Dlop::create_integer(-1)->is_known_zero());

  EXPECT_FALSE(Dlop::nil()->is_known_zero());
  EXPECT_FALSE(Dlop::invalid()->is_known_zero());
  EXPECT_FALSE(Dlop::create_string("")->is_known_zero());
  EXPECT_FALSE(Dlop::from_pyrope("0sb??")->is_known_zero());  // unknown bits
  // Boolean false also reads as is_known_zero (numeric-context zero).
  EXPECT_TRUE(Dlop::create_bool(false)->is_known_zero());
  EXPECT_FALSE(Dlop::create_bool(true)->is_known_zero());
}

TEST_F(Dlop_test, nil_is_distinct) {
  auto n = Dlop::nil();
  EXPECT_TRUE(n->is_nil());
  EXPECT_FALSE(n->is_invalid());
  EXPECT_FALSE(n->is_integer());
  auto inv = Dlop::invalid();
  EXPECT_FALSE(inv->is_nil());
  EXPECT_TRUE(inv->is_invalid());
}

// =========================================================================
// Mask helpers
// =========================================================================
TEST_F(Dlop_test, get_mask_value_static) {
  EXPECT_EQ(Dlop::get_mask_value(0)->to_i(), 1);   // bits==0 -> 1 per the contract
  EXPECT_EQ(Dlop::get_mask_value(1)->to_i(), 1);
  EXPECT_EQ(Dlop::get_mask_value(4)->to_i(), 0xF);
  EXPECT_EQ(Dlop::get_mask_value(8)->to_i(), 0xFF);
  EXPECT_EQ(Dlop::get_mask_value(16)->to_i(), 0xFFFF);
}

TEST_F(Dlop_test, get_mask_value_range) {
  // bits [3..0] -> 0xF
  EXPECT_EQ(Dlop::get_mask_value(3, 0)->to_i(), 0xF);
  // bits [7..4] -> 0xF0
  EXPECT_EQ(Dlop::get_mask_value(7, 4)->to_i(), 0xF0);
  // single bit h==l
  EXPECT_EQ(Dlop::get_mask_value(5, 5)->to_i(), 0x20);
}

TEST_F(Dlop_test, get_neg_mask_value_static) {
  // -1 << 4 == -16
  EXPECT_EQ(Dlop::get_neg_mask_value(4)->to_i(), -16);
  // -1 << 0 == -1  (but bits<=1 returns 1 per legacy semantics)
  EXPECT_EQ(Dlop::get_neg_mask_value(0)->to_i(), 1);
  EXPECT_EQ(Dlop::get_neg_mask_value(1)->to_i(), 1);
  EXPECT_EQ(Dlop::get_neg_mask_value(8)->to_i(), -256);
}

TEST_F(Dlop_test, get_mask_range_continuous) {
  // 0b0000_1111 -> (0, range_end)
  auto a = Dlop::create_integer(0xF);
  auto r = a->get_mask_range();
  EXPECT_EQ(r.first, 0);
  EXPECT_GE(r.second, 3);
}

TEST_F(Dlop_test, get_mask_range_shifted) {
  // 0b1111_0000 -> trailing 4 zeros + mask
  auto a = Dlop::create_integer(0xF0);
  auto r = a->get_mask_range();
  EXPECT_EQ(r.first, 4);
}

TEST_F(Dlop_test, get_mask_range_pairs_two_runs) {
  // 0b0011_0011 -> two runs of 2 ones each at positions 0 and 4
  auto a = Dlop::create_integer(0x33);
  auto pairs = a->get_mask_range_pairs();
  ASSERT_EQ(pairs.size(), 2u);
  EXPECT_EQ(pairs[0].first, 0);
  EXPECT_EQ(pairs[0].second, 2);
  EXPECT_EQ(pairs[1].first, 4);
  EXPECT_EQ(pairs[1].second, 2);
}

// =========================================================================
// Serialize / unserialize roundtrip
// =========================================================================
TEST_F(Dlop_test, serialize_roundtrip_int) {
  auto a = Dlop::create_integer(0x12345678);
  auto s = a->serialize();
  auto b = Dlop::unserialize(s);
  EXPECT_EQ(a->to_i(), b->to_i());
  EXPECT_EQ(a->type, b->type);
}

TEST_F(Dlop_test, serialize_roundtrip_negative) {
  auto a = Dlop::create_integer(-12345);
  auto s = a->serialize();
  auto b = Dlop::unserialize(s);
  EXPECT_EQ(a->to_i(), b->to_i());
}

TEST_F(Dlop_test, serialize_roundtrip_unknown) {
  auto a = Dlop::from_pyrope("0sb1?0?");
  auto s = a->serialize();
  auto b = Dlop::unserialize(s);
  EXPECT_EQ(a->to_pyrope(), b->to_pyrope());
}

TEST_F(Dlop_test, serialize_roundtrip_invalid_nil) {
  auto inv = Dlop::invalid();
  auto s   = inv->serialize();
  auto r   = Dlop::unserialize(s);
  EXPECT_TRUE(r->is_invalid());

  auto n  = Dlop::nil();
  auto s2 = n->serialize();
  auto r2 = Dlop::unserialize(s2);
  EXPECT_TRUE(r2->is_nil());
}

// =========================================================================
// Hash stability
// =========================================================================
TEST_F(Dlop_test, hash_consistency) {
  auto a = Dlop::create_integer(42);
  auto b = Dlop::create_integer(42);
  EXPECT_EQ(a->hash(), b->hash());
  auto c = Dlop::create_integer(43);
  EXPECT_NE(a->hash(), c->hash());
}

TEST_F(Dlop_test, hash_distinguishes_types) {
  auto i = Dlop::create_integer(0);
  auto b = Dlop::create_bool(false);
  // Different type tag -> different hash even when payload near-zero
  EXPECT_NE(i->hash(), b->hash());
}

// =========================================================================
// to_field
// =========================================================================
TEST_F(Dlop_test, to_field_integer) {
  EXPECT_EQ(Dlop::create_integer(0)->to_field(), "0");
  EXPECT_EQ(Dlop::create_integer(7)->to_field(), "7");
  EXPECT_EQ(Dlop::create_integer(42)->to_field(), "42");
}

TEST_F(Dlop_test, to_field_string) {
  auto s = Dlop::create_string("foo");
  EXPECT_EQ(s->to_field(), "foo");
}

// =========================================================================
// to_known_rand
// =========================================================================
TEST_F(Dlop_test, to_known_rand_no_unknowns_identity) {
  auto a = Dlop::create_integer(0xA5);
  auto b = a->to_known_rand();
  EXPECT_FALSE(b->has_unknowns());
  EXPECT_EQ(b->to_i(), 0xA5);
}

TEST_F(Dlop_test, to_known_rand_strips_unknowns) {
  auto a = Dlop::from_pyrope("0sb1??1");
  EXPECT_TRUE(a->has_unknowns());
  auto b = a->to_known_rand();
  EXPECT_FALSE(b->has_unknowns());
}

// =========================================================================
// std::format integration
// =========================================================================
TEST_F(Dlop_test, format_integration_value) {
  auto a = Dlop::create_integer(42);
  EXPECT_EQ(std::format("{}", *a), "42");
}

TEST_F(Dlop_test, format_integration_spool_ptr) {
  auto a = Dlop::create_integer(7);
  EXPECT_EQ(std::format("{}", a), "7");
  spool_ptr<Dlop> empty;
  EXPECT_EQ(std::format("{}", empty), "");
}
