//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// Cross-test: verify Dlop, Slop<128>, and Lconst produce identical results
// for the same inputs, compared via to_pyrope() string output.

#include "dlop.hpp"
#include "lconst.hpp"
#include "slop.hpp"

#include <string>

#include "fmt/format.h"
#include "gtest/gtest.h"
#include "lrand.hpp"

class Cross_test : public ::testing::Test {};

using S = Slop<128>;

// Helper: compare Dlop and Lconst to_pyrope for same input
static void check_pyrope_roundtrip(std::string_view txt) {
  auto d = Dlop::from_pyrope(txt);
  auto l = Lconst::from_pyrope(txt);
  auto s = S::from_pyrope(txt);

  auto d_str = d->to_pyrope();
  auto l_str = l.to_pyrope();
  auto s_str = s.to_pyrope();

  EXPECT_EQ(d_str, l_str) << "Dlop vs Lconst mismatch for: " << txt;
  EXPECT_EQ(d_str, s_str) << "Dlop vs Slop mismatch for: " << txt;
}

// =========================================================================
// Pyrope round-trip
// =========================================================================
TEST_F(Cross_test, pyrope_integers) {
  check_pyrope_roundtrip("0");
  check_pyrope_roundtrip("1");
  check_pyrope_roundtrip("-1");
  check_pyrope_roundtrip("42");
  check_pyrope_roundtrip("-42");
  check_pyrope_roundtrip("63");
  check_pyrope_roundtrip("-63");
}

TEST_F(Cross_test, pyrope_hex) {
  check_pyrope_roundtrip("0xff");
  check_pyrope_roundtrip("0xdead");
  check_pyrope_roundtrip("-0xff");
  check_pyrope_roundtrip("0xdeadbeef");
}

TEST_F(Cross_test, pyrope_binary) {
  check_pyrope_roundtrip("0b1010");
  check_pyrope_roundtrip("0b0");
  check_pyrope_roundtrip("0b1");
  check_pyrope_roundtrip("0b11111111");
}

TEST_F(Cross_test, pyrope_special) {
  // "true"/"false": Dlop has Boolean type returning "true"/"false",
  // Lconst stores as numeric -1/0 returning "-1"/"0".
  // We just check Dlop == Slop here.
  auto d_t = Dlop::from_pyrope("true");
  auto s_t = S::from_pyrope("true");
  EXPECT_EQ(d_t->to_pyrope(), s_t.to_pyrope());

  auto d_f = Dlop::from_pyrope("false");
  auto s_f = S::from_pyrope("false");
  EXPECT_EQ(d_f->to_pyrope(), s_f.to_pyrope());
}

// =========================================================================
// Arithmetic cross-checks
// =========================================================================
static void check_add(int64_t a, int64_t b) {
  auto d_a = Dlop::create_integer(a);
  auto d_b = Dlop::create_integer(b);
  auto d_r = d_a->add_op(d_b);

  auto s_a = S::create_integer(a);
  auto s_b = S::create_integer(b);
  auto s_r = s_a.add_op(s_b);

  auto l_r = Lconst(a) + Lconst(b);

  EXPECT_EQ(d_r->to_pyrope(), l_r.to_pyrope()) << "add Dlop vs Lconst: " << a << " + " << b;
  EXPECT_EQ(d_r->to_pyrope(), s_r.to_pyrope()) << "add Dlop vs Slop: " << a << " + " << b;
}

TEST_F(Cross_test, add_basic) {
  check_add(0, 0);
  check_add(1, 1);
  check_add(-1, 1);
  check_add(100, 200);
  check_add(-100, -200);
  check_add(INT32_MAX, 1);
  check_add(INT32_MIN, -1);
}

static void check_sub(int64_t a, int64_t b) {
  auto d_r = Dlop::create_integer(a)->sub_op(Dlop::create_integer(b));
  auto s_r = S::create_integer(a).sub_op(S::create_integer(b));
  auto l_r = Lconst(a) - Lconst(b);

  EXPECT_EQ(d_r->to_pyrope(), l_r.to_pyrope()) << "sub Dlop vs Lconst: " << a << " - " << b;
  EXPECT_EQ(d_r->to_pyrope(), s_r.to_pyrope()) << "sub Dlop vs Slop: " << a << " - " << b;
}

