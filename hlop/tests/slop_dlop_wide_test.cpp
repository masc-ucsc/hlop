//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// Wide (500-bit) differential test for every basic *_op.
//
// Two independent checks run here:
//
//   1. Dlop vs Slop, bit-for-bit. The same ~500-bit *concrete* value (no
//      unknowns) is built as a runtime-sized Dlop and as a fixed-width
//      Slop<1100>, the same op is applied on both, and every result bit must
//      match. This catches per-class wiring/sizing bugs across the 64-bit word
//      boundary.
//
//   2. Algebraic invariants that do NOT rely on Dlop and Slop agreeing — they
//      could share a Blop bug. The strongest is the division "multiply-back"
//      identity  a == (a/b)*b + (a%b)  with |a%b| < |b|, checked on both
//      classes at 500 bits, plus a handful of hand-computed div/mod/popcount
//      cases with known answers.
//
// Slop<1100> gives headroom for the widest result: two ~500-bit operands
// multiplied/concatenated need ~1000 bits.

#include <cstdint>
#include <random>
#include <string>
#include <vector>

#include "dlop.hpp"
#include "gtest/gtest.h"
#include "slop.hpp"

namespace {

constexpr int      kInputWidth = 500;   // significant signed width of operands
constexpr int      kSlopWidth  = 1100;  // > 2*kInputWidth: room for mult/concat
constexpr int      kPoolSize   = 54;
constexpr int      kIterations = 12000;
constexpr uint64_t kSeed       = 0x500B175ULL;

using S = Slop<kSlopWidth>;

// A pool of concrete signed values, each an MSB-first bit string of width
// kInputWidth (char[0] is the sign bit for the "0sb" parser). Effective
// magnitudes are mixed: some span the full 500 bits, others are sign-extended
// from a much narrower width. Mixing widths is what exercises divmodn's
// large-quotient and unequal-operand-width paths (a 500-bit value over an
// 8-bit value yields a ~492-bit quotient).
std::vector<std::string> BuildPool() {
  std::mt19937_64          rng{kSeed};
  std::vector<std::string> pool;
  pool.reserve(kPoolSize);
  const int eff_widths[] = {kInputWidth, 300, 130, 65, 64, 33, 8, 3, 1};
  for (int i = 0; i < kPoolSize; ++i) {
    int eff = eff_widths[i % (sizeof(eff_widths) / sizeof(eff_widths[0]))];
    // Randomize the low `eff` bits; sign-extend the rest from bit eff-1.
    std::string bits(kInputWidth, '0');
    for (int b = 0; b < eff; ++b) {
      bits[kInputWidth - 1 - b] = (rng() & 1) ? '1' : '0';
    }
    char sign = bits[kInputWidth - eff];  // bit eff-1 = the sign of the narrow value
    for (int b = eff; b < kInputWidth; ++b) {
      bits[kInputWidth - 1 - b] = sign;
    }
    pool.push_back(std::move(bits));
  }
  // A few crafted patterns that stress word boundaries and sign handling.
  pool.emplace_back(kInputWidth, '0');                       // 0
  pool.emplace_back(kInputWidth, '1');                       // -1 (all ones)
  pool.emplace_back("01" + std::string(kInputWidth - 2, '0'));  // large positive 2^498
  {
    std::string m(kInputWidth, '0');  // 0sb0...01...1 mask of 64 ones at a word boundary
    for (int b = kInputWidth - 64; b < kInputWidth; ++b) {
      m[b] = '1';
    }
    pool.push_back(std::move(m));
  }
  return pool;
}

S    MakeSlop(const std::string& bits) { return S::from_pyrope("0sb" + bits); }
auto MakeDlop(const std::string& bits) { return Dlop::from_pyrope("0sb" + bits); }

// Concrete values only — Dlop has no unknowns here, so bit_test (which sign-
// extends past the stored width) gives an exact comparison.
void ExpectEqual(const Dlop& d, const S& s, const std::string& tag) {
  ASSERT_FALSE(d.is_invalid()) << tag << ": dlop returned Invalid";
  int w = std::max(d.get_bits(), s.get_bits()) + 2;
  for (int pos = 0; pos < w; ++pos) {
    bool db = d.bit_test(pos);
    bool sb = s.bit_test(pos);
    ASSERT_EQ(db, sb) << tag << ": bit " << pos << " mismatch (dlop=" << db << " slop=" << sb << ")";
  }
}

void RunOnce(std::mt19937_64& rng, const std::vector<std::string>& pool, int op_idx) {
  const auto& ba = pool[rng() % pool.size()];
  const auto& bb = pool[rng() % pool.size()];

  auto sa = MakeSlop(ba);
  auto sb = MakeSlop(bb);
  auto da = MakeDlop(ba);
  auto db = MakeDlop(bb);

  switch (op_idx) {
    case 0:  ExpectEqual(*da->and_op(*db), sa.and_op(sb), "and_op"); break;
    case 1:  ExpectEqual(*da->or_op(*db), sa.or_op(sb), "or_op"); break;
    case 2:  ExpectEqual(*da->xor_op(*db), sa.xor_op(sb), "xor_op"); break;
    case 3:  ExpectEqual(*da->not_op(), sa.not_op(), "not_op"); break;
    case 4:  ExpectEqual(*da->neg_op(), sa.neg_op(), "neg_op"); break;
    case 5:  ExpectEqual(*da->add_op(*db), sa.add_op(sb), "add_op"); break;
    case 6:  ExpectEqual(*da->sub_op(*db), sa.sub_op(sb), "sub_op"); break;
    case 7:  ExpectEqual(*da->mult_op(*db), sa.mult_op(sb), "mult_op"); break;
    case 8: {
      if (sb.is_known_false()) {
        break;  // skip divide-by-zero
      }
      ExpectEqual(*da->div_op(*db), sa.div_op(sb), "div_op");
      break;
    }
    case 9: {
      if (sb.is_known_false()) {
        break;
      }
      ExpectEqual(*da->mod_op(*db), sa.mod_op(sb), "mod_op");
      break;
    }
    case 10: {
      int amt = rng() % 500;  // 500 + 500 = 1000 < kSlopWidth
      ExpectEqual(*da->shl_op(Dlop::create_integer(amt)), sa.shl_op(amt), "shl_op(" + std::to_string(amt) + ")");
      break;
    }
    case 11: {
      int amt = rng() % 600;
      ExpectEqual(*da->sra_op(Dlop::create_integer(amt)), sa.sra_op(amt), "sra_op(" + std::to_string(amt) + ")");
      break;
    }
    case 12: ExpectEqual(*da->eq_op(*db), sa.eq_op(sb), "eq_op"); break;
    case 13: ExpectEqual(*da->lt_op(*db), sa.lt_op(sb), "lt_op"); break;
    case 14: ExpectEqual(*da->le_op(*db), sa.le_op(sb), "le_op"); break;
    case 15: ExpectEqual(*da->gt_op(*db), sa.gt_op(sb), "gt_op"); break;
    case 16: ExpectEqual(*da->ge_op(*db), sa.ge_op(sb), "ge_op"); break;
    case 17: {
      int from = 1 + (rng() % 600);
      ExpectEqual(*da->sext_op(Dlop::create_integer(from)), sa.sext_op(from), "sext_op(" + std::to_string(from) + ")");
      break;
    }
    case 18: ExpectEqual(*da->ror_op(), sa.ror_op(), "ror_op_unary"); break;
    case 19: ExpectEqual(*da->rand_op(), sa.rand_op(), "rand_op_unary"); break;
    case 20: ExpectEqual(*da->rxor_op(), sa.rxor_op(), "rxor_op_unary"); break;
    case 21: ExpectEqual(*da->ror_op(*db), sa.ror_op(sb), "ror_op_binary"); break;
    case 22: {
      // Dlop::popcount_op returns a 1-bit unknown for negative inputs (a
      // negative value sign-extends with unbounded set bits), while Slop counts
      // the bits in its fixed width. Only compare for non-negative operands,
      // where both yield the exact count.
      if (!sa.is_negative()) {
        ExpectEqual(*da->popcount_op(), sa.popcount_op(), "popcount_op");
      }
      break;
    }
    case 23: ExpectEqual(*da->get_mask_op(), sa.get_mask_op(), "get_mask_op_unary"); break;
    case 24: ExpectEqual(*da->get_mask_op(*db), sa.get_mask_op(sb), "get_mask_op(mask)"); break;
    case 25: {
      // Lconst::set_mask_op is only defined for a non-negative source and value
      // (its general path asserts !is_negative() on both; the negative cases are
      // FIXME/undefined). The mask may be negative. Use magnitudes for src/val.
      const auto& bv     = pool[rng() % pool.size()];
      S           src_s  = sa.is_negative() ? sa.neg_op() : sa;
      auto        src_d  = da->is_negative() ? da->neg_op() : da->add_op(*Dlop::create_integer(0));
      auto        vraw_s = MakeSlop(bv);
      auto        vraw_d = MakeDlop(bv);
      S           val_s  = vraw_s.is_negative() ? vraw_s.neg_op() : vraw_s;
      auto        val_d  = vraw_d->is_negative() ? vraw_d->neg_op() : vraw_d->add_op(*Dlop::create_integer(0));
      ExpectEqual(*src_d->set_mask_op(*db, *val_d), src_s.set_mask_op(sb, val_s), "set_mask_op");
      break;
    }
    case 26: ExpectEqual(*da->concat_op(*db), sa.concat_op(sb), "concat_op"); break;
  }
}

}  // namespace

