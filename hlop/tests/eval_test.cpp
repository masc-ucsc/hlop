//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "eval.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <vector>

#include "dcontext.hpp"

// =========================================================================
// Slop kernel tests (static/generated path)
// =========================================================================

using V32 = Slop<32>;
using V64 = Slop<64>;
using V8  = Slop<8>;
using V1  = Slop<1>;

class EvalSlopTest : public ::testing::Test {};

// --- Pure single-sink ops ---

TEST_F(EvalSlopTest, eval_or) {
  std::array<V8, 3> ins{V8::from_pyrope("0ub0011"), V8::from_pyrope("0ub1000"), V8::from_pyrope("0ub0100")};
  auto              out = hlop::eval_or<V8>(ins);
  EXPECT_TRUE(out.is_known_eq(V8::from_pyrope("0ub1111")));
}

TEST_F(EvalSlopTest, eval_and) {
  std::array<V8, 2> ins{V8::from_pyrope("0ub1110"), V8::from_pyrope("0ub1011")};
  auto              out = hlop::eval_and<V8>(ins);
  EXPECT_TRUE(out.is_known_eq(V8::from_pyrope("0ub1010")));
}

TEST_F(EvalSlopTest, eval_xor) {
  std::array<V8, 2> ins{V8::from_pyrope("0ub1110"), V8::from_pyrope("0ub1011")};
  auto              out = hlop::eval_xor<V8>(ins);
  EXPECT_TRUE(out.is_known_eq(V8::from_pyrope("0ub0101")));
}

TEST_F(EvalSlopTest, eval_ror_true) {
  std::array<V8, 3> ins{V8::create_integer(0), V8::create_integer(5), V8::create_integer(0)};
  auto              out = hlop::eval_ror<V8>(ins);
  EXPECT_TRUE(out.is_known_true());
}

TEST_F(EvalSlopTest, eval_ror_false) {
  std::array<V8, 2> ins{V8::create_integer(0), V8::create_integer(0)};
  auto              out = hlop::eval_ror<V8>(ins);
  EXPECT_TRUE(out.is_known_false());
}

TEST_F(EvalSlopTest, eval_mult) {
  std::array<V32, 2> ins{V32::create_integer(6), V32::create_integer(7)};
  auto               out = hlop::eval_mult<V32>(ins);
  EXPECT_TRUE(out.is_known_eq(V32::create_integer(42)));
}

TEST_F(EvalSlopTest, eval_not) {
  auto out = hlop::eval_not(V8::from_pyrope("0ub1010"));
  // ~0b...1010 = 0b...0101 (sign-extended, so ~0x0a = 0xf5 in 8-bit = -11)
  EXPECT_TRUE(out.is_known_eq(V8::create_integer(~0x0a)));
}

TEST_F(EvalSlopTest, eval_div) {
  auto out = hlop::eval_div(V32::create_integer(42), V32::create_integer(6));
  EXPECT_TRUE(out.is_known_eq(V32::create_integer(7)));
}

TEST_F(EvalSlopTest, eval_lt_true) {
  auto out = hlop::eval_lt(V32::create_integer(3), V32::create_integer(5));
  EXPECT_TRUE(out.is_known_true());
}

TEST_F(EvalSlopTest, eval_lt_false) {
  auto out = hlop::eval_lt(V32::create_integer(5), V32::create_integer(3));
  EXPECT_TRUE(out.is_known_false());
}

TEST_F(EvalSlopTest, eval_eq_true) {
  auto out = hlop::eval_eq(V32::create_integer(42), V32::create_integer(42));
  EXPECT_TRUE(out.is_known_true());
}

TEST_F(EvalSlopTest, eval_eq_false) {
  auto out = hlop::eval_eq(V32::create_integer(42), V32::create_integer(43));
  EXPECT_TRUE(out.is_known_false());
}

TEST_F(EvalSlopTest, eval_sext) {
  // 0b1010 sign-extended from bit 3 -> 0b...11111010 = -6
  auto out = hlop::eval_sext(V32::from_pyrope("0ub1010"), 3);
  EXPECT_TRUE(out.is_known_eq(V32::create_integer(-6)));
}

TEST_F(EvalSlopTest, eval_shl) {
  auto out = hlop::eval_shl(V32::create_integer(1), V32::create_integer(4));
  EXPECT_TRUE(out.is_known_eq(V32::create_integer(16)));
}

TEST_F(EvalSlopTest, eval_sra) {
  auto out = hlop::eval_sra(V32::create_integer(-16), V32::create_integer(2));
  EXPECT_TRUE(out.is_known_eq(V32::create_integer(-4)));
}

