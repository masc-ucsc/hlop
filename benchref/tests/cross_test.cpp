//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// Random cross-checks across:
// - unbounded signed semantics: Dlop, Slop<128>, Lconst
// - fixed-width unsigned semantics: UInt<64>, with Lconst as an exact positive reference
// - fixed-width signed semantics: SInt<64>, with native two's-complement expectations

#include "dlop.hpp"
#include "gtest/gtest.h"
#include "lconst.hpp"
#include "slop.hpp"
#include "sint.hpp"
#include "uint.hpp"

#include <array>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace {

using SignedStatic = Slop<128>;

std::string to_hex64(uint64_t value) {
  std::ostringstream os;
  os << "0x" << std::hex << std::setfill('0') << std::setw(16) << value;
  return os.str();
}

std::string to_hex_u64_result(uint64_t value) {
  return to_hex64(value);
}

int64_t wrap_add64(int64_t a, int64_t b) {
  return static_cast<int64_t>(static_cast<uint64_t>(a) + static_cast<uint64_t>(b));
}

int64_t wrap_sub64(int64_t a, int64_t b) {
  return static_cast<int64_t>(static_cast<uint64_t>(a) - static_cast<uint64_t>(b));
}

int64_t wrap_lsh64(int64_t a, unsigned shamt) {
  return static_cast<int64_t>(static_cast<uint64_t>(a) << shamt);
}

int64_t arithmetic_rsh64(int64_t a, unsigned shamt) {
  if (shamt == 0) return a;
  if (a >= 0) return static_cast<int64_t>(static_cast<uint64_t>(a) >> shamt);

  uint64_t shifted = static_cast<uint64_t>(a) >> shamt;
  uint64_t fill    = ~uint64_t{0} << (64 - shamt);
  return static_cast<int64_t>(shifted | fill);
}

std::vector<int64_t> signed_cornercases() {
  return {
      -(int64_t(1) << 32),
      -((int64_t(1) << 32) - 1),
      std::numeric_limits<int32_t>::min(),
      std::numeric_limits<int32_t>::min() + 1,
      -65536,
      -1024,
      -255,
      -2,
      -1,
      0,
      1,
      2,
      3,
      7,
      8,
      15,
      31,
      63,
      64,
      127,
      255,
      1024,
      65535,
      (int64_t(1) << 16) - 1,
      (int64_t(1) << 32) - 1,
      int64_t(1) << 32,
      std::numeric_limits<int32_t>::max() - 1,
      std::numeric_limits<int32_t>::max(),
  };
}

std::vector<uint64_t> unsigned_cornercases() {
  return {
      0,
      1,
      2,
      3,
      7,
      8,
      15,
      31,
      63,
      64,
      127,
      255,
      256,
      1024,
      65535,
      (uint64_t(1) << 32) - 1,
      uint64_t(1) << 32,
      (uint64_t(1) << 63) - 1,
      uint64_t(1) << 63,
      std::numeric_limits<uint64_t>::max() - 1,
      std::numeric_limits<uint64_t>::max(),
  };
}

std::vector<int64_t> random_signed_values() {
  std::vector<int64_t> values = signed_cornercases();

  std::mt19937_64 rng(0x5eed1234ULL);
  std::uniform_int_distribution<int32_t> dist(std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::max());

  for (int i = 0; i < 64; ++i) {
    values.emplace_back(dist(rng));
  }

  return values;
}

std::vector<uint64_t> random_unsigned_values() {
  std::vector<uint64_t> values = unsigned_cornercases();

  std::mt19937_64 rng(0x1234beefULL);
  std::uniform_int_distribution<uint64_t> dist(0, std::numeric_limits<uint64_t>::max());

  for (int i = 0; i < 64; ++i) {
    values.emplace_back(dist(rng));
  }

  return values;
}

std::string canonical_pyrope(std::string_view text) {
  return Lconst::from_pyrope(text).to_pyrope();
}

std::string canonical_pyrope(const Dlop &value) {
  return canonical_pyrope(value.to_pyrope());
}

std::string canonical_pyrope(const SignedStatic &value) {
  return canonical_pyrope(value.to_pyrope());
}

std::string canonical_pyrope(const Lconst &value) {
  return canonical_pyrope(value.to_pyrope());
}