constexpr int kOpCount = 27;

TEST(SlopDlopWide, fuzz_all_ops_500bit) {
  auto            pool = BuildPool();
  std::mt19937_64 rng{kSeed ^ 0xA5A5A5A5ULL};
  for (int i = 0; i < kIterations; ++i) {
    RunOnce(rng, pool, i % kOpCount);
    if (::testing::Test::HasFailure()) {
      ADD_FAILURE() << "Failing iteration " << i << " op=" << (i % kOpCount);
      return;
    }
  }
}

// ---------------------------------------------------------------------------
// Algebraic invariants that do not depend on Dlop and Slop merely agreeing.
// ---------------------------------------------------------------------------

// |x| for both classes (neg if negative). The Dlop identity-copy goes through
// add_op(0) so the positive branch also yields a fresh spool_ptr<Dlop>.
static S               SlopAbs(const S& x) { return x.is_negative() ? x.neg_op() : x; }
static spool_ptr<Dlop> DlopAbs(const Dlop& x) {
  return x.is_negative() ? x.neg_op() : x.add_op(*Dlop::create_integer(0));
}

// a == (a/b)*b + (a%b) and |a%b| < |b|, verified entirely within Slop.
static void CheckDivModInvariantSlop(const S& a, const S& b, const std::string& tag) {
  ASSERT_FALSE(b.is_known_false()) << tag;
  S q     = a.div_op(b);
  S r     = a.mod_op(b);
  S recon = q.mult_op(b).add_op(r);
  EXPECT_TRUE(recon.is_known_eq(a)) << tag << ": Slop a != q*b+r";
  EXPECT_TRUE(SlopAbs(r).lt_op(SlopAbs(b)).is_known_true()) << tag << ": Slop |r| !< |b|";
  // Remainder sign follows the dividend (or is zero).
  if (!r.is_known_false()) {
    EXPECT_EQ(r.is_negative(), a.is_negative()) << tag << ": Slop rem sign";
  }
}