TEST_F(Cross_test, sub_basic) {
  check_sub(0, 0);
  check_sub(10, 3);
  check_sub(3, 10);
  check_sub(-5, -3);
  check_sub(-5, 3);
}

static void check_mult(int64_t a, int64_t b) {
  auto d_r = Dlop::create_integer(a)->mult_op(Dlop::create_integer(b));
  auto s_r = S::create_integer(a).mult_op(S::create_integer(b));
  auto l_a = Lconst(a);
  auto l_b = Lconst(b);
  auto l_r = l_a.mult_op(l_b);

  EXPECT_EQ(d_r->to_pyrope(), l_r.to_pyrope()) << "mult Dlop vs Lconst: " << a << " * " << b;
  EXPECT_EQ(d_r->to_pyrope(), s_r.to_pyrope()) << "mult Dlop vs Slop: " << a << " * " << b;
}

TEST_F(Cross_test, mult_basic) {
  check_mult(0, 0);
  check_mult(1, 1);
  check_mult(7, 6);
  check_mult(-3, 4);
  check_mult(-3, -4);
  check_mult(100, 100);
}

static void check_div(int64_t a, int64_t b) {
  if (b == 0) return;  // skip div by zero
  auto d_r = Dlop::create_integer(a)->div_op(Dlop::create_integer(b));
  auto s_r = S::create_integer(a).div_op(S::create_integer(b));
  auto l_r = Lconst(a).div_op(Lconst(b));

  EXPECT_EQ(d_r->to_pyrope(), l_r.to_pyrope()) << "div Dlop vs Lconst: " << a << " / " << b;
  EXPECT_EQ(d_r->to_pyrope(), s_r.to_pyrope()) << "div Dlop vs Slop: " << a << " / " << b;
}

TEST_F(Cross_test, div_basic) {
  check_div(42, 6);
  check_div(42, 7);
  check_div(-42, 6);
  check_div(42, -6);
  check_div(100, 3);
}

// =========================================================================
// Bitwise cross-checks
// =========================================================================
static void check_and(int64_t a, int64_t b) {
  auto d_r = Dlop::create_integer(a)->and_op(Dlop::create_integer(b));
  auto s_r = S::create_integer(a).and_op(S::create_integer(b));
  auto l_r = Lconst(a).and_op(Lconst(b));

  EXPECT_EQ(d_r->to_pyrope(), l_r.to_pyrope()) << "and Dlop vs Lconst: " << a << " & " << b;
  EXPECT_EQ(d_r->to_pyrope(), s_r.to_pyrope()) << "and Dlop vs Slop: " << a << " & " << b;
}

TEST_F(Cross_test, and_basic) {
  check_and(0xFF, 0x0F);
  check_and(0, -1);
  check_and(-1, -1);
  check_and(0b1010, 0b1100);
}

static void check_or(int64_t a, int64_t b) {
  auto d_r = Dlop::create_integer(a)->or_op(Dlop::create_integer(b));
  auto s_r = S::create_integer(a).or_op(S::create_integer(b));
  auto l_r = Lconst(a).or_op(Lconst(b));

  EXPECT_EQ(d_r->to_pyrope(), l_r.to_pyrope()) << "or Dlop vs Lconst: " << a << " | " << b;
  EXPECT_EQ(d_r->to_pyrope(), s_r.to_pyrope()) << "or Dlop vs Slop: " << a << " | " << b;
}

TEST_F(Cross_test, or_basic) {
  check_or(0b1010, 0b0101);
  check_or(0, 0);
  check_or(-1, 0);
  check_or(0xFF00, 0x00FF);
}

