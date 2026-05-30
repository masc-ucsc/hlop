//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// Differential test: every op in Dlop should produce a result consistent
// with the corresponding op in Slop, for any concrete realization of Dlop's
// unknowns.
//
// Property: pick two 127-bit values A_c, B_c (concrete). Build Slop<256>
// values from them. Build Dlop values from the same bit patterns but with
// some bits stripped to '?'. Apply the same op on both sides. Every known
// bit in the Dlop result must equal the corresponding bit in the Slop
// result. Unknown bits in Dlop are unconstrained — Slop can hold anything
// there.

#include <cstdint>
#include <random>
#include <string>
#include <vector>

#include "dlop.hpp"
#include "gtest/gtest.h"
#include "slop.hpp"

namespace {

constexpr int      kSlopWidth      = 256;  // headroom for 127×127-bit MULT.
constexpr int      kInputWidth     = 127;
constexpr int      kPoolSize       = 32;
constexpr int      kUnknownChanceN = 8;  // ~12.5% of bits become '?' in Dlop.
constexpr int      kIterations     = 8000;
constexpr uint64_t kSeed           = 0xD1FFEEULL;

using S = Slop<kSlopWidth>;

// One entry in the pre-generated pool: the same 127-bit value expressed two
// ways. `concrete` is all '0'/'1'; `masked` agrees on '0'/'1' positions but
// has some '?' bits substituted in.
struct PoolEntry {
  std::string concrete;  // 127 chars, '0'/'1' only, MSB-first
  std::string masked;    // 127 chars, '0'/'1'/'?', MSB-first
};

std::vector<PoolEntry> BuildPool() {
  std::mt19937_64        rng{kSeed};
  std::vector<PoolEntry> pool;
  pool.reserve(kPoolSize);
  for (int i = 0; i < kPoolSize; ++i) {
    PoolEntry e;
    e.concrete.reserve(kInputWidth);
    e.masked.reserve(kInputWidth);
    for (int b = 0; b < kInputWidth; ++b) {
      char c = (rng() & 1) ? '1' : '0';
      e.concrete.push_back(c);
      // The first char (MSB) is the sign indicator in "0sb…" — keep it
      // known so ops whose result width depends on get_bits() (concat,
      // get_mask, …) agree between Dlop and Slop. Unknowns inside the
      // bit field still exercise every propagation path.
      bool can_unknown = (b > 0);
      e.masked.push_back((can_unknown && (rng() % kUnknownChanceN) == 0) ? '?' : c);
    }
    pool.push_back(std::move(e));
  }
  return pool;
}

// Tri-bit accessor for one position of a Dlop. Returns '0', '1', or '?'.
// Reads base()/extra() directly so sign-extension and unknown-extension are
// handled uniformly: positions beyond the stored words sign-extend the top
// word's base (the unknown-bit at the sign position is, by Dlop convention,
// the unknown sign — but we treat *higher* positions as the sign extension
// of base only, which matches bit_test()).
char DlopTriBit(const Dlop& d, int pos) {
  int  word = pos / 64;
  int  bit  = pos % 64;
  bool b, u;
  if (word >= d.size) {
    b = d.base()[d.size - 1] < 0;
    // Sign extension of the *unknown* mask: if the top stored extra word is
    // negative (i.e. the top stored bit is unknown), the extension is also
    // unknown — otherwise it's known and matches the sign of base.
    u = d.extra()[d.size - 1] < 0;
  } else {
    b = (d.base()[word] >> bit) & 1;
    u = (d.extra()[word] >> bit) & 1;
  }
  return u ? '?' : (b ? '1' : '0');
}

// Slop has no unknowns — just bit_test.
char SlopBit(const S& s, int pos) { return s.bit_test(pos) ? '1' : '0'; }

// Soundness check: every known bit in d must match the matching bit in s.
void ExpectConsistent(const Dlop& d, const S& s, const std::string& tag) {
  ASSERT_FALSE(d.is_invalid()) << "Dlop returned Invalid for " << tag;
  // Compare bit positions [0, max+pad). bit_test sign-extends beyond the
  // natural width so any reasonable upper bound works.
  int w = std::max(d.get_bits(), s.get_bits()) + 2;
  for (int pos = 0; pos < w; ++pos) {
    char dc = DlopTriBit(d, pos);
    char sc = SlopBit(s, pos);
    if (dc == '?') {
      continue;
    }
    ASSERT_EQ(dc, sc) << tag << ": bit " << pos << " mismatch (dlop=" << dc << " slop=" << sc << ")";
  }
}

// Single iteration: pick two pool entries and exercise one op on both sides.
// Bitwise + shift + masked-extract are bit-local. add_op propagates the
// carry chain by forcing every bit at or above the lowest unknown input bit
// to be unknown in the result.
void RunOnce(std::mt19937_64& rng, const std::vector<PoolEntry>& pool, int op_idx) {
  const auto& ea = pool[rng() % pool.size()];
  const auto& eb = pool[rng() % pool.size()];

  // The "0sb" prefix tells from_pyrope/from_binary to parse as signed
  // binary; the leading bit becomes the sign.
  auto sa = S::from_pyrope("0sb" + ea.concrete);
  auto sb = S::from_pyrope("0sb" + eb.concrete);
  auto da = Dlop::from_pyrope("0sb" + ea.masked);
  auto db = Dlop::from_pyrope("0sb" + eb.masked);

  switch (op_idx) {
    case 0: ExpectConsistent(*da->and_op(*db), sa.and_op(sb), "and_op"); break;
    case 1: ExpectConsistent(*da->or_op(*db), sa.or_op(sb), "or_op"); break;
    case 2: ExpectConsistent(*da->xor_op(*db), sa.xor_op(sb), "xor_op"); break;
    case 3: {
      // Shift by a small known amount so the result fits in Slop<256>.
      int amt = rng() % 60;
      ExpectConsistent(*da->shl_op(amt), sa.shl_op(amt), "shl_op(" + std::to_string(amt) + ")");
      break;
    }
    case 4: {
      int amt = rng() % 60;
      ExpectConsistent(*da->sra_op(amt), sa.sra_op(amt), "sra_op(" + std::to_string(amt) + ")");
      break;
    }
    case 5: {
      // get_mask_op with a concrete (no-unknown) mask — exercises the
      // unknown-propagation fix we just added.
      auto mask_d = Dlop::create_integer((int64_t(1) << 60) - 1);
      auto mask_s = S::create_integer((int64_t(1) << 60) - 1);
      ExpectConsistent(*da->get_mask_op(*mask_d), sa.get_mask_op(mask_s), "get_mask_op");
      break;
    }
    case 6: ExpectConsistent(*da->add_op(*db), sa.add_op(sb), "add_op"); break;
    case 7: ExpectConsistent(*da->sub_op(*db), sa.sub_op(sb), "sub_op"); break;
    case 8: ExpectConsistent(*da->neg_op(), sa.neg_op(), "neg_op"); break;
    case 9: ExpectConsistent(*da->mult_op(*db), sa.mult_op(sb), "mult_op"); break;
    case 10: ExpectConsistent(*da->eq_op(*db), sa.eq_op(sb), "eq_op"); break;
    case 11: ExpectConsistent(*da->lt_op(*db), sa.lt_op(sb), "lt_op"); break;
    case 12: ExpectConsistent(*da->le_op(*db), sa.le_op(sb), "le_op"); break;
    case 13: ExpectConsistent(*da->gt_op(*db), sa.gt_op(sb), "gt_op"); break;
    case 14: ExpectConsistent(*da->ge_op(*db), sa.ge_op(sb), "ge_op"); break;
    case 15: ExpectConsistent(*da->not_op(), sa.not_op(), "not_op"); break;
    case 16: {
      int from = 1 + (rng() % 100);  // sign-extend from bit `from`
      ExpectConsistent(*da->sext_op(from), sa.sext_op(from), "sext_op(" + std::to_string(from) + ")");
      break;
    }
    case 17: ExpectConsistent(*da->ror_op(), sa.ror_op(), "ror_op_unary"); break;
    case 18: ExpectConsistent(*da->rand_op(), sa.rand_op(), "rand_op_unary"); break;
    case 19: ExpectConsistent(*da->rxor_op(), sa.rxor_op(), "rxor_op_unary"); break;
    case 20: {
      // Shift amount is itself a Dlop/Slop value (small concrete int).
      int64_t amt   = rng() % 60;
      auto    amt_d = Dlop::create_integer(amt);
      auto    amt_s = S::create_integer(amt);
      ExpectConsistent(*da->shl_op(*amt_d), sa.shl_op(amt_s), "shl_op(Dlop=" + std::to_string(amt) + ")");
      break;
    }
    case 21: {
      int64_t amt   = rng() % 60;
      auto    amt_d = Dlop::create_integer(amt);
      auto    amt_s = S::create_integer(amt);
      ExpectConsistent(*da->sra_op(*amt_d), sa.sra_op(amt_s), "sra_op(Dlop=" + std::to_string(amt) + ")");
      break;
    }
    case 22: {
      // get_mask_op where the mask itself may have unknowns.
      ExpectConsistent(*da->get_mask_op(*db), sa.get_mask_op(sb), "get_mask_op(Dlop)");
      break;
    }
    case 23: {
      // set_mask_op: replace bits in da selected by mask with bits from value.
      const auto& ev     = pool[rng() % pool.size()];
      auto        mask_d = Dlop::create_integer((int64_t(1) << 50) - 1);
      auto        mask_s = S::create_integer((int64_t(1) << 50) - 1);
      auto        val_d  = Dlop::from_pyrope("0sb" + ev.masked);
      auto        val_s  = S::from_pyrope("0sb" + ev.concrete);
      ExpectConsistent(*da->set_mask_op(*mask_d, *val_d), sa.set_mask_op(mask_s, val_s), "set_mask_op");
      break;
    }
    case 24: {
      // div_op needs single-word values (multi-word divn not implemented in
      // blop.hpp). Use a small (~60-bit) signed numerator/denominator with
      // a non-zero divisor. Denominator built so it can't resolve to zero.
      int64_t num      = static_cast<int64_t>(rng()) >> 4;        // ~60 bits
      int64_t denom    = (static_cast<int64_t>(rng()) >> 4) | 1;  // non-zero
      auto    sa_small = S::create_integer(num);
      auto    sb_small = S::create_integer(denom);
      auto    da_small = Dlop::create_integer(num);
      auto    db_small = Dlop::create_integer(denom);
      ExpectConsistent(*da_small->div_op(*db_small), sa_small.div_op(sb_small), "div_op");
      break;
    }
    case 25: {
      int64_t num      = static_cast<int64_t>(rng()) >> 4;
      int64_t denom    = (static_cast<int64_t>(rng()) >> 4) | 1;
      auto    sa_small = S::create_integer(num);
      auto    sb_small = S::create_integer(denom);
      auto    da_small = Dlop::create_integer(num);
      auto    db_small = Dlop::create_integer(denom);
      ExpectConsistent(*da_small->mod_op(*db_small), sa_small.mod_op(sb_small), "mod_op");
      break;
    }
    case 26: {
      // concat_op width = lhs.get_bits() + rhs.get_bits(). With unknown bits
      // the effective width is concretization-dependent (Slop sees one width,
      // Dlop sees another). Only exercise concat on the no-unknowns slice of
      // the pool — the underlying lsh/or/get_mask are already covered.
      if (!da->has_unknowns() && !db->has_unknowns()) {
        ExpectConsistent(*da->concat_op(*db), sa.concat_op(sb), "concat_op");
      }
      break;
    }
  }
}

}  // namespace