TEST_F(EvalSlopTest, eval_set_mask_zero_mask) {
  auto base = V8::create_integer(0xFF);
  auto mask = V8::create_integer(0);
  auto val  = V8::create_integer(0xAA);
  auto out  = hlop::eval_set_mask(base, mask, val);
  EXPECT_TRUE(out.is_known_eq(base));
}

TEST_F(EvalSlopTest, eval_set_mask_low_nibble) {
  auto base = V32::from_pyrope("0xFFF");
  auto mask = V32::from_pyrope("0x0F");
  auto val  = V32::from_pyrope("0xa");
  auto out  = hlop::eval_set_mask(base, mask, val);
  EXPECT_TRUE(out.is_known_eq(V32::from_pyrope("0xFFa")));
}

// --- Multi-sink ops ---

TEST_F(EvalSlopTest, eval_sum) {
  std::array<V32, 2> plus{V32::create_integer(10), V32::create_integer(3)};
  std::array<V32, 1> minus{V32::create_integer(4)};
  auto               out = hlop::eval_sum<V32>({.plus = plus, .minus = minus});
  EXPECT_TRUE(out.is_known_eq(V32::create_integer(9)));
}

TEST_F(EvalSlopTest, eval_sum_plus_only) {
  std::array<V32, 3> plus{V32::create_integer(1), V32::create_integer(2), V32::create_integer(3)};
  std::array<V32, 0> minus{};
  auto               out = hlop::eval_sum<V32>({.plus = plus, .minus = minus});
  EXPECT_TRUE(out.is_known_eq(V32::create_integer(6)));
}

TEST_F(EvalSlopTest, eval_mux) {
  using SSel             = Slop<4>;
  SSel               sel = SSel::create_integer(2);
  std::array<V32, 3> data{V32::from_pyrope("0x11"), V32::from_pyrope("0x22"), V32::from_pyrope("0x33")};
  auto               out = hlop::eval_mux<SSel, V32>({.sel = sel, .data = data});
  EXPECT_TRUE(out.is_known_eq(V32::from_pyrope("0x33")));
}

TEST_F(EvalSlopTest, eval_mux_first) {
  V32                sel = V32::create_integer(0);
  std::array<V32, 3> data{V32::from_pyrope("0x11"), V32::from_pyrope("0x22"), V32::from_pyrope("0x33")};
  auto               out = hlop::eval_mux<V32, V32>({.sel = sel, .data = data});
  EXPECT_TRUE(out.is_known_eq(V32::from_pyrope("0x11")));
}

TEST_F(EvalSlopTest, eval_lut_basic) {
  // 2-input AND gate: truth table = 0b1000 = 8
  V8                lut_val = V8::create_integer(8);
  std::array<V8, 2> ins{V8::create_integer(1), V8::create_integer(1)};
  auto              out = hlop::eval_lut<V8>({.lut_val = lut_val, .inputs = ins});
  EXPECT_TRUE(out.is_known_true());

  // Input 0=1, 1=0 -> index=1 -> bit 1 of 0b1000 = 0
  std::array<V8, 2> ins2{V8::create_integer(1), V8::create_integer(0)};
  auto              out2 = hlop::eval_lut<V8>({.lut_val = lut_val, .inputs = ins2});
  EXPECT_TRUE(out2.is_known_false());
}

// --- Stateful ops ---

TEST_F(EvalSlopTest, eval_flop_basic) {
  hlop::RegState<V32> regs(4, V32::create_integer(0));

  V32 clk = V32::create_integer(1);
  V32 din = V32::create_integer(42);

  hlop::FlopArgs<V32> fargs{.din = din, .clock_pin = clk};
  auto                q = hlop::eval_flop<V32>(regs, 0, fargs);
  EXPECT_TRUE(q.is_known_eq(V32::create_integer(0)));  // current is still 0

  regs.advance_clock();

  V32                 din2 = V32::create_integer(99);
  hlop::FlopArgs<V32> fargs2{.din = din2, .clock_pin = clk};
  auto                q2 = hlop::eval_flop<V32>(regs, 0, fargs2);
  EXPECT_TRUE(q2.is_known_eq(V32::create_integer(42)));  // now visible after advance_clock
}