// Same invariant within Dlop.
static void CheckDivModInvariantDlop(const Dlop& a, const Dlop& b, const std::string& tag) {
  ASSERT_FALSE(b.is_known_false()) << tag;
  auto q     = a.div_op(b);
  auto r     = a.mod_op(b);
  auto recon = q->mult_op(b)->add_op(*r);
  EXPECT_TRUE(recon->is_known_eq(a)) << tag << ": Dlop a != q*b+r";
  EXPECT_TRUE(DlopAbs(*r)->lt_op(*DlopAbs(b))->is_known_true()) << tag << ": Dlop |r| !< |b|";
  if (!r->is_known_false()) {
    EXPECT_EQ(r->is_negative(), a.is_negative()) << tag << ": Dlop rem sign";
  }
}

TEST(SlopDlopWide, divmod_multiply_back_invariant) {
  std::mt19937_64 rng{kSeed ^ 0x1234ULL};
  auto            pool = BuildPool();
  for (int i = 0; i < 2000; ++i) {
    const auto& ba = pool[rng() % pool.size()];
    const auto& bb = pool[rng() % pool.size()];
    auto        sb = MakeSlop(bb);
    if (sb.is_known_false()) {
      continue;
    }
    auto sa  = MakeSlop(ba);
    auto da  = MakeDlop(ba);
    auto db  = MakeDlop(bb);
    auto tag = "iter" + std::to_string(i);
    CheckDivModInvariantSlop(sa, sb, tag);
    CheckDivModInvariantDlop(*da, *db, tag);
    if (::testing::Test::HasFailure()) {
      return;
    }
  }
}

