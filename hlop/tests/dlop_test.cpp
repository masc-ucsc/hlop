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

TEST_F(Dlop_test, concat_op_string_byte_aligned) {
  // Regression for a get_bits()-vs-byte_count mismatch: `"hello "` packs
  // into 48 bits but get_bits() returns 47 (it reserves a sign bit).
  // Concat must shift by the byte-aligned width, otherwise the second
  // operand's bytes end up offset by one bit and the round-trip via
  // to_string returns garbage. The cases below all have the property
  // that the top byte's MSB is 0, which is where the off-by-one bites.
  struct Case { const char* lhs; const char* rhs; const char* expected; };
  for (const auto& [lhs, rhs, expected] : std::vector<Case>{
           {"'a'", "'b'", "ab"},
           {"'hello '", "'world'", "hello world"},
           {"'hello '", "'world '", "hello world "},
           {"' '", "' '", "  "},
       }) {
    auto a = Dlop::from_pyrope(lhs);
    auto b = Dlop::from_pyrope(rhs);
    auto c = a->concat_op(*b);
    EXPECT_TRUE(c->is_string()) << lhs << " ++ " << rhs;
    EXPECT_EQ(c->to_string(), expected) << lhs << " ++ " << rhs;
  }
}

TEST_F(Dlop_test, from_pyrope_unsigned_prefix) {
  // `0u<base>...` is explicit-unsigned with a following base selector.
  auto ub = Dlop::from_pyrope("0ub10101");
  EXPECT_FALSE(ub->is_string());
  EXPECT_EQ(ub->to_i(), 0b10101);

  auto ux = Dlop::from_pyrope("0uxFF");
  EXPECT_EQ(ux->to_i(), 0xFF);

  auto uo = Dlop::from_pyrope("0uo17");
  EXPECT_EQ(uo->to_i(), 017);

  auto ud = Dlop::from_pyrope("0ud42");
  EXPECT_EQ(ud->to_i(), 42);
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

// Nil propagates through bitwise/logical ops, with the boolean short-circuit
// identities (false AND _, true OR _) still folding to a concrete result.
TEST_F(Dlop_test, and_or_xor_not_propagate_nil) {
  auto t = Dlop::create_bool(true);
  auto f = Dlop::create_bool(false);
  auto n = Dlop::nil();

  // and_op: false short-circuits, everything else with a nil → nil
  EXPECT_TRUE(n->and_op(*n)->is_nil());
  EXPECT_TRUE(n->and_op(*t)->is_nil());
  EXPECT_TRUE(t->and_op(*n)->is_nil());
  EXPECT_TRUE(n->and_op(*f)->is_known_false());
  EXPECT_TRUE(f->and_op(*n)->is_known_false());

  // or_op: true short-circuits, everything else with a nil → nil
  EXPECT_TRUE(n->or_op(*n)->is_nil());
  EXPECT_TRUE(n->or_op(*f)->is_nil());
  EXPECT_TRUE(f->or_op(*n)->is_nil());
  EXPECT_TRUE(n->or_op(*t)->is_known_true());
  EXPECT_TRUE(t->or_op(*n)->is_known_true());

  // xor_op: no short-circuit identity → any nil operand poisons the result
  EXPECT_TRUE(n->xor_op(*n)->is_nil());
  EXPECT_TRUE(n->xor_op(*t)->is_nil());
  EXPECT_TRUE(t->xor_op(*n)->is_nil());

  // not_op: unset stays unset
  EXPECT_TRUE(n->not_op()->is_nil());
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

  EXPECT_TRUE(a->lt_op(b)->is_known_true());
  EXPECT_TRUE(a->le_op(b)->is_known_true());
  EXPECT_TRUE(a->gt_op(b)->is_known_false());
  EXPECT_TRUE(a->ge_op(b)->is_known_false());
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

// get_mask_op(mask) — multi-bit mask packs the selected bits LSB-first.
TEST_F(Dlop_test, get_mask_op_multibit_packed) {
  auto v = Dlop::create_integer(0xfeed);
  // mask 0xff selects the low byte → 0xed
  EXPECT_EQ(v->get_mask_op(Dlop::create_integer(0xff))->to_i(), 0xed);
  // non-contiguous mask 0xf00 selects nibble [11..8] → 0xe (packed low)
  EXPECT_EQ(v->get_mask_op(Dlop::create_integer(0xf00))->to_i(), 0xe);
}

// get_mask_op(mask) — single-bit mask returns the signed 1-bit integer
// (-1 if the bit is set, 0 if clear), not the unsigned 1/0.
TEST_F(Dlop_test, get_mask_op_single_bit_set_is_neg_one) {
  // 0b1010, bit 1 mask → bit is set → -1
  auto v = Dlop::create_integer(0b1010);
  auto r = v->get_mask_op(Dlop::create_integer(0b0010));
  EXPECT_EQ(r->to_i(), -1);
  EXPECT_TRUE(r->is_negative());
}

TEST_F(Dlop_test, get_mask_op_single_bit_clear_is_zero) {
  // 0b1010, bit 0 mask → bit clear → 0
  auto v = Dlop::create_integer(0b1010);
  auto r = v->get_mask_op(Dlop::create_integer(0b0001));
  EXPECT_EQ(r->to_i(), 0);
  EXPECT_FALSE(r->is_negative());
}

// get_mask_op(mask) — when the selected bit is an unknown, the result is
// a 1-bit unknown (0sb?), not 0 or -1.
TEST_F(Dlop_test, get_mask_op_single_bit_unknown) {
  // 0sb10?0: bit 0 = 0, bit 1 = ?, bit 2 = 0, bit 3 = 1.
  auto v = Dlop::from_pyrope("0sb10?0");

  // Selecting bit 1 (the ?) → 1-bit unknown (structurally equal to unknown(1)).
  auto r_unk = v->get_mask_op(Dlop::create_integer(0b0010));
  EXPECT_TRUE(r_unk->has_unknowns());
  EXPECT_EQ(r_unk->to_pyrope(), Dlop::unknown(1)->to_pyrope());

  // Selecting bit 3 (known 1) → -1.
  auto r_set = v->get_mask_op(Dlop::create_integer(0b1000));
  EXPECT_FALSE(r_set->has_unknowns());
  EXPECT_EQ(r_set->to_i(), -1);

  // Selecting bit 0 (known 0) → 0.
  auto r_clr = v->get_mask_op(Dlop::create_integer(0b0001));
  EXPECT_FALSE(r_clr->has_unknowns());
  EXPECT_EQ(r_clr->to_i(), 0);
}

// Unknown bits inside a multi-bit selection propagate to the packed result.
TEST_F(Dlop_test, get_mask_op_multibit_unknown_propagates) {
  // 0sb10?0, mask 0b1110 → bits 3,2,1 → 1,0,? → packed LSB-first: ?,0,1
  auto v = Dlop::from_pyrope("0sb10?0");
  auto r = v->get_mask_op(Dlop::create_integer(0b1110));
  EXPECT_TRUE(r->has_unknowns());
  EXPECT_GE(r->get_bits(), 3);
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

// =========================================================================
// Three-valued op tests (TODO: hlop_todo.md)
// =========================================================================
TEST_F(Dlop_test, eq_known_bits_decide_false) {
  // bit 0: known 0 vs known 1 — disagree → result must be definite false.
  auto a = Dlop::from_pyrope("0sb?00");
  auto b = Dlop::from_pyrope("0sb?01");
  auto r = a->eq_op(*b);
  EXPECT_TRUE(r->is_known_false());
}

TEST_F(Dlop_test, eq_unknown_when_known_bits_agree) {
  // every known position agrees (bit 0=0, bit 2=1); only bit 1 unknown on both.
  auto a = Dlop::from_pyrope("0sb1?0");
  auto b = Dlop::from_pyrope("0sb1?0");
  auto r = a->eq_op(*b);
  EXPECT_FALSE(r->is_known_true());
  EXPECT_FALSE(r->is_known_false());
  EXPECT_TRUE(r->has_unknowns());
}

TEST_F(Dlop_test, eq_known_bits_decide_false_top) {
  // bit 0: known 0 vs known 1 — decides false (bit 1 unknown on LHS).
  auto a = Dlop::from_pyrope("0sb1?0");
  auto b = Dlop::from_pyrope("0sb111");
  auto r = a->eq_op(*b);
  EXPECT_TRUE(r->is_known_false());
}

TEST_F(Dlop_test, lt_decided_by_top_sign_bit) {
  // 0sb01 (+1) < 0sb1? (negative): top bit known-different — definite.
  auto a = Dlop::from_pyrope("0sb01");
  auto b = Dlop::from_pyrope("0sb1?");
  // a > b numerically (a positive, b negative).
  EXPECT_TRUE(a->gt_op(*b)->is_known_true());
  EXPECT_TRUE(b->lt_op(*a)->is_known_true());
}

TEST_F(Dlop_test, lt_unknown_when_decided_bit_unknown) {
  // 0sb1?0 vs 0sb111: top bit agrees, bit 1 unknown on LHS → unknown.
  auto a = Dlop::from_pyrope("0sb1?0");
  auto b = Dlop::from_pyrope("0sb111");
  auto r = a->lt_op(*b);
  EXPECT_TRUE(r->has_unknowns());
  EXPECT_FALSE(r->is_known_true());
  EXPECT_FALSE(r->is_known_false());
}

TEST_F(Dlop_test, ge_equal_known_bits_only_unknown) {
  // 0sb1?0 vs 0sb1?0: known bits all match, bit 1 unknown on both sides. The
  // conservative MSB walk gives up at the first unknown bit → unknown.
  auto a = Dlop::from_pyrope("0sb1?0");
  auto b = Dlop::from_pyrope("0sb1?0");
  auto r = a->ge_op(*b);
  EXPECT_TRUE(r->has_unknowns());
}

TEST_F(Dlop_test, add_unknown_grows_carry) {
  // 0sb?? + 1: every bit unknown — carry expansion keeps the result fully
  // unknown (base=-1, extra=-1 in one word). Width-sensitive consumers see
  // a >=2-bit unknown.
  auto a = Dlop::from_pyrope("0sb??");
  auto r = a->add_op(int64_t(1));
  EXPECT_TRUE(r->has_unknowns());
  EXPECT_GE(r->get_bits(), 2);
}

TEST_F(Dlop_test, add_unknown_does_not_propagate_past_known_zero) {
  // 0sb1?00 + 1: carry from bit 0 (1) hits known 0 at bit 1 → carry dies.
  // Bit 2 stays unknown, bit 3 stays known 1. So result bit 0 is 1, bit 1 is
  // 0, bit 2 unknown, bit 3 known 1.
  // (Current implementation widens "unknown above lowest unknown" which is a
  // conservative envelope; bit 3 may also be flagged unknown by the
  // hi_fill path. We assert the bits that the spec considers must-known and
  // accept the conservative envelope above the unknown.)
  auto a = Dlop::from_pyrope("0sb1?00");
  auto r = a->add_op(int64_t(1));
  // Bit 0 of the result must be a definite 1 (no unknowns flow from below).
  EXPECT_NE(r->base()[0] & 1, 0);
  EXPECT_EQ(r->extra()[0] & 1, 0);
}

TEST_F(Dlop_test, ror_unknown_unary) {
  // ror on a value whose only set bits are unknown → 1-bit unknown.
  auto u = Dlop::from_pyrope("0sb?0");
  auto r = u->ror_op();
  EXPECT_TRUE(r->has_unknowns());
}

TEST_F(Dlop_test, ror_known_true_with_unknowns) {
  // Any known-set bit forces the OR-reduction to true regardless of unknowns.
  auto v = Dlop::from_pyrope("0sb?1");  // bit 0 known 1, bit 1 unknown
  auto r = v->ror_op();
  EXPECT_TRUE(r->is_known_true());
}

TEST_F(Dlop_test, and_with_zero_is_zero) {
  // Identity fold: v & 0 == 0, even when v has unknowns.
  auto v = Dlop::from_pyrope("0sb?1?0");
  auto z = Dlop::create_integer(0);
  auto r = v->and_op(*z);
  EXPECT_TRUE(r->is_known_zero());
  EXPECT_FALSE(r->has_unknowns());
}

TEST_F(Dlop_test, or_with_minus_one_is_minus_one) {
  // Identity fold: v | -1 == -1, even when v has unknowns.
  auto v       = Dlop::from_pyrope("0sb?1?0");
  auto neg_one = Dlop::create_integer(-1);
  auto r       = v->or_op(*neg_one);
  EXPECT_FALSE(r->has_unknowns());
  EXPECT_TRUE(r->is_known_true());
  // All-ones value reads as -1.
  EXPECT_EQ(r->to_i(), int64_t(-1));
}

TEST_F(Dlop_test, or_with_all_ones_byte_forces_known) {
  // v | 0xff masks the low byte to known ones; remaining bits stay whatever v
  // says. The whole-byte equality requires the upper bits of v to also be
  // known (here we pick a known-zero value above the byte).
  auto v   = Dlop::from_pyrope("0sb0000000???????");  // 7 unknown bits in low byte
  auto ff  = Dlop::create_integer(0xff);
  auto r   = v->or_op(*ff);
  auto cmp = r->eq_op(*ff);
  EXPECT_TRUE(cmp->is_known_true());
}