void expect_unbounded_equal(std::string_view op, int64_t a, int64_t b, const Dlop &d_res, const SignedStatic &s_res, const Lconst &l_res) {
  EXPECT_EQ(canonical_pyrope(d_res), canonical_pyrope(s_res)) << op << " Dlop vs Slop mismatch for a=" << a << " b=" << b;
  EXPECT_EQ(canonical_pyrope(d_res), canonical_pyrope(l_res)) << op << " Dlop vs Lconst mismatch for a=" << a << " b=" << b;
}

void check_unbounded_pair(int64_t a, int64_t b) {
  auto d_a = Dlop::create_integer(a);
  auto d_b = Dlop::create_integer(b);
  auto s_a = SignedStatic::create_integer(a);
  auto s_b = SignedStatic::create_integer(b);
  auto l_a = Lconst(a);
  auto l_b = Lconst(b);

  expect_unbounded_equal("add", a, b, *d_a->add_op(d_b), s_a.add_op(s_b), l_a.add_op(l_b));
  expect_unbounded_equal("sub", a, b, *d_a->sub_op(d_b), s_a.sub_op(s_b), l_a.sub_op(l_b));
  expect_unbounded_equal("and", a, b, *d_a->and_op(d_b), s_a.and_op(s_b), l_a.and_op(l_b));
  expect_unbounded_equal("or", a, b, *d_a->or_op(d_b), s_a.or_op(s_b), l_a.or_op(l_b));

  EXPECT_EQ(canonical_pyrope(*d_a->not_op()), canonical_pyrope(s_a.not_op())) << "not Dlop vs Slop mismatch for a=" << a;
  EXPECT_EQ(canonical_pyrope(*d_a->not_op()), canonical_pyrope(l_a.not_op())) << "not Dlop vs Lconst mismatch for a=" << a;

  for (int shamt : {0, 1, 7, 31, 63}) {
    if (a >= 0) {
      expect_unbounded_equal("lsh", a, shamt, *d_a->shl_op(shamt), s_a.shl_op(shamt), l_a.lsh_op(shamt));
    }
    expect_unbounded_equal("rsh", a, shamt, *d_a->sra_op(shamt), s_a.sra_op(shamt), l_a.rsh_op(shamt));
  }

  EXPECT_EQ(d_a->get_bits(), s_a.get_bits()) << "get_bits Dlop vs Slop mismatch for a=" << a;
  EXPECT_EQ(d_a->get_bits(), l_a.get_bits()) << "get_bits Dlop vs Lconst mismatch for a=" << a;

  if (b != 0 && !(a == std::numeric_limits<int64_t>::min() && b == -1)) {
    expect_unbounded_equal("div", a, b, *d_a->div_op(d_b), s_a.div_op(s_b), l_a.div_op(l_b));
  }
}

void check_unbounded_mult_pair(int64_t a, int64_t b) {
  auto d_a = Dlop::create_integer(a);
  auto d_b = Dlop::create_integer(b);
  auto s_a = SignedStatic::create_integer(a);
  auto s_b = SignedStatic::create_integer(b);
  auto l_a = Lconst(a);
  auto l_b = Lconst(b);

  expect_unbounded_equal("mul", a, b, *d_a->mult_op(d_b), s_a.mult_op(s_b), l_a.mult_op(l_b));
}

template <typename UIntLike>
void expect_uint_matches_lconst(std::string_view op, uint64_t a, uint64_t b, const UIntLike &u_res, const Lconst &l_res) {
  auto from_uint = Lconst::from_pyrope(u_res.to_string_hex());
  EXPECT_EQ(from_uint.to_pyrope(), l_res.to_pyrope()) << op << " UInt vs Lconst mismatch for a=" << to_hex64(a) << " b=" << to_hex64(b);
}

void check_uint_pair(uint64_t a, uint64_t b) {
  UInt<64> u_a(a);
  UInt<64> u_b(b);

  auto l_a = Lconst::from_pyrope(to_hex64(a));
  auto l_b = Lconst::from_pyrope(to_hex64(b));

  expect_uint_matches_lconst("add", a, b, u_a + u_b, l_a.add_op(l_b));
  expect_uint_matches_lconst("mul", a, b, u_a * u_b, l_a.mult_op(l_b));
  expect_uint_matches_lconst("and", a, b, u_a & u_b, l_a.and_op(l_b));
  expect_uint_matches_lconst("or", a, b, u_a | u_b, l_a.or_op(l_b));

  EXPECT_EQ((~u_a).as_single_word(), ~a) << "not UInt mismatch for a=" << to_hex64(a);

  for (int shamt : {0, 1, 7, 31, 63}) {
    auto u_lsh = Lconst::from_pyrope((u_a << UInt<6>(shamt)).to_string_hex());
    auto u_rsh = Lconst::from_pyrope((u_a >> UInt<6>(shamt)).to_string_hex());

    EXPECT_EQ(u_lsh.to_pyrope(), l_a.lsh_op(shamt).to_pyrope()) << "lsh UInt vs Lconst mismatch for a=" << to_hex64(a) << " shamt=" << shamt;
    EXPECT_EQ(u_rsh.to_pyrope(), l_a.rsh_op(shamt).to_pyrope()) << "rsh UInt vs Lconst mismatch for a=" << to_hex64(a) << " shamt=" << shamt;
  }

  if (b != 0) {
    expect_uint_matches_lconst("div", a, b, u_a / u_b, l_a.div_op(l_b));
    expect_uint_matches_lconst("mod", a, b, u_a % u_b, Lconst::from_pyrope(to_hex64(a % b)));
  }
}