TEST_F(EvalSlopTest, eval_flop_enable_false) {
  hlop::RegState<V32> regs(4, V32::create_integer(0));
  V32                 clk = V32::create_integer(1);
  V32                 en  = V32::create_integer(0);
  V32                 din = V32::create_integer(42);

  hlop::FlopArgs<V32> fargs{.din = din, .clock_pin = clk, .enable = &en};
  hlop::eval_flop<V32>(regs, 0, fargs);
  regs.advance_clock();

  auto q = hlop::eval_flop<V32>(regs, 0, fargs);
  EXPECT_TRUE(q.is_known_eq(V32::create_integer(0)));  // enable was false, so no update
}

TEST_F(EvalSlopTest, eval_latch_transparent) {
  hlop::RegState<V32> regs(4, V32::create_integer(0));
  V32                 en  = V32::create_integer(1);
  V32                 din = V32::create_integer(42);

  hlop::LatchArgs<V32> largs{.din = din, .enable = en};
  auto                 q = hlop::eval_latch<V32>(regs, 0, largs);
  EXPECT_TRUE(q.is_known_eq(V32::create_integer(42)));  // transparent when enable is high
}

TEST_F(EvalSlopTest, eval_latch_opaque) {
  hlop::RegState<V32> regs(4, V32::create_integer(0));
  V32                 en_high = V32::create_integer(1);
  V32                 en_low  = V32::create_integer(0);
  V32                 din1    = V32::create_integer(42);
  V32                 din2    = V32::create_integer(99);

  hlop::LatchArgs<V32> largs1{.din = din1, .enable = en_high};
  hlop::eval_latch<V32>(regs, 0, largs1);

  hlop::LatchArgs<V32> largs2{.din = din2, .enable = en_low};
  auto                 q = hlop::eval_latch<V32>(regs, 0, largs2);
  EXPECT_TRUE(q.is_known_eq(V32::create_integer(42)));  // holds last value
}

// --- Memory ---

TEST_F(EvalSlopTest, eval_memory_write_read) {
  hlop::MemState<V32> mem(256, V32::create_integer(0), false);

  V32 addr = V32::create_integer(7);
  V32 data = V32::from_pyrope("0xdeadbeef");
  V32 en   = V32::create_integer(1);

  hlop::MemoryWriteArgs<V32> wargs{.addr = addr, .data = data, .enable = en};
  hlop::eval_memory_write<V32>(mem, wargs);

  // Before advance_clock, read should see old value (fwd=false)
  hlop::MemoryReadArgs<V32> rargs{.addr = addr, .enable = en};
  auto                      rd = hlop::eval_memory_read<V32>(mem, rargs);
  EXPECT_TRUE(rd.is_known_eq(V32::create_integer(0)));

  mem.advance_clock();

  auto rd2 = hlop::eval_memory_read<V32>(mem, rargs);
  EXPECT_TRUE(rd2.is_known_eq(V32::from_pyrope("0xdeadbeef")));
}

TEST_F(EvalSlopTest, eval_memory_fwd) {
  hlop::MemState<V32> mem(256, V32::create_integer(0), true);

  V32 addr     = V32::create_integer(7);
  V32 data     = V32::from_pyrope("0xcafe");
  V32 en       = V32::create_integer(1);
  V32 fwd_flag = V32::create_integer(1);

  hlop::MemoryWriteArgs<V32> wargs{.addr = addr, .data = data, .enable = en};
  hlop::eval_memory_write<V32>(mem, wargs);

  // With fwd=true, read should see pending write
  hlop::MemoryReadArgs<V32> rargs{.addr = addr, .enable = en, .fwd = &fwd_flag};
  auto                      rd = hlop::eval_memory_read<V32>(mem, rargs);
  EXPECT_TRUE(rd.is_known_eq(V32::from_pyrope("0xcafe")));
}

// =========================================================================
// DContext tests (dynamic/dlop path)
// =========================================================================

class EvalDlopTest : public ::testing::Test {
protected:
  hlop::DContext ctx;

  static hlop::DValue V(std::string_view txt) { return Dlop::from_pyrope(txt); }
  static hlop::DValue Vi(int64_t v) { return Dlop::create_integer(v); }
};

TEST_F(EvalDlopTest, or_basic) {
  hlop::DCall call{
      .op     = hlop::Ntype_op::Or,
      .inputs = {
          {.value = V("0ub0011")},
          {.value = V("0ub1000")},
          {.value = V("0ub0100")},
      },
  };
  auto res = ctx.execute(call);
  EXPECT_TRUE(res.outputs[0]->is_known_eq(*V("0ub1111")));
}

TEST_F(EvalDlopTest, and_basic) {
  hlop::DCall call{
      .op     = hlop::Ntype_op::And,
      .inputs = {
          {.value = V("0ub1110")},
          {.value = V("0ub1011")},
      },
  };
  auto res = ctx.execute(call);
  EXPECT_TRUE(res.outputs[0]->is_known_eq(*V("0ub1010")));
}