static void check_not(int64_t a) {
  auto d_r = Dlop::create_integer(a)->not_op();
  auto s_r = S::create_integer(a).not_op();
  auto l_r = Lconst(a).not_op();

  EXPECT_EQ(d_r->to_pyrope(), l_r.to_pyrope()) << "not Dlop vs Lconst: ~" << a;
  EXPECT_EQ(d_r->to_pyrope(), s_r.to_pyrope()) << "not Dlop vs Slop: ~" << a;
}

TEST_F(Cross_test, not_basic) {
  check_not(0);
  check_not(1);
  check_not(-1);
  check_not(0xFF);
  check_not(-128);
}

// =========================================================================
// Shift cross-checks
// =========================================================================
static void check_lsh(int64_t a, int amt) {
  auto d_r = Dlop::create_integer(a)->lsh_op(amt);
  auto s_r = S::create_integer(a).lsh_op(amt);
  auto l_r = Lconst(a).lsh_op(amt);

  EXPECT_EQ(d_r->to_pyrope(), l_r.to_pyrope()) << "lsh Dlop vs Lconst: " << a << " << " << amt;
  EXPECT_EQ(d_r->to_pyrope(), s_r.to_pyrope()) << "lsh Dlop vs Slop: " << a << " << " << amt;
}

TEST_F(Cross_test, lsh_basic) {
  check_lsh(1, 0);
  check_lsh(1, 4);
  check_lsh(1, 8);
  check_lsh(-1, 3);
  check_lsh(0xFF, 8);
}

static void check_rsh(int64_t a, int amt) {
  auto d_r = Dlop::create_integer(a)->rsh_op(amt);
  auto s_r = S::create_integer(a).rsh_op(amt);
  auto l_r = Lconst(a).rsh_op(amt);

  EXPECT_EQ(d_r->to_pyrope(), l_r.to_pyrope()) << "rsh Dlop vs Lconst: " << a << " >> " << amt;
  EXPECT_EQ(d_r->to_pyrope(), s_r.to_pyrope()) << "rsh Dlop vs Slop: " << a << " >> " << amt;
}

TEST_F(Cross_test, rsh_basic) {
  check_rsh(0xFF, 4);
  check_rsh(0x1000, 8);
  check_rsh(-8, 2);
  check_rsh(1, 1);
}

// =========================================================================
// Query cross-checks
// =========================================================================
TEST_F(Cross_test, get_bits_cross) {
  auto values = {int64_t(0), int64_t(1), int64_t(-1), int64_t(7), int64_t(-8), int64_t(127), int64_t(-128), int64_t(1000)};
  for (auto v : values) {
    auto d_bits = Dlop::create_integer(v)->get_bits();
    auto s_bits = S::create_integer(v).get_bits();
    auto l_bits = Lconst(v).get_bits();
    EXPECT_EQ(d_bits, l_bits) << "get_bits Dlop vs Lconst for " << v;
    EXPECT_EQ(d_bits, s_bits) << "get_bits Dlop vs Slop for " << v;
  }
}

// =========================================================================
// Random cross-checks
// =========================================================================
TEST_F(Cross_test, random_add) {
  Lrand<int32_t> rng;
  for (int i = 0; i < 100; ++i) {
    int64_t a = rng.any();
    int64_t b = rng.any();
    check_add(a, b);
  }
}

TEST_F(Cross_test, random_sub) {
  Lrand<int32_t> rng;
  for (int i = 0; i < 100; ++i) {
    int64_t a = rng.any();
    int64_t b = rng.any();
    check_sub(a, b);
  }
}

TEST_F(Cross_test, random_mult) {
  Lrand<int16_t> rng;  // smaller range to avoid overflow in 128-bit slop
  for (int i = 0; i < 100; ++i) {
    int64_t a = rng.any();
    int64_t b = rng.any();
    check_mult(a, b);
  }
}

TEST_F(Cross_test, random_and_or_not) {
  Lrand<int32_t> rng;
  for (int i = 0; i < 100; ++i) {
    int64_t a = rng.any();
    int64_t b = rng.any();
    check_and(a, b);
    check_or(a, b);
    check_not(a);
  }
}