// Hand-computed division/modulo with answers known a-priori, spanning multiple
// 64-bit words and every sign combination.
TEST(SlopDlopWide, divmod_known_answers) {
  // 2^k helpers.
  auto slop_pow2 = [](int k) { return S::create_integer(1).shl_op(k); };
  auto dlop_pow2 = [](int k) { return Dlop::create_integer(1)->shl_op(Dlop::create_integer(k)); };

  struct Case {
    int     an, ar;   // dividend = 2^an + ar
    int     bn;       // divisor  = 2^bn
    bool    neg_a, neg_b;
    int     exp_qn;   // |quotient| = 2^exp_qn
    int64_t exp_r;    // remainder (signed)
  };
  // 2^200 / 2^100 = 2^100 r 0 ;  (2^200 + ar)/2^100 = 2^100 r ar  (ar < 2^100)
  const Case cases[] = {
      {200, 0, 100, false, false, 100, 0},
      {200, 7, 100, false, false, 100, 7},
      {200, 7, 100, true, false, 100, -7},
      {200, 7, 100, false, true, 100, 7},
      {200, 7, 100, true, true, 100, -7},
      {300, 123456789, 64, false, false, 300 - 64, 123456789},
  };

  for (const auto& c : cases) {
    std::string tag = std::format("a=2^{}+{} b=2^{} na={} nb={}", c.an, c.ar, c.bn, c.neg_a, c.neg_b);

    S a_s = slop_pow2(c.an).add_op(S::create_integer(c.ar));
    S b_s = slop_pow2(c.bn);
    if (c.neg_a) {
      a_s = a_s.neg_op();
    }
    if (c.neg_b) {
      b_s = b_s.neg_op();
    }
    S exp_q_s = slop_pow2(c.exp_qn);
    if (c.neg_a != c.neg_b) {
      exp_q_s = exp_q_s.neg_op();
    }
    EXPECT_TRUE(a_s.div_op(b_s).is_known_eq(exp_q_s)) << "Slop quotient " << tag;
    EXPECT_TRUE(a_s.mod_op(b_s).is_known_eq(S::create_integer(c.exp_r))) << "Slop remainder " << tag;

    auto a_d = dlop_pow2(c.an)->add_op(Dlop::create_integer(c.ar));
    auto b_d = dlop_pow2(c.bn);
    if (c.neg_a) {
      a_d = a_d->neg_op();
    }
    if (c.neg_b) {
      b_d = b_d->neg_op();
    }
    auto exp_q_d = dlop_pow2(c.exp_qn);
    if (c.neg_a != c.neg_b) {
      exp_q_d = exp_q_d->neg_op();
    }
    EXPECT_TRUE(a_d->div_op(*b_d)->is_known_eq(*exp_q_d)) << "Dlop quotient " << tag;
    EXPECT_TRUE(a_d->mod_op(*b_d)->is_known_eq(*Dlop::create_integer(c.exp_r))) << "Dlop remainder " << tag;
  }
}