TEST_F(EvalDlopTest, xor_basic) {
  hlop::DCall call{
      .op     = hlop::Ntype_op::Xor,
      .inputs = {
          {.value = V("0ub1110")},
          {.value = V("0ub1011")},
      },
  };
  auto res = ctx.execute(call);
  EXPECT_TRUE(res.outputs[0]->is_known_eq(*V("0ub0101")));
}

TEST_F(EvalDlopTest, sum_with_AB) {
  hlop::DCall call{
      .op     = hlop::Ntype_op::Sum,
      .inputs = {
          {.pin = "A", .value = Vi(10)},
          {.pin = "A", .value = Vi(3)},
          {.pin = "B", .value = Vi(4)},
      },
  };
  auto res = ctx.execute(call);
  EXPECT_EQ(res.outputs[0]->to_i(), 9);  // 10 + 3 - 4
}

TEST_F(EvalDlopTest, mult_basic) {
  hlop::DCall call{
      .op     = hlop::Ntype_op::Mult,
      .inputs = {
          {.value = Vi(6)},
          {.value = Vi(7)},
      },
  };
  auto res = ctx.execute(call);
  EXPECT_EQ(res.outputs[0]->to_i(), 42);
}

TEST_F(EvalDlopTest, div_basic) {
  hlop::DCall call{
      .op     = hlop::Ntype_op::Div,
      .inputs = {
          {.value = Vi(42)},
          {.value = Vi(6)},
      },
  };
  auto res = ctx.execute(call);
  EXPECT_EQ(res.outputs[0]->to_i(), 7);
}

TEST_F(EvalDlopTest, not_basic) {
  hlop::DCall call{
      .op     = hlop::Ntype_op::Not,
      .inputs = {
          {.value = Vi(0)},
      },
  };
  auto res = ctx.execute(call);
  EXPECT_EQ(res.outputs[0]->to_i(), -1);  // ~0 = -1
}

TEST_F(EvalDlopTest, lt_basic) {
  hlop::DCall call{
      .op     = hlop::Ntype_op::LT,
      .inputs = {
          {.value = Vi(3)},
          {.value = Vi(5)},
      },
  };
  auto res = ctx.execute(call);
  EXPECT_TRUE(res.outputs[0]->is_known_true());
}

TEST_F(EvalDlopTest, eq_basic) {
  hlop::DCall call{
      .op     = hlop::Ntype_op::EQ,
      .inputs = {
          {.value = Vi(42)},
          {.value = Vi(42)},
      },
  };
  auto res = ctx.execute(call);
  EXPECT_TRUE(res.outputs[0]->is_known_true());
}

TEST_F(EvalDlopTest, shl_basic) {
  hlop::DCall call{
      .op     = hlop::Ntype_op::SHL,
      .inputs = {
          {.value = Vi(1)},
          {.value = Vi(4)},
      },
  };
  auto res = ctx.execute(call);
  EXPECT_EQ(res.outputs[0]->to_i(), 16);
}

TEST_F(EvalDlopTest, sra_basic) {
  hlop::DCall call{
      .op     = hlop::Ntype_op::SRA,
      .inputs = {
          {.value = Vi(-16)},
          {.value = Vi(2)},
      },
  };
  auto res = ctx.execute(call);
  EXPECT_EQ(res.outputs[0]->to_i(), -4);
}

TEST_F(EvalDlopTest, mux_basic) {
  hlop::DCall call{
      .op     = hlop::Ntype_op::Mux,
      .inputs = {
          {.pid = 0, .value = Vi(2)},
          {.pid = 1, .value = V("0x11")},
          {.pid = 2, .value = V("0x22")},
          {.pid = 3, .value = V("0x33")},
      },
  };
  auto res = ctx.execute(call);
  EXPECT_EQ(res.outputs[0]->to_i(), 0x33);
}

TEST_F(EvalDlopTest, ror_true) {
  hlop::DCall call{
      .op     = hlop::Ntype_op::Ror,
      .inputs = {
          {.value = Vi(0)},
          {.value = Vi(5)},
          {.value = Vi(0)},
      },
  };
  auto res = ctx.execute(call);
  EXPECT_TRUE(res.outputs[0]->is_known_true());
}