void check_sint_pair(int64_t a, int64_t b) {
  SInt<64> s_a(a);
  SInt<64> s_b(b);

  EXPECT_EQ(s_a.addw(s_b).as_single_word(), wrap_add64(a, b)) << "addw SInt mismatch for a=" << a << " b=" << b;
  EXPECT_EQ(s_a.subw(s_b).as_single_word(), wrap_sub64(a, b)) << "subw SInt mismatch for a=" << a << " b=" << b;

  auto s_not = static_cast<uint64_t>((~s_a).as_single_word());
  auto e_not = static_cast<uint64_t>(~a);
  EXPECT_EQ(s_not, e_not) << "not SInt mismatch for a=" << a;

  auto s_and = static_cast<uint64_t>((s_a & s_b).as_single_word());
  auto s_or  = static_cast<uint64_t>((s_a | s_b).as_single_word());
  EXPECT_EQ(s_and, static_cast<uint64_t>(a & b)) << "and SInt mismatch for a=" << a << " b=" << b;
  EXPECT_EQ(s_or, static_cast<uint64_t>(a | b)) << "or SInt mismatch for a=" << a << " b=" << b;

  for (unsigned shamt : {0u, 1u, 7u, 31u, 63u}) {
    EXPECT_EQ(s_a.dshlw(UInt<6>(shamt)).as_single_word(), wrap_lsh64(a, shamt)) << "dshlw SInt mismatch for a=" << a << " shamt=" << shamt;
    EXPECT_EQ((s_a >> UInt<6>(shamt)).as_single_word(), arithmetic_rsh64(a, shamt)) << "rsh SInt mismatch for a=" << a << " shamt=" << shamt;
  }

  if (b != 0 && !(a == std::numeric_limits<int64_t>::min() && b == -1)) {
    EXPECT_EQ((s_a % s_b).as_single_word(), a % b) << "mod SInt mismatch for a=" << a << " b=" << b;
  }
}

}  // namespace

TEST(CrossTest, UnboundedModelsShareCornercasesAndRandomValues) {
  auto values = random_signed_values();

  for (auto a : values) {
    for (auto b : values) {
      check_unbounded_pair(a, b);
    }
  }
}

TEST(CrossTest, UnboundedModelsMultiplySmallerCornercasesAndRandomValues) {
  std::vector<int64_t> values = {
      std::numeric_limits<int16_t>::min(),
      std::numeric_limits<int16_t>::min() + 1,
      -255,
      -16,
      -2,
      -1,
      0,
      1,
      2,
      3,
      7,
      15,
      31,
      255,
      1024,
      std::numeric_limits<int16_t>::max() - 1,
      std::numeric_limits<int16_t>::max(),
  };

  std::mt19937 rng(0x4242);
  std::uniform_int_distribution<int16_t> dist(std::numeric_limits<int16_t>::min(), std::numeric_limits<int16_t>::max());
  for (int i = 0; i < 64; ++i) {
    values.emplace_back(dist(rng));
  }

  for (auto a : values) {
    for (auto b : values) {
      check_unbounded_mult_pair(a, b);
    }
  }
}

TEST(CrossTest, UInt64MatchesPositiveLconstReference) {
  auto values = random_unsigned_values();

  for (auto a : values) {
    for (auto b : values) {
      check_uint_pair(a, b);
    }
  }
}

TEST(CrossTest, SInt64MatchesWrappedNativeExpectations) {
  auto values = random_signed_values();

  for (auto a : values) {
    for (auto b : values) {
      check_sint_pair(a, b);
    }
  }
}