// Boundary div/mod cases: ±1 divisor, zero dividend, and a divisor strictly
// wider than the dividend (quotient 0, remainder == dividend).
TEST(SlopDlopWide, divmod_edge_cases) {
  auto slop_pow2 = [](int k) { return S::create_integer(1).shl_op(k); };
  auto dlop_pow2 = [](int k) { return Dlop::create_integer(1)->shl_op(Dlop::create_integer(k)); };

  // big = 2^480 + 0x1234, a wide positive value.
  S    big_s = slop_pow2(480).add_op(S::create_integer(0x1234));
  auto big_d = dlop_pow2(480)->add_op(Dlop::create_integer(0x1234));

  // x / 1 == x, x % 1 == 0  (and the negative-divisor mirror).
  for (int sign : {1, -1}) {
    S    one_s = S::create_integer(sign);
    auto one_d = Dlop::create_integer(sign);
    S    eq_s  = (sign < 0) ? big_s.neg_op() : big_s;
    auto eq_d  = (sign < 0) ? big_d->neg_op() : big_d->add_op(*Dlop::create_integer(0));
    EXPECT_TRUE(big_s.div_op(one_s).is_known_eq(eq_s)) << "Slop x/" << sign;
    EXPECT_TRUE(big_s.mod_op(one_s).is_known_eq(S::create_integer(0))) << "Slop x%" << sign;
    EXPECT_TRUE(big_d->div_op(*one_d)->is_known_eq(*eq_d)) << "Dlop x/" << sign;
    EXPECT_TRUE(big_d->mod_op(*one_d)->is_known_eq(*Dlop::create_integer(0))) << "Dlop x%" << sign;
  }

  // 0 / big == 0, 0 % big == 0.
  EXPECT_TRUE(S::create_integer(0).div_op(big_s).is_known_eq(S::create_integer(0))) << "Slop 0/big";
  EXPECT_TRUE(S::create_integer(0).mod_op(big_s).is_known_eq(S::create_integer(0))) << "Slop 0%big";
  EXPECT_TRUE(Dlop::create_integer(0)->div_op(*big_d)->is_known_eq(*Dlop::create_integer(0))) << "Dlop 0/big";
  EXPECT_TRUE(Dlop::create_integer(0)->mod_op(*big_d)->is_known_eq(*Dlop::create_integer(0))) << "Dlop 0%big";

  // small / big == 0, small % big == small  (divisor wider than dividend).
  S    small_s = S::create_integer(0x9abc);
  auto small_d = Dlop::create_integer(0x9abc);
  EXPECT_TRUE(small_s.div_op(big_s).is_known_eq(S::create_integer(0))) << "Slop small/big";
  EXPECT_TRUE(small_s.mod_op(big_s).is_known_eq(small_s)) << "Slop small%big";
  EXPECT_TRUE(small_d->div_op(*big_d)->is_known_eq(*Dlop::create_integer(0))) << "Dlop small/big";
  EXPECT_TRUE(small_d->mod_op(*big_d)->is_known_eq(*small_d)) << "Dlop small%big";
}

// Shifting right/left by more than the value width: a robustness check for the
// over-shift path in Blop's shift primitives.
TEST(SlopDlopWide, overshift) {
  // Negative ~500-bit value: sra past the width must yield all-ones (-1).
  auto neg_d = Dlop::create_integer(-1)->shl_op(Dlop::create_integer(450));  // ...111 000...0
  S    neg_s = S::create_integer(-1).shl_op(450);
  for (int amt : {500, 700, 1000}) {
    ExpectEqual(*neg_d->sra_op(Dlop::create_integer(amt)), neg_s.sra_op(amt), "sra_overshift(" + std::to_string(amt) + ")");
  }
  // Positive value: sra past the width yields 0; shl past it yields 0.
  auto pos_d = Dlop::create_integer(1)->shl_op(Dlop::create_integer(480));
  S    pos_s = S::create_integer(1).shl_op(480);
  ExpectEqual(*pos_d->sra_op(Dlop::create_integer(900)), pos_s.sra_op(900), "sra_overshift_pos");
}

// popcount over >64 bits, with answers known a-priori.
TEST(SlopDlopWide, popcount_known_answers) {
  // Value with exactly 5 set bits at positions 0,100,200,300,400.
  S    five_s = S::create_integer(0);
  auto five_d = Dlop::create_integer(0);
  for (int pos : {0, 100, 200, 300, 400}) {
    five_s = five_s.or_op(S::create_integer(1).shl_op(pos));
    five_d = five_d->or_op(Dlop::create_integer(1)->shl_op(Dlop::create_integer(pos)));
  }
  EXPECT_TRUE(five_s.popcount_op().is_known_eq(S::create_integer(5))) << "Slop popcount==5";
  EXPECT_TRUE(five_d->popcount_op()->is_known_eq(*Dlop::create_integer(5))) << "Dlop popcount==5";

  // 2^300 - 1 is 300 consecutive ones.
  S    ones_s = S::create_integer(1).shl_op(300).sub_op(S::create_integer(1));
  auto ones_d = Dlop::create_integer(1)->shl_op(Dlop::create_integer(300))->sub_op(*Dlop::create_integer(1));
  EXPECT_TRUE(ones_s.popcount_op().is_known_eq(S::create_integer(300))) << "Slop popcount==300";
  EXPECT_TRUE(ones_d->popcount_op()->is_known_eq(*Dlop::create_integer(300))) << "Dlop popcount==300";
}