TEST_F(EvalDlopTest, ror_false) {
  hlop::DCall call{
      .op     = hlop::Ntype_op::Ror,
      .inputs = {
          {.value = Vi(0)},
          {.value = Vi(0)},
      },
  };
  auto res = ctx.execute(call);
  EXPECT_TRUE(res.outputs[0]->is_known_false());
}

TEST_F(EvalDlopTest, flop_basic) {
  hlop::DCall flop{
      .op       = hlop::Ntype_op::Flop,
      .state_id = "test.reg0",
      .inputs   = {
          {.pin = "clock_pin", .value = Vi(1)},
          {.pin = "din", .value = Vi(42)},
          {.pin = "enable", .value = Vi(1)},
          {.pin = "posclk", .value = Vi(1)},
      },
  };

  auto cur = ctx.execute(flop);
  EXPECT_EQ(cur.outputs[0]->to_i(), 0);  // initial value

  ctx.advance_clock();

  auto next = ctx.execute(flop);
  EXPECT_EQ(next.outputs[0]->to_i(), 42);  // committed value
}

TEST_F(EvalDlopTest, flop_enable_false) {
  hlop::DCall flop{
      .op       = hlop::Ntype_op::Flop,
      .state_id = "test.reg1",
      .inputs   = {
          {.pin = "clock_pin", .value = Vi(1)},
          {.pin = "din", .value = Vi(42)},
          {.pin = "enable", .value = Vi(0)},
          {.pin = "posclk", .value = Vi(1)},
      },
  };

  ctx.execute(flop);
  ctx.advance_clock();

  auto next = ctx.execute(flop);
  EXPECT_EQ(next.outputs[0]->to_i(), 0);  // enable=0, no update
}

TEST_F(EvalDlopTest, latch_transparent) {
  hlop::DCall latch{
      .op       = hlop::Ntype_op::Latch,
      .state_id = "test.latch0",
      .inputs   = {
          {.pin = "din", .value = Vi(42)},
          {.pin = "enable", .value = Vi(1)},
      },
  };

  auto res = ctx.execute(latch);
  EXPECT_EQ(res.outputs[0]->to_i(), 42);  // transparent
}

TEST_F(EvalDlopTest, latch_opaque) {
  // First make it transparent to store a value
  hlop::DCall latch1{
      .op       = hlop::Ntype_op::Latch,
      .state_id = "test.latch1",
      .inputs   = {
          {.pin = "din", .value = Vi(42)},
          {.pin = "enable", .value = Vi(1)},
      },
  };
  ctx.execute(latch1);

  // Now disable
  hlop::DCall latch2{
      .op       = hlop::Ntype_op::Latch,
      .state_id = "test.latch1",
      .inputs   = {
          {.pin = "din", .value = Vi(99)},
          {.pin = "enable", .value = Vi(0)},
      },
  };
  auto res = ctx.execute(latch2);
  EXPECT_EQ(res.outputs[0]->to_i(), 42);  // holds previous value
}

// --- Equivalence: verify dlop and slop produce same results ---

TEST_F(EvalDlopTest, equivalence_sum) {
  // dlop path
  hlop::DCall call{
      .op     = hlop::Ntype_op::Sum,
      .inputs = {
          {.pin = "A", .value = Vi(100)},
          {.pin = "A", .value = Vi(50)},
          {.pin = "B", .value = Vi(25)},
      },
  };
  auto dres = ctx.execute(call);

  // slop path
  std::array<V32, 2> plus{V32::create_integer(100), V32::create_integer(50)};
  std::array<V32, 1> minus{V32::create_integer(25)};
  auto               sres = hlop::eval_sum<V32>({.plus = plus, .minus = minus});

  EXPECT_EQ(dres.outputs[0]->to_i(), sres.to_i());
  EXPECT_EQ(dres.outputs[0]->to_i(), 125);
}

TEST_F(EvalDlopTest, equivalence_mux) {
  // dlop path
  hlop::DCall call{
      .op     = hlop::Ntype_op::Mux,
      .inputs = {
          {.pid = 0, .value = Vi(1)},
          {.pid = 1, .value = Vi(100)},
          {.pid = 2, .value = Vi(200)},
          {.pid = 3, .value = Vi(300)},
      },
  };
  auto dres = ctx.execute(call);

  // slop path
  V32                sel = V32::create_integer(1);
  std::array<V32, 3> data{V32::create_integer(100), V32::create_integer(200), V32::create_integer(300)};
  auto               sres = hlop::eval_mux<V32, V32>({.sel = sel, .data = data});

  EXPECT_EQ(dres.outputs[0]->to_i(), sres.to_i());
  EXPECT_EQ(dres.outputs[0]->to_i(), 200);
}
