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
  // ~0ub...1010 = 0sb...0101 (sign-extended, so ~0x0a = 0xf5 in 8-bit = -11)
  EXPECT_TRUE(out.is_known_eq(V8::create_integer(~0x0a)));
}

TEST_F(EvalSlopTest, eval_div) {
  auto out = hlop::eval_div(V32::create_integer(42), V32::create_integer(6));
  EXPECT_TRUE(out.is_known_eq(V32::create_integer(7)));
}

TEST_F(EvalSlopTest, eval_rem) {
  auto out = hlop::eval_rem(V32::create_integer(42), V32::create_integer(5));
  EXPECT_TRUE(out.is_known_eq(V32::create_integer(2)));
}

// Truncated, NOT floored: -42 % 5 is -2 (sign of the dividend), and 42 % -5 is
// +2. A floored modulo would answer +3 and -3 -- the one behaviour difference
// that matters, so it is pinned in both directions.
TEST_F(EvalSlopTest, eval_rem_negative_follows_dividend) {
  auto neg_dividend = hlop::eval_rem(V32::create_integer(-42), V32::create_integer(5));
  EXPECT_TRUE(neg_dividend.is_known_eq(V32::create_integer(-2)));

  auto neg_divisor = hlop::eval_rem(V32::create_integer(42), V32::create_integer(-5));
  EXPECT_TRUE(neg_divisor.is_known_eq(V32::create_integer(2)));
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
  // 0ub1010 sign-extended from bit 3 -> 0sb...11111010 = -6
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

// Non-contiguous mask: get_mask is a gather/pack, not an AND. Selecting the two
// separated nibbles of 0xABCD with 0xF0F concatenates them low-first -> 0xBD
// (a plain AND would wrongly yield 0x0B0D).
TEST_F(EvalSlopTest, eval_get_mask_non_contiguous) {
  auto out = hlop::eval_get_mask(V32::from_pyrope("0xABCD"), V32::from_pyrope("0xF0F"));
  EXPECT_TRUE(out.is_known_eq(V32::from_pyrope("0xBD")));
  // Documented example: extract bits 8..11 of 0xFEED -> 0xE.
  auto out2 = hlop::eval_get_mask(V32::from_pyrope("0xFEED"), V32::from_pyrope("0xF00"));
  EXPECT_TRUE(out2.is_known_eq(V32::from_pyrope("0xE")));
}

// Non-contiguous mask: set_mask is a scatter (consume value's low bits into the
// mask-selected positions), not (base&~mask)|(value&mask). 0xABC scattered into
// the two nibbles of 0xFFF selected by 0xF0F -> 0xBFC (the in-place form would
// wrongly yield 0xAFC).
TEST_F(EvalSlopTest, eval_set_mask_non_contiguous) {
  auto base = V32::from_pyrope("0xFFF");
  auto mask = V32::from_pyrope("0xF0F");
  auto val  = V32::from_pyrope("0xABC");
  auto out  = hlop::eval_set_mask(base, mask, val);
  EXPECT_TRUE(out.is_known_eq(V32::from_pyrope("0xBFC")));
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
  // 2-input AND gate: truth table = 0ub1000 = 8
  V8                lut_val = V8::create_integer(8);
  std::array<V8, 2> ins{V8::create_integer(1), V8::create_integer(1)};
  auto              out = hlop::eval_lut<V8>({.lut_val = lut_val, .inputs = ins});
  EXPECT_TRUE(out.is_known_true());

  // Input 0=1, 1=0 -> index=1 -> bit 1 of 0ub1000 = 0
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

TEST_F(EvalSlopTest, eval_flop_pipe_depth_three) {
  hlop::RegState<V32> regs(4, V32::create_integer(0));
  V32                 clk = V32::create_integer(1);

  V32                 din1 = V32::create_integer(11);
  hlop::FlopArgs<V32> a1{.din = din1, .clock_pin = clk};
  EXPECT_TRUE(hlop::eval_flop_pipe<3>(regs, 0, a1).is_known_eq(V32::create_integer(0)));
  regs.advance_clock();

  V32                 din2 = V32::create_integer(22);
  hlop::FlopArgs<V32> a2{.din = din2, .clock_pin = clk};
  EXPECT_TRUE(hlop::eval_flop_pipe<3>(regs, 0, a2).is_known_eq(V32::create_integer(0)));
  regs.advance_clock();

  V32                 din3 = V32::create_integer(33);
  hlop::FlopArgs<V32> a3{.din = din3, .clock_pin = clk};
  EXPECT_TRUE(hlop::eval_flop_pipe<3>(regs, 0, a3).is_known_eq(V32::create_integer(0)));
  regs.advance_clock();

  V32                 din4 = V32::create_integer(44);
  hlop::FlopArgs<V32> a4{.din = din4, .clock_pin = clk};
  EXPECT_TRUE(hlop::eval_flop_pipe<3>(regs, 0, a4).is_known_eq(V32::create_integer(11)));
}

TEST_F(EvalSlopTest, eval_flop_pipe_enable_false_holds_all_stages) {
  hlop::RegState<V32> regs(4, V32::create_integer(0));
  V32                 clk = V32::create_integer(1);
  V32                 en0 = V32::create_integer(0);

  V32                 din1 = V32::create_integer(7);
  hlop::FlopArgs<V32> a1{.din = din1, .clock_pin = clk};
  hlop::eval_flop_pipe<2>(regs, 0, a1);
  regs.advance_clock();

  V32                 din2 = V32::create_integer(9);
  hlop::FlopArgs<V32> hold{.din = din2, .clock_pin = clk, .enable = &en0};
  EXPECT_TRUE(hlop::eval_flop_pipe<2>(regs, 0, hold).is_known_eq(V32::create_integer(0)));
  regs.advance_clock();

  hlop::FlopArgs<V32> observe{.din = din2, .clock_pin = clk};
  EXPECT_TRUE(hlop::eval_flop_pipe<2>(regs, 0, observe).is_known_eq(V32::create_integer(0)));
  regs.advance_clock();
  EXPECT_TRUE(hlop::eval_flop_pipe<2>(regs, 0, observe).is_known_eq(V32::create_integer(7)));
}

TEST_F(EvalSlopTest, eval_flop_pipe_async_reset_resets_all_stages) {
  hlop::RegState<V32> regs(4, V32::create_integer(0));
  V32                 clk     = V32::create_integer(1);
  V32                 initial = V32::create_integer(5);
  V32                 reset   = V32::create_integer(1);
  V32                 async   = V32::create_integer(1);

  V32                 din = V32::create_integer(42);
  hlop::FlopArgs<V32> run{.din = din, .clock_pin = clk, .initial = &initial};
  hlop::eval_flop_pipe<3>(regs, 0, run);
  regs.advance_clock();

  hlop::FlopArgs<V32> rst{.din = din, .clock_pin = clk, .reset_pin = &reset, .initial = &initial, .async_ = &async};
  EXPECT_TRUE(hlop::eval_flop_pipe<3>(regs, 0, rst).is_known_eq(initial));
  regs.advance_clock();
  EXPECT_TRUE(hlop::eval_flop_pipe<3>(regs, 0, run).is_known_eq(initial));
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
  hlop::MemState<V32> mem(256, V32::create_integer(0), hlop::Mem_order::old);

  V32 addr = V32::create_integer(7);
  // upass.tolg emits a get_mask/wrap on the data path, so `din` always fits the
  // entry: 0xdeadbeef has bit 31 set, so in a 32-bit memory it arrives already
  // sign-extended. A wider din is a compiler bug and asserts in stage_write.
  V32 data = V32::from_pyrope("0xdeadbeef").sext_op(31);
  V32 en   = V32::create_integer(1);

  hlop::MemoryWriteArgs<V32> wargs{.addr = addr, .data = data, .enable = en};
  hlop::eval_memory_write<V32>(mem, wargs);

  // ordering="old": the staged write is invisible until advance_clock().
  hlop::MemoryReadArgs<V32> rargs{.addr = addr, .enable = en};
  auto                      rd = hlop::eval_memory_read<V32>(mem, rargs);
  EXPECT_TRUE(rd.is_known_eq(V32::create_integer(0)));

  mem.advance_clock();

  auto rd2 = hlop::eval_memory_read<V32>(mem, rargs);
  EXPECT_TRUE(rd2.is_known_eq(data));
}

TEST_F(EvalSlopTest, eval_memory_fwd) {
  hlop::MemState<V32> mem(256, V32::create_integer(0), hlop::Mem_order::fwd);

  V32 addr = V32::create_integer(7);
  V32 data = V32::from_pyrope("0xcafe");
  V32 en   = V32::create_integer(1);

  hlop::MemoryWriteArgs<V32> wargs{.addr = addr, .data = data, .enable = en};
  hlop::eval_memory_write<V32>(mem, wargs);

  // ordering="fwd": the same-cycle read sees the staged write.
  hlop::MemoryReadArgs<V32> rargs{.addr = addr, .enable = en};
  auto                      rd = hlop::eval_memory_read<V32>(mem, rargs);
  EXPECT_TRUE(rd.is_known_eq(V32::from_pyrope("0xcafe")));
}

TEST_F(EvalSlopTest, eval_memory_program_and_none) {
  // Two read ports, one write port; read port 0 precedes the write, read port 1
  // follows it.
  hlop::Mem_cfg cfg;
  cfg.size      = 16;
  cfg.bits      = 32;
  cfg.n_rd      = 2;
  cfg.n_wr      = 1;
  cfg.n_user_wr = 1;
  cfg.order     = hlop::Mem_order::program;
  cfg.fwd_upto  = {0, 1};

  hlop::MemState<V32> mem;
  mem.configure(cfg, V32::create_integer(0));
  mem.entries()[3] = V32::create_integer(7);

  V32 addr = V32::create_integer(3);
  V32 data = V32::create_integer(42);
  V32 en   = V32::create_integer(1);
  hlop::eval_memory_write<V32>(mem, {.addr = addr, .data = data, .enable = en});

  EXPECT_TRUE(hlop::eval_memory_read<V32>(mem, {.addr = addr, .enable = en, .rd_port = 0}).is_known_eq(V32::create_integer(7)));
  EXPECT_TRUE(hlop::eval_memory_read<V32>(mem, {.addr = addr, .enable = en, .rd_port = 1}).is_known_eq(V32::create_integer(42)));

  // ordering="none": both defined answers are 0 here, so a nonzero read proves
  // the collision is undefined (Slop fills it from the seeded PRNG).
  hlop::MemState<V32> mem_none(16, V32::create_integer(0), hlop::Mem_order::none);
  hlop::eval_memory_write<V32>(mem_none, {.addr = addr, .data = V32::create_integer(0), .enable = en});
  bool saw_nonzero = false;
  for (int i = 0; i < 64; ++i) {
    if (!hlop::eval_memory_read<V32>(mem_none, {.addr = addr, .enable = en}).is_known_eq(V32::create_integer(0))) {
      saw_nonzero = true;
    }
  }
  EXPECT_TRUE(saw_nonzero);
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
  EXPECT_EQ(res.outputs[0]->to_just_i64(), 9);  // 10 + 3 - 4
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
  EXPECT_EQ(res.outputs[0]->to_just_i64(), 42);
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
  EXPECT_EQ(res.outputs[0]->to_just_i64(), 7);
}

TEST_F(EvalDlopTest, rem_basic) {
  hlop::DCall call{
      .op     = hlop::Ntype_op::Rem,
      .inputs = {
          {.value = Vi(42)},
          {.value = Vi(5)},
      },
  };
  auto res = ctx.execute(call);
  EXPECT_EQ(res.outputs[0]->to_just_i64(), 2);
}

// Positional, not commutative: swapping the operands must change the answer
// (5 % 42 == 5), which is what catches a fold-over-all-inputs implementation.
TEST_F(EvalDlopTest, rem_negative_and_operand_order) {
  hlop::DCall neg{
      .op     = hlop::Ntype_op::Rem,
      .inputs = {
          {.value = Vi(-42)},
          {.value = Vi(5)},
      },
  };
  EXPECT_EQ(ctx.execute(neg).outputs[0]->to_just_i64(), -2);

  hlop::DCall swapped{
      .op     = hlop::Ntype_op::Rem,
      .inputs = {
          {.value = Vi(5)},
          {.value = Vi(42)},
      },
  };
  EXPECT_EQ(ctx.execute(swapped).outputs[0]->to_just_i64(), 5);
}

TEST_F(EvalDlopTest, rem_by_zero_is_nil) {
  hlop::DCall call{
      .op     = hlop::Ntype_op::Rem,
      .inputs = {
          {.value = Vi(42)},
          {.value = Vi(0)},
      },
  };
  auto res = ctx.execute(call);
  EXPECT_TRUE(res.outputs[0]->is_nil());
}

TEST_F(EvalDlopTest, not_basic) {
  hlop::DCall call{
      .op     = hlop::Ntype_op::Not,
      .inputs = {
          {.value = Vi(0)},
      },
  };
  auto res = ctx.execute(call);
  EXPECT_EQ(res.outputs[0]->to_just_i64(), -1);  // ~0 = -1
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
  EXPECT_EQ(res.outputs[0]->to_just_i64(), 16);
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
  EXPECT_EQ(res.outputs[0]->to_just_i64(), -4);
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
  EXPECT_EQ(res.outputs[0]->to_just_i64(), 0x33);
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
  EXPECT_EQ(cur.outputs[0]->to_just_i64(), 0);  // initial value

  ctx.advance_clock();

  auto next = ctx.execute(flop);
  EXPECT_EQ(next.outputs[0]->to_just_i64(), 42);  // committed value
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
  EXPECT_EQ(next.outputs[0]->to_just_i64(), 0);  // enable=0, no update
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
  EXPECT_EQ(res.outputs[0]->to_just_i64(), 42);  // transparent
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
  EXPECT_EQ(res.outputs[0]->to_just_i64(), 42);  // holds previous value
}

// --- Memory (DContext::exec_memory) ---
//
// The Memory cell's sink pids are laid out in blocks of 16 (LiveHD
// Ntype::Memory_port_stride): port i's per-port pin at offset `off` is pid
// i*16 + off, with addr=0, din=3, enable=4, rdport=10. The cell-global pins
// (bits/size/fwd/undef/wensize) are looked up by NAME, so they carry no pid.
// `fwd`/`undef` are per-(read,write) matrices, bit (r*n_wr + w).

namespace {
constexpr int kStride = 16;

hlop::DInput mem_pid(int port, int off, hlop::DValue v) { return {.pid = port * kStride + off, .value = std::move(v)}; }
hlop::DInput mem_pin(const char* name, hlop::DValue v) { return {.pin = name, .value = std::move(v)}; }

// One write port (block 0) + one read port (block 1), both at `addr`.
std::vector<hlop::DInput> mem_1w1r(hlop::DValue waddr, hlop::DValue din, hlop::DValue wen, hlop::DValue raddr, hlop::DValue fwd,
                                   hlop::DValue undef) {
  return {
      mem_pin("bits", Dlop::create_integer(32)),
      mem_pin("size", Dlop::create_integer(16)),
      mem_pin("fwd", std::move(fwd)),
      mem_pin("undef", std::move(undef)),
      mem_pid(0, 0, std::move(waddr)),
      mem_pid(0, 3, std::move(din)),
      mem_pid(0, 4, std::move(wen)),
      mem_pid(0, 10, Dlop::create_bool(false)),  // rdport=0 -> write
      mem_pid(1, 0, std::move(raddr)),
      mem_pid(1, 10, Dlop::create_bool(true)),  // rdport=1 -> read
  };
}
}  // namespace

TEST_F(EvalDlopTest, memory_old_hides_the_staged_write) {
  hlop::DCall call{.op       = hlop::Ntype_op::Memory,
                   .state_id = "test.mem_old",
                   .inputs   = mem_1w1r(Vi(3), Vi(42), Vi(1), Vi(3), Vi(0), Vi(0))};

  EXPECT_EQ(ctx.execute(call).outputs[0]->to_just_i64(), 0);  // ordering="old": committed value
  ctx.advance_clock();
  EXPECT_EQ(ctx.execute(call).outputs[0]->to_just_i64(), 42);
}

TEST_F(EvalDlopTest, memory_fwd_forwards_the_staged_write) {
  // fwd matrix bit (r=0,w=0) set -> Mem_order::fwd.
  hlop::DCall call{.op       = hlop::Ntype_op::Memory,
                   .state_id = "test.mem_fwd",
                   .inputs   = mem_1w1r(Vi(3), Vi(42), Vi(1), Vi(3), Vi(1), Vi(0))};

  EXPECT_EQ(ctx.execute(call).outputs[0]->to_just_i64(), 42);  // same cycle
}

TEST_F(EvalDlopTest, memory_none_makes_the_collision_unknown) {
  // undef matrix bit (r=0,w=0) set -> Mem_order::none.
  hlop::DCall call{.op       = hlop::Ntype_op::Memory,
                   .state_id = "test.mem_none",
                   .inputs   = mem_1w1r(Vi(3), Vi(42), Vi(1), Vi(3), Vi(0), Vi(1))};

  EXPECT_TRUE(ctx.execute(call).outputs[0]->has_unknowns());
}

TEST_F(EvalDlopTest, memory_no_collision_is_defined_under_none) {
  // Same undef matrix, but the read address does not collide with the write.
  hlop::DCall call{.op       = hlop::Ntype_op::Memory,
                   .state_id = "test.mem_none2",
                   .inputs   = mem_1w1r(Vi(3), Vi(42), Vi(1), Vi(9), Vi(0), Vi(1))};

  auto out = ctx.execute(call).outputs[0];
  EXPECT_FALSE(out->has_unknowns());
  EXPECT_EQ(out->to_just_i64(), 0);
}

#ifndef NDEBUG
// `fwd`/`undef` are comptime, so an unknown bit there is a compiler bug, not a
// value the memory has to interpret. Read through bit_test it would look SET and
// latch ordering="fwd" for the life of the memory, so it must fail loudly.
TEST(EvalDlopMemoryDeathTest, unknown_comptime_matrix_asserts) {
  hlop::DContext ctx;
  hlop::DCall    call{.op       = hlop::Ntype_op::Memory,
                      .state_id = "test.mem_xfwd",
                      .inputs   = mem_1w1r(Dlop::create_integer(3),
                                           Dlop::create_integer(42),
                                           Dlop::create_integer(1),
                                           Dlop::create_integer(3),
                                           Dlop::unknown(1),
                                           Dlop::create_integer(0))};
  EXPECT_DEATH((void)ctx.execute(call), "cannot have unknown bits");
}
#endif

TEST_F(EvalDlopTest, memory_disabled_write_never_commits) {
  hlop::DCall call{.op       = hlop::Ntype_op::Memory,
                   .state_id = "test.mem_wen0",
                   .inputs   = mem_1w1r(Vi(3), Vi(42), Vi(0), Vi(3), Vi(1), Vi(0))};

  EXPECT_EQ(ctx.execute(call).outputs[0]->to_just_i64(), 0);
  ctx.advance_clock();
  EXPECT_EQ(ctx.execute(call).outputs[0]->to_just_i64(), 0);
}

TEST_F(EvalDlopTest, memory_unknown_write_enable_makes_the_lane_x) {
  // An x enable means the write MAY have landed, so the lane is neither the old
  // value nor din -- it is x. Treating it as a known 0 would hand back stale
  // DEFINED data; treating it as a known 1 would claim the write definitely
  // happened. Both wensize paths must agree on this.
  hlop::DCall call{.op       = hlop::Ntype_op::Memory,
                   .state_id = "test.mem_xwen",
                   .inputs   = mem_1w1r(Vi(3), Vi(42), Dlop::unknown(1), Vi(3), Vi(1), Vi(0))};
  EXPECT_TRUE(ctx.execute(call).outputs[0]->has_unknowns());
  ctx.advance_clock();
  EXPECT_TRUE(ctx.execute(call).outputs[0]->has_unknowns());  // committed as x too

  // Same design with a 2-lane enable: lane 0 is a known 1, lane 1 is x, so the
  // low nibble takes din and the high nibble goes x.
  hlop::DCall lanes{
      .op       = hlop::Ntype_op::Memory,
      .state_id = "test.mem_xwen2",
      .inputs   = {
          mem_pin("bits", Vi(8)), mem_pin("size", Vi(16)), mem_pin("wensize", Vi(2)), mem_pin("fwd", Vi(1)),
          mem_pin("undef", Vi(0)),
          mem_pid(0, 0, Vi(3)), mem_pid(0, 3, Vi(0x2A)),
          mem_pid(0, 4, Dlop::from_pyrope("0ub?1")),
          mem_pid(0, 10, Dlop::create_bool(false)),
          mem_pid(1, 0, Vi(3)), mem_pid(1, 10, Dlop::create_bool(true)),
      },
  };
  const auto out = ctx.execute(lanes).outputs[0];
  for (int b = 0; b < 4; ++b) {
    EXPECT_FALSE(out->unknown_bit_test(b)) << "lane 0 bit " << b << " should be known";
  }
  for (int b = 4; b < 8; ++b) {
    EXPECT_TRUE(out->unknown_bit_test(b)) << "lane 1 bit " << b << " should be x";
  }
  EXPECT_TRUE(out->and_op(*Vi(0xF))->is_known_eq(*Vi(0xA)));  // low nibble took din
}

TEST_F(EvalDlopTest, memory_gated_read_port_holds_its_dout) {
  // Cycle 1: an enabled read of the forwarded write drives 42 onto the bus.
  auto c1 = mem_1w1r(Vi(5), Vi(42), Vi(1), Vi(5), Vi(1), Vi(0));
  c1.push_back(mem_pid(1, 4, Vi(1)));  // read enable = 1
  hlop::DCall on{.op = hlop::Ntype_op::Memory, .state_id = "test.mem_ren", .inputs = std::move(c1)};
  EXPECT_EQ(ctx.execute(on).outputs[0]->to_just_i64(), 42);

  ctx.advance_clock();

  // Cycle 2: nothing writes and the read port is gated off, so the dout HOLDS
  // 42 -- not a fabricated 0, and not entry 3 (which is still 0). A fresh x or
  // PRNG draw here would report switching activity a gated port cannot have.
  auto c2 = mem_1w1r(Vi(5), Vi(99), Vi(0), Vi(3), Vi(1), Vi(0));  // write disabled
  c2.push_back(mem_pid(1, 4, Vi(0)));                             // read enable = 0
  hlop::DCall off{.op = hlop::Ntype_op::Memory, .state_id = "test.mem_ren", .inputs = std::move(c2)};

  const auto held = ctx.execute(off).outputs[0];
  EXPECT_FALSE(held->has_unknowns()) << "a gated dout must not toggle";
  EXPECT_EQ(held->to_just_i64(), 42);
}

TEST_F(EvalDlopTest, memory_read_enable_x_is_undefined) {
  // An enable that is itself x: we cannot say whether the port accessed the
  // array, so the dout is x rather than a held or a fabricated value.
  auto xin = mem_1w1r(Vi(3), Vi(42), Vi(1), Vi(3), Vi(1), Vi(0));
  xin.push_back(mem_pid(1, 4, Dlop::unknown(1)));
  hlop::DCall unk{.op = hlop::Ntype_op::Memory, .state_id = "test.mem_ren_x", .inputs = std::move(xin)};
  EXPECT_TRUE(ctx.execute(unk).outputs[0]->has_unknowns());

  // An absent enable pin means always-on.
  hlop::DCall absent{.op       = hlop::Ntype_op::Memory,
                     .state_id = "test.mem_ren_absent",
                     .inputs   = mem_1w1r(Vi(3), Vi(42), Vi(1), Vi(3), Vi(1), Vi(0))};
  EXPECT_EQ(ctx.execute(absent).outputs[0]->to_just_i64(), 42);
}

TEST_F(EvalDlopTest, memory_program_forwards_only_preceding_writes) {
  // One write port (block 0) and two read ports (blocks 1 and 2). Read port 0
  // precedes the write, read port 1 follows it, so the fwd matrix rows are
  // {0, 1} -> bit (r=1,w=0) set -> 0b10.
  hlop::DCall call{
      .op       = hlop::Ntype_op::Memory,
      .state_id = "test.mem_prog",
      .inputs   = {
          mem_pin("bits", Vi(32)), mem_pin("size", Vi(16)), mem_pin("fwd", Vi(0b10)), mem_pin("undef", Vi(0)),
          mem_pid(0, 0, Vi(3)), mem_pid(0, 3, Vi(42)), mem_pid(0, 4, Vi(1)), mem_pid(0, 10, Dlop::create_bool(false)),
          mem_pid(1, 0, Vi(3)), mem_pid(1, 10, Dlop::create_bool(true)),
          mem_pid(2, 0, Vi(3)), mem_pid(2, 10, Dlop::create_bool(true)),
      },
  };

  auto res = ctx.execute(call);
  ASSERT_EQ(res.outputs.size(), 2u);
  EXPECT_EQ(res.outputs[0]->to_just_i64(), 0);   // before the write
  EXPECT_EQ(res.outputs[1]->to_just_i64(), 42);  // after the write
}

TEST_F(EvalDlopTest, memory_write_port_priority_is_the_port_index) {
  // Two write ports (blocks 0 and 1) to the same address; the higher index
  // wins at tick(). One read port at block 2.
  hlop::DCall call{
      .op       = hlop::Ntype_op::Memory,
      .state_id = "test.mem_prio",
      .inputs   = {
          mem_pin("bits", Vi(32)), mem_pin("size", Vi(16)), mem_pin("fwd", Vi(0)), mem_pin("undef", Vi(0)),
          mem_pid(0, 0, Vi(5)), mem_pid(0, 3, Vi(11)), mem_pid(0, 4, Vi(1)), mem_pid(0, 10, Dlop::create_bool(false)),
          mem_pid(1, 0, Vi(5)), mem_pid(1, 3, Vi(22)), mem_pid(1, 4, Vi(1)), mem_pid(1, 10, Dlop::create_bool(false)),
          mem_pid(2, 0, Vi(5)), mem_pid(2, 10, Dlop::create_bool(true)),
      },
  };

  ctx.execute(call);
  ctx.advance_clock();
  EXPECT_EQ(ctx.execute(call).outputs[0]->to_just_i64(), 22);
}

TEST_F(EvalDlopTest, memory_wensize_writes_only_the_enabled_lane) {
  // bits=8, wensize=2 -> two 4-bit lanes; enable 0b01 writes the low nibble.
  hlop::DCall call{
      .op       = hlop::Ntype_op::Memory,
      .state_id = "test.mem_wen",
      .inputs   = {
          mem_pin("bits", Vi(8)), mem_pin("size", Vi(16)), mem_pin("wensize", Vi(2)), mem_pin("fwd", Vi(0)),
          mem_pin("undef", Vi(0)),
          mem_pid(0, 0, Vi(2)), mem_pid(0, 3, Vi(0xFF)), mem_pid(0, 4, Vi(0b01)),
          mem_pid(0, 10, Dlop::create_bool(false)),
          mem_pid(1, 0, Vi(2)), mem_pid(1, 10, Dlop::create_bool(true)),
      },
  };

  ctx.execute(call);
  ctx.advance_clock();
  EXPECT_EQ(ctx.execute(call).outputs[0]->to_just_i64(), 0x0F);  // high lane untouched
}

// --- Whole-array Memory cells (the `update`/`reset` buses + `read_all`) ---
// bits=4, size=4 -> a 16-bit bus, entry 0 in the LOW nibble, row-major: the
// same layout Mem_base, cgen_verilog and pass.lec use.

TEST_F(EvalDlopTest, memory_whole_array_update_and_read_all) {
  hlop::DCall call{
      .op       = hlop::Ntype_op::Memory,
      .state_id = "test.mem_whole",
      .inputs   = {
          mem_pin("bits", Vi(4)), mem_pin("size", Vi(4)),
          mem_pin("update", Vi(0x4321)),
      },
  };

  // Combinational read_all reflects the CURRENT contents; the update is a
  // next-state bus, so it is not visible until the edge.
  auto before = ctx.execute(call);
  ASSERT_TRUE(before.read_all.has_value());
  EXPECT_EQ((*before.read_all)->to_just_i64(), 0);

  ctx.advance_clock();

  auto after = ctx.execute(call);
  ASSERT_TRUE(after.read_all.has_value());
  EXPECT_EQ((*after.read_all)->to_just_i64(), 0x4321);
}

TEST_F(EvalDlopTest, memory_whole_array_update_enable_gates_the_bus) {
  hlop::DCall off{
      .op       = hlop::Ntype_op::Memory,
      .state_id = "test.mem_whole_ue",
      .inputs   = {
          mem_pin("bits", Vi(4)), mem_pin("size", Vi(4)),
          mem_pin("update", Vi(0x4321)), mem_pin("update_enable", Vi(0)),
      },
  };
  ctx.execute(off);
  ctx.advance_clock();
  EXPECT_EQ((*ctx.execute(off).read_all)->to_just_i64(), 0);  // gated: no update
}

TEST_F(EvalDlopTest, memory_whole_array_reset_outranks_writes_and_update) {
  // A reset restores `init` AND drops the per-port writes staged this cycle,
  // rather than letting them commit on top of the restored contents. Priority
  // is reset > per-port write > update, matching cgen_sim.
  auto inputs = std::vector<hlop::DInput>{
      mem_pin("bits", Vi(4)),
      mem_pin("size", Vi(4)),
      mem_pin("update", Vi(0x4321)),
      mem_pin("init", Vi(0x1111)),
      mem_pin("reset", Vi(1)),
      mem_pin("fwd", Vi(0)),
      mem_pin("undef", Vi(0)),
      mem_pid(0, 0, Vi(2)),
      mem_pid(0, 3, Vi(7)),
      mem_pid(0, 4, Vi(1)),
      mem_pid(0, 10, Dlop::create_bool(false)),
  };
  hlop::DCall call{.op = hlop::Ntype_op::Memory, .state_id = "test.mem_whole_rst", .inputs = std::move(inputs)};

  ctx.execute(call);
  ctx.advance_clock();
  EXPECT_EQ((*ctx.execute(call).read_all)->to_just_i64(), 0x1111);  // init, not 0x4321, not entry2=7
}

TEST_F(EvalDlopTest, memory_whole_array_write_lands_on_top_of_the_update) {
  // No reset: the bulk update applies first, then the per-port write overrides
  // the entry it touches.
  auto inputs = std::vector<hlop::DInput>{
      mem_pin("bits", Vi(4)),
      mem_pin("size", Vi(4)),
      mem_pin("update", Vi(0x4321)),
      mem_pin("fwd", Vi(0)),
      mem_pin("undef", Vi(0)),
      mem_pid(0, 0, Vi(2)),
      mem_pid(0, 3, Vi(7)),
      mem_pid(0, 4, Vi(1)),
      mem_pid(0, 10, Dlop::create_bool(false)),
  };
  hlop::DCall call{.op = hlop::Ntype_op::Memory, .state_id = "test.mem_whole_w", .inputs = std::move(inputs)};

  ctx.execute(call);
  ctx.advance_clock();
  EXPECT_EQ((*ctx.execute(call).read_all)->to_just_i64(), 0x4721);  // entry 2: 3 -> 7
}

TEST_F(EvalDlopTest, memory_per_port_cell_has_no_read_all) {
  hlop::DCall call{.op       = hlop::Ntype_op::Memory,
                   .state_id = "test.mem_no_readall",
                   .inputs   = mem_1w1r(Vi(3), Vi(42), Vi(1), Vi(3), Vi(0), Vi(0))};
  EXPECT_FALSE(ctx.execute(call).read_all.has_value());  // O(size) packing is skipped
}

TEST_F(EvalDlopTest, memory_unknown_read_address_is_unknown) {
  hlop::DCall call{.op       = hlop::Ntype_op::Memory,
                   .state_id = "test.mem_xaddr",
                   .inputs   = mem_1w1r(Vi(3), Vi(42), Vi(1), Dlop::unknown(4), Vi(0), Vi(0))};

  EXPECT_TRUE(ctx.execute(call).outputs[0]->has_unknowns());
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

  EXPECT_EQ(dres.outputs[0]->to_just_i64(), sres.to_just_i64());
  EXPECT_EQ(dres.outputs[0]->to_just_i64(), 125);
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

  EXPECT_EQ(dres.outputs[0]->to_just_i64(), sres.to_just_i64());
  EXPECT_EQ(dres.outputs[0]->to_just_i64(), 200);
}