// Regression tests for bugs surfaced by the adversarial review of the
// multi-word arithmetic changes.

// Division by ±1 is the one quotient that can overflow the dividend's width.
// Dlop is arbitrary precision, so most-negative / -1 must WIDEN (never wrap or
// hit the INT64_MIN/-1 signed-overflow UB of the scalar path).
TEST(SlopDlopWide, dlop_div_by_pm1_no_overflow) {
  auto one  = Dlop::create_integer(1);
  auto mone = Dlop::create_integer(-1);

  // -2^63 / -1 == +2^63 (single-word dividend widens to two words; no UB).
  auto min63 = Dlop::create_integer(INT64_MIN);                              // -2^63
  auto pos63 = Dlop::create_integer(1)->shl_op(Dlop::create_integer(63));    // +2^63
  EXPECT_TRUE(min63->div_op(*mone)->is_known_eq(*pos63)) << "-2^63 / -1";
  EXPECT_FALSE(min63->div_op(*mone)->is_negative()) << "-2^63 / -1 must be positive";
  EXPECT_TRUE(min63->mod_op(*mone)->is_known_eq(*Dlop::create_integer(0))) << "-2^63 % -1";

  // -2^200 / -1 == +2^200 (multi-word dividend widens by a word).
  auto min200 = Dlop::create_integer(1)->shl_op(Dlop::create_integer(200))->neg_op();
  auto pos200 = Dlop::create_integer(1)->shl_op(Dlop::create_integer(200));
  EXPECT_TRUE(min200->div_op(*mone)->is_known_eq(*pos200)) << "-2^200 / -1";
  EXPECT_FALSE(min200->div_op(*mone)->is_negative()) << "-2^200 / -1 must be positive";

  // Ordinary value: x/1 == x, x/-1 == -x, x%(±1) == 0.
  auto v = Dlop::create_integer(1)->shl_op(Dlop::create_integer(130))->add_op(*Dlop::create_integer(7));
  EXPECT_TRUE(v->div_op(*one)->is_known_eq(*v)) << "x/1";
  EXPECT_TRUE(v->div_op(*mone)->is_known_eq(*v->neg_op())) << "x/-1";
  EXPECT_TRUE(v->mod_op(*one)->is_known_eq(*Dlop::create_integer(0))) << "x%1";
  EXPECT_TRUE(v->mod_op(*mone)->is_known_eq(*Dlop::create_integer(0))) << "x%-1";
}

// Single-word Slop (n_words==1) must not invoke int64 UB nor assert on
// over-shift; results follow fixed-width modular semantics.
TEST(SlopDlopWide, slop_single_word_edges) {
  // INT64_MIN / -1 wraps to INT64_MIN (modular), INT64_MIN % -1 == 0 — no UB.
  Slop<64> mn = Slop<64>::create_integer(INT64_MIN);
  Slop<64> m1 = Slop<64>::create_integer(-1);
  EXPECT_TRUE(mn.div_op(m1).is_known_eq(mn)) << "Slop<64> INT_MIN/-1 wraps to INT_MIN";
  EXPECT_TRUE(mn.mod_op(m1).is_known_eq(Slop<64>::create_integer(0))) << "Slop<64> INT_MIN%-1 == 0";

  // Over-shift on a single-word Slop: shl past width clears; arithmetic sra past
  // width fills with the sign bit.
  Slop<32> five = Slop<32>::create_integer(5);
  EXPECT_TRUE(five.shl_op(64).is_known_eq(Slop<32>::create_integer(0))) << "Slop<32> 5<<64 == 0";
  Slop<32> negv = Slop<32>::create_integer(-8);
  EXPECT_TRUE(negv.sra_op(100).is_known_eq(Slop<32>::create_integer(-1))) << "Slop<32> (-8)>>100 == -1";
  Slop<32> posv = Slop<32>::create_integer(8);
  EXPECT_TRUE(posv.sra_op(100).is_known_eq(Slop<32>::create_integer(0))) << "Slop<32> 8>>100 == 0";
}