TEST(SlopDlopDiff, fuzz_property) {
  auto            pool = BuildPool();
  std::mt19937_64 rng{kSeed ^ 0xA5A5A5A5ULL};
  constexpr int   kOpCount = 27;
  for (int i = 0; i < kIterations; ++i) {
    int op = i % kOpCount;
    RunOnce(rng, pool, op);
    if (::testing::Test::HasFailure()) {
      ADD_FAILURE() << "Failing iteration: " << i << " op=" << op;
      return;
    }
  }
}

// Smaller targeted tests — easier to debug than the fuzz loop when one op
// regresses.
TEST(SlopDlopDiff, and_or_xor_concrete_vs_masked) {
  // A simple known input pair to lock in the basic plumbing.
  auto sa = S::from_pyrope("0sb01010101");
  auto sb = S::from_pyrope("0sb00110011");
  auto da = Dlop::from_pyrope("0sb01?10101");
  auto db = Dlop::from_pyrope("0sb00110011");

  ExpectConsistent(*da->and_op(*db), sa.and_op(sb), "and");
  ExpectConsistent(*da->or_op(*db), sa.or_op(sb), "or");
  ExpectConsistent(*da->xor_op(*db), sa.xor_op(sb), "xor");
}

// add_op must propagate unknowns through the carry chain: any concretization
// of the unknown inputs must agree with Dlop's "known" output bits.
TEST(SlopDlopDiff, add_op_carry_chain_soundness) {
  // a = 0sb??11 — bits 0..1 are 1, bits 2..3 unknown. Concretizations
  // span {3, 7, -5, -1}; with b = 1 the sum spans {4, 8, -4, 0}. Only
  // bits 0..1 are invariant (both 0); bit 2 upward must be unknown.
  auto da = Dlop::from_pyrope("0sb??11");
  auto db = Dlop::from_pyrope("0sb0001");
  for (const char* concrete : {"0sb0011", "0sb0111", "0sb1011", "0sb1111"}) {
    auto sa = S::from_pyrope(concrete);
    auto sb = S::from_pyrope("0sb0001");
    ExpectConsistent(*da->add_op(*db), sa.add_op(sb), std::string("add_op@") + concrete);
  }
}
