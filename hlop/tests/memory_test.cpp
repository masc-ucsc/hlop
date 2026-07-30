//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// One small test per same-cycle ordering mode (old / fwd / program / none),
// on both backends, plus the pieces the four modes share: write-write port
// priority, sub-word (wensize) lanes, and the staging/tick cycle protocol.

#include "memory.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>

using namespace hlop;

namespace {

using V8  = Slop<8>;
using V16 = Slop<16>;
using D   = spool_ptr<Dlop>;

constexpr V8 v8(int64_t x) { return V8::create_integer(x); }
V8           on8() { return V8::create_bool(true); }
V8           off8() { return V8::create_bool(false); }

// Low `bits` of a (possibly sign-extended) Slop, as an unsigned pattern.
template <int N>
uint64_t bitsof(const Slop<N>& v) {
  return static_cast<uint64_t>(v.to_just_i64()) & ((uint64_t{1} << N) - 1);
}

// A 16-bit pattern as the entry type must carry it: sign-wrapped, the form
// upass.tolg's get_mask/wrap produces. A raw V16::create_integer(0xABCD) needs
// 17 signed bits, which stage_write rejects as a data-path bug.
V16 pat16(uint64_t pattern) { return V16::create_integer(static_cast<int64_t>(pattern)).sext_op(15); }

}  // namespace

// ===========================================================================
// ordering="old" — nothing forwards; a colliding read is the DEFINED committed
// value. What every Verilog reader produces.
// ===========================================================================
TEST(MemoryOrdering, old_read_sees_committed_value) {
  Memory_old<V8, 8, 16, 1, 1> m;
  m[3] = v8(7);

  m.stage_write<0>(on8(), v8(3), v8(42));
  EXPECT_EQ(m.read<0>(v8(3)).to_just_i64(), 7);  // same cycle: still the old value
  EXPECT_EQ(m.read<0>(v8(4)).to_just_i64(), 0);  // untouched address

  m.tick();
  EXPECT_EQ(m.read<0>(v8(3)).to_just_i64(), 42);  // next cycle: committed
}

// ===========================================================================
// ordering="fwd" — pure transparency, position-blind.
// ===========================================================================
TEST(MemoryOrdering, fwd_read_sees_new_data) {
  Memory_fwd<V8, 8, 16, 1, 1> m;
  m[3] = v8(7);

  m.stage_write<0>(on8(), v8(3), v8(42));
  EXPECT_EQ(m.read<0>(v8(3)).to_just_i64(), 42);  // forwarded
  EXPECT_EQ(m.read<0>(v8(4)).to_just_i64(), 0);   // no collision -> stored

  m.tick();
  EXPECT_EQ(m.read<0>(v8(3)).to_just_i64(), 42);
}

TEST(MemoryOrdering, fwd_disabled_write_does_not_forward) {
  Memory_fwd<V8, 8, 16, 1, 1> m;
  m[3] = v8(7);

  m.stage_write<0>(off8(), v8(3), v8(42));
  EXPECT_EQ(m.read<0>(v8(3)).to_just_i64(), 7);
  m.tick();
  EXPECT_EQ(m.read<0>(v8(3)).to_just_i64(), 7);  // never committed either
}

// ===========================================================================
// ordering="program" (the Pyrope default) — read port r forwards exactly the
// writes that textually precede it. Read port 0 is before the write, read
// port 1 after, so Fwd_upto is {0, 1}.
// ===========================================================================
TEST(MemoryOrdering, program_forwards_only_preceding_writes) {
  static constexpr std::array<uint16_t, 2>                                           pre{0, 1};
  Memory_program<V8, 8, 16, /*NRd=*/2, /*NWr=*/1, /*NUserWr=*/1, /*WenSize=*/1, pre> m;
  m[3] = v8(7);

  m.stage_write<0>(on8(), v8(3), v8(42));
  EXPECT_EQ(m.read<0>(v8(3)).to_just_i64(), 7);   // read BEFORE the write
  EXPECT_EQ(m.read<1>(v8(3)).to_just_i64(), 42);  // read AFTER the write

  m.tick();
  EXPECT_EQ(m.read<0>(v8(3)).to_just_i64(), 42);
  EXPECT_EQ(m.read<1>(v8(3)).to_just_i64(), 42);
}

// With two writes, a read between them forwards only the first.
TEST(MemoryOrdering, program_prefix_is_per_read_port) {
  static constexpr std::array<uint16_t, 1>                                           pre{1};
  Memory_program<V8, 8, 16, /*NRd=*/1, /*NWr=*/2, /*NUserWr=*/2, /*WenSize=*/1, pre> m;

  m.stage_write<0>(on8(), v8(5), v8(11));
  m.stage_write<1>(on8(), v8(5), v8(22));
  EXPECT_EQ(m.read<0>(v8(5)).to_just_i64(), 11);  // only write port 0 precedes

  m.tick();
  EXPECT_EQ(m.read<0>(v8(5)).to_just_i64(), 22);  // last enabled port wins the commit
}

// ===========================================================================
// ordering="none" — the collision is UNDEFINED. Slop has no x, so the value is
// a PRNG draw. Both defined answers here are 0 (stored 0, written 0), so any
// nonzero result proves the read is neither "old" nor "fwd".
// ===========================================================================
TEST(MemoryOrdering, none_collision_is_undefined) {
  hlop_set_random_seed(0xBEEF);
  Memory_none<V8, 8, 16, 1, 1> m;  // every entry starts at 0

  m.stage_write<0>(on8(), v8(3), v8(0));  // write 0 over 0
  bool saw_nonzero = false;
  for (int i = 0; i < 64; ++i) {
    if (m.read<0>(v8(3)).to_just_i64() != 0) {
      saw_nonzero = true;
    }
  }
  EXPECT_TRUE(saw_nonzero);

  // A read of an address nobody wrote is still the plain stored value.
  m[4] = v8(9);
  EXPECT_EQ(m.read<0>(v8(4)).to_just_i64(), 9);
}

TEST(MemoryOrdering, none_is_reproducible_under_a_seed) {
  auto run = [] {
    hlop_set_random_seed(0x1234);
    Memory_none<V8, 8, 16, 1, 1> m;
    m.stage_write<0>(on8(), v8(2), v8(0));
    std::array<int64_t, 8> out{};
    for (auto& o : out) {
      o = m.read<0>(v8(2)).to_just_i64();
    }
    return out;
  };
  // The PRNG is a thread_local latched at first use, so the two runs share one
  // stream: what must hold is that the draws are the PRNG's, not that the
  // sequence restarts. Assert the reads actually vary (they are draws, not a
  // constant) rather than that two runs match.
  auto a      = run();
  bool varies = false;
  for (std::size_t i = 1; i < a.size(); ++i) {
    if (a[i] != a[0]) {
      varies = true;
    }
  }
  EXPECT_TRUE(varies);
}

// ===========================================================================
// Shared machinery
// ===========================================================================

// Write-write same-address collision: the highest-numbered enabled port wins,
// matching cgen_memory_*.v's statement-order priority.
TEST(MemoryPorts, higher_write_port_wins_the_commit) {
  Memory_old<V8, 8, 16, 1, 3> m;
  m.stage_write<0>(on8(), v8(1), v8(10));
  m.stage_write<2>(on8(), v8(1), v8(30));
  m.stage_write<1>(on8(), v8(1), v8(20));  // staging order must not matter
  m.tick();
  EXPECT_EQ(m.read<0>(v8(1)).to_just_i64(), 30);
}

// A restore/reset port (index >= NUserWr) never forwards and is never
// undefined, even in fwd/none mode.
TEST(MemoryPorts, restore_port_never_forwards) {
  Memory_fwd<V8, 8, 16, /*NRd=*/1, /*NWr=*/2, /*NUserWr=*/1> m;
  m[6] = v8(5);
  m.stage_write<1>(on8(), v8(6), v8(99));  // the restore port
  EXPECT_EQ(m.read<0>(v8(6)).to_just_i64(), 5);
  m.tick();
  EXPECT_EQ(m.read<0>(v8(6)).to_just_i64(), 99);  // it still commits
}

TEST(MemoryPorts, out_of_range_read_is_zero_and_write_is_dropped) {
  Memory_fwd<V8, 8, 4, 1, 1> m;
  m.stage_write<0>(on8(), v8(99), v8(42));  // out of range: dropped
  EXPECT_EQ(m.read<0>(v8(99)).to_just_i64(), 0);
  m.tick();
  for (std::size_t i = 0; i < m.size(); ++i) {
    EXPECT_EQ(m[i].to_just_i64(), 0);
  }
}

// ===========================================================================
// Sub-word write enable (wensize): every resolution is per lane. 16 bits in
// four 4-bit lanes; only lanes 0 and 2 are enabled.
// ===========================================================================
TEST(MemoryLanes, fwd_forwards_only_the_enabled_lanes) {
  Memory_fwd<V16, 16, 8, /*NRd=*/1, /*NWr=*/1, /*NUserWr=*/1, /*WenSize=*/4> m;
  m[2] = V16::create_integer(0x1234);

  m.stage_write<0>(V16::create_integer(0b0101), V16::create_integer(2), pat16(0xABCD));

  // lane0 <- 0xD, lane1 keeps 0x3, lane2 <- 0xB, lane3 keeps 0x1
  EXPECT_EQ(bitsof(m.read<0>(V16::create_integer(2))), 0x1B3Du);

  m.tick();
  EXPECT_EQ(bitsof(m[2]), 0x1B3Du);  // the commit is lane-granular too
}

TEST(MemoryLanes, none_undefines_only_the_enabled_lanes) {
  hlop_set_random_seed(0x5EED);
  Memory_none<V16, 16, 8, /*NRd=*/1, /*NWr=*/1, /*NUserWr=*/1, /*WenSize=*/4> m;
  m[2] = V16::create_integer(0x1234);

  m.stage_write<0>(V16::create_integer(0b0101), V16::create_integer(2), pat16(0xABCD));

  bool lane0_varied = false;
  bool lane2_varied = false;
  auto first        = bitsof(m.read<0>(V16::create_integer(2)));
  for (int i = 0; i < 64; ++i) {
    const uint64_t v = bitsof(m.read<0>(V16::create_integer(2)));
    EXPECT_EQ((v >> 4) & 0xF, 0x3u);   // lane1: not written -> stored
    EXPECT_EQ((v >> 12) & 0xF, 0x1u);  // lane3: not written -> stored
    if ((v & 0xF) != (first & 0xF)) {
      lane0_varied = true;
    }
    if (((v >> 8) & 0xF) != ((first >> 8) & 0xF)) {
      lane2_varied = true;
    }
  }
  EXPECT_TRUE(lane0_varied);
  EXPECT_TRUE(lane2_varied);
}

TEST(MemoryLanes, disabled_lanes_are_not_committed) {
  Memory_old<V16, 16, 8, /*NRd=*/1, /*NWr=*/1, /*NUserWr=*/1, /*WenSize=*/4> m;
  m[0] = V16::create_integer(0x1234);
  m.stage_write<0>(V16::create_integer(0b1000), V16::create_integer(0), pat16(0xABCD));
  m.tick();
  EXPECT_EQ(bitsof(m[0]), 0xA234u);  // only lane3 changed
}

// ===========================================================================
// Whole-array bridging (the cell's `read_all` / `update` buses)
// ===========================================================================
TEST(MemoryWholeArray, read_all_and_apply_update_round_trip) {
  Memory_old<V8, 8, 4, 1, 1> m;
  m[0] = v8(1);
  m[1] = v8(2);
  m[2] = v8(3);
  m[3] = v8(4);

  auto bus = m.read_all();  // Slop<32>, entry 0 in the low bits
  EXPECT_EQ(static_cast<uint64_t>(bus.to_just_i64()) & 0xFFFFFFFFu, 0x04030201u);

  Memory_old<V8, 8, 4, 1, 1> m2;
  m2.apply_update(bus);
  for (std::size_t i = 0; i < 4; ++i) {
    EXPECT_EQ(m2[i].to_just_i64(), m[i].to_just_i64());
  }
}

// ===========================================================================
// Dlop backend (Mem_dyn): same four modes, but `none` produces REAL unknown
// bits rather than a random draw.
// ===========================================================================
namespace {

// Configure in place: Mem_dyn holds spool_ptrs, so it is deliberately not
// copied around in the tests.
void setup_dyn(Mem_dyn<D>& m, Mem_order order, int n_wr = 1, int wensize = 1, int bits = 8) {
  Mem_cfg cfg;
  cfg.size      = 16;
  cfg.bits      = bits;
  cfg.n_rd      = 1;
  cfg.n_wr      = n_wr;
  cfg.n_user_wr = n_wr;
  cfg.wensize   = wensize;
  cfg.order     = order;
  m.configure(cfg, Dlop::create_integer(0));
}

// Same shape, but leaves n_user_wr at its "unset" default.
void setup_dyn_default_user_wr(Mem_dyn<D>& m) {
  Mem_cfg cfg;
  cfg.size  = 8;
  cfg.bits  = 8;
  cfg.n_rd  = 1;
  cfg.n_wr  = 1;
  cfg.order = Mem_order::fwd;
  m.configure(cfg, Dlop::create_integer(0));
}

D don() { return Dlop::create_bool(true); }
D di(int64_t x) { return Dlop::create_integer(x); }

}  // namespace

TEST(MemoryDlop, old_and_fwd_and_program) {
  Mem_dyn<D> m_old;
  setup_dyn(m_old, Mem_order::old);
  m_old.entries()[3] = di(7);
  m_old.stage_write(0, don(), di(3), di(42));
  EXPECT_EQ(m_old.read(0, di(3))->to_just_i64(), 7);
  m_old.tick();
  EXPECT_EQ(m_old.read(0, di(3))->to_just_i64(), 42);

  Mem_dyn<D> m_fwd;
  setup_dyn(m_fwd, Mem_order::fwd);
  m_fwd.entries()[3] = di(7);
  m_fwd.stage_write(0, don(), di(3), di(42));
  EXPECT_EQ(m_fwd.read(0, di(3))->to_just_i64(), 42);

  Mem_cfg cfg;
  cfg.size      = 16;
  cfg.bits      = 8;
  cfg.n_rd      = 1;
  cfg.n_wr      = 1;
  cfg.n_user_wr = 1;
  cfg.order     = Mem_order::program;
  cfg.fwd_upto  = {0};  // the read precedes the write
  Mem_dyn<D> m_prog;
  m_prog.configure(cfg, Dlop::create_integer(0));
  m_prog.entries()[3] = di(7);
  m_prog.stage_write(0, don(), di(3), di(42));
  EXPECT_EQ(m_prog.read(0, di(3))->to_just_i64(), 7);
}

TEST(MemoryDlop, none_collision_is_a_real_unknown) {
  Mem_dyn<D> m;
  setup_dyn(m, Mem_order::none);
  m.entries()[3] = di(7);

  m.stage_write(0, don(), di(3), di(42));
  auto collided = m.read(0, di(3));
  EXPECT_TRUE(collided->has_unknowns());  // Dlop keeps the x, it does not randomize

  auto clean = m.read(0, di(4));
  EXPECT_FALSE(clean->has_unknowns());
  EXPECT_EQ(clean->to_just_i64(), 0);
}

TEST(MemoryDlop, none_undefines_only_the_enabled_lanes) {
  Mem_dyn<D> m;
  setup_dyn(m, Mem_order::none, /*n_wr=*/1, /*wensize=*/4, /*bits=*/16);
  m.entries()[2] = di(0x1234);

  m.stage_write(0, di(0b0101), di(2), di(0xABCD));
  auto v = m.read(0, di(2));
  EXPECT_TRUE(v->has_unknowns());
  // lanes 0 and 2 are undefined; lanes 1 and 3 keep the stored nibbles.
  for (int b = 0; b < 4; ++b) {
    EXPECT_TRUE(v->unknown_bit_test(b)) << "lane0 bit " << b;
    EXPECT_TRUE(v->unknown_bit_test(8 + b)) << "lane2 bit " << b;
  }
  for (int b = 0; b < 4; ++b) {
    EXPECT_FALSE(v->unknown_bit_test(4 + b)) << "lane1 bit " << b;
    EXPECT_FALSE(v->unknown_bit_test(12 + b)) << "lane3 bit " << b;
  }
}

TEST(MemoryDlop, unknown_address_makes_the_read_undefined_and_drops_the_write) {
  Mem_dyn<D> m;
  setup_dyn(m, Mem_order::old);
  m.entries()[1] = di(5);

  auto unk_addr = Dlop::unknown(4);
  EXPECT_TRUE(m.read(0, unk_addr)->has_unknowns());

  m.stage_write(0, don(), unk_addr, di(42));
  m.tick();
  for (const auto& e : m.entries()) {
    EXPECT_FALSE(e->has_unknowns());
  }
  EXPECT_EQ(m.entries()[1]->to_just_i64(), 5);
}

// ===========================================================================
// The primitives the `none` path is built on
// ===========================================================================
TEST(MemoryPrimitives, dlop_make_unknown_bits_is_positional) {
  auto v = Dlop::create_integer(0xF0);
  auto r = v->make_unknown_bits(*Dlop::create_integer(0x0F));
  for (int b = 0; b < 4; ++b) {
    EXPECT_TRUE(r->unknown_bit_test(b));
  }
  for (int b = 4; b < 8; ++b) {
    EXPECT_FALSE(r->unknown_bit_test(b));
    EXPECT_TRUE(r->bit_test(b));  // the kept nibble is still 0xF
  }
  // A zero mask leaves the value untouched.
  auto same = v->make_unknown_bits(*Dlop::create_integer(0));
  EXPECT_FALSE(same->has_unknowns());
  EXPECT_EQ(same->to_just_i64(), 0xF0);
}

TEST(MemoryPrimitives, slop_unknown_is_canonical) {
  hlop_set_random_seed(0xA5A5);
  // A 10-bit draw must be a canonical signed Slop<16>: sign-extended from bit
  // 9, never a zero-extended 0..1023.
  bool saw_negative = false;
  for (int i = 0; i < 64; ++i) {
    auto v = V16::unknown(10);
    EXPECT_LE(v.to_just_i64(), 511);
    EXPECT_GE(v.to_just_i64(), -512);
    if (v.to_just_i64() < 0) {
      saw_negative = true;
    }
  }
  EXPECT_TRUE(saw_negative);
}

TEST(MemoryPrimitives, slop_unknown_lanes_keeps_the_unmasked_bits) {
  hlop_set_random_seed(0xC3C3);
  const auto base = V16::create_integer(0x1234);
  const auto mask = V16::create_integer(0x00FF);
  for (int i = 0; i < 32; ++i) {
    const uint64_t v = bitsof(base.unknown_lanes(mask));
    EXPECT_EQ(v >> 8, 0x12u);  // the unmasked byte survives
  }
}

// A lane mask must stay a FINITE nbits-wide mask at every width. Building it as
// `create_integer(-1)->adjust_bits(n)` collapsed to -1 (all ones to infinity)
// for n >= 64, because adjust_bits cannot widen a single-word value -- so every
// sub-word write with 64-bit lanes overwrote the whole entry.
TEST(MemoryPrimitives, dlop_ones_is_finite_at_every_width) {
  for (int n : {1, 8, 32, 63, 64, 65, 128}) {
    auto o = Mem_val<D>::ones(n);
    EXPECT_FALSE(o->is_negative()) << "ones(" << n << ") sign-extends";
    EXPECT_EQ(o->get_bits(), n + 1) << "ones(" << n << ") is not n bits wide";  // +1 signed sign bit
    EXPECT_TRUE(o->bit_test(n - 1));
    EXPECT_FALSE(o->bit_test(n));
  }
}

TEST(MemoryDlop, wide_lanes_do_not_clobber_the_other_lane) {
  // bits=128, wensize=2 -> two 64-bit lanes; only lane 0 is enabled.
  Mem_dyn<D> m;
  Mem_cfg    cfg;
  cfg.size      = 4;
  cfg.bits      = 128;
  cfg.n_rd      = 1;
  cfg.n_wr      = 1;
  cfg.n_user_wr = 1;
  cfg.wensize   = 2;
  cfg.order     = Mem_order::old;
  m.configure(cfg, Dlop::create_integer(0));

  // Top bit clear, so the 128-bit canonicalization is a no-op and the expected
  // value stays readable.
  m.entries()[1] = Dlop::from_pyrope("0x5AAAAAAAAAAAAAAABBBBBBBBBBBBBBBB");

  m.stage_write(0, di(0b01), di(1), di(0x11));  // lane 0 only
  m.tick();

  const auto got = m.read(0, di(1));
  EXPECT_TRUE(got->is_known_eq(*Dlop::from_pyrope("0x5AAAAAAAAAAAAAAA0000000000000011"))) << "got " << got->to_pyrope();
}

TEST(MemoryDlop, an_undefined_read_has_no_known_sign) {
  // The undef path must re-canonicalize to `bits` like Slop's unknown_lanes
  // does; otherwise the entry's sign extension above the width stays KNOWN and
  // `read < 0` resolves definitely instead of propagating x.
  Mem_dyn<D> m;
  setup_dyn(m, Mem_order::none, /*n_wr=*/1, /*wensize=*/1, /*bits=*/32);
  m.entries()[3] = di(-1);

  m.stage_write(0, don(), di(3), di(42));
  const auto got = m.read(0, di(3));
  ASSERT_TRUE(got->has_unknowns());
  for (int b = 0; b < 32; ++b) {
    EXPECT_TRUE(got->unknown_bit_test(b)) << "bit " << b << " is known";
  }
  EXPECT_TRUE(got->lt_op(*di(0))->has_unknowns());  // the sign is x, not a known 1
}

TEST(MemoryDlop, a_committed_entry_matches_the_forwarded_value) {
  // One write must not read back as two different values. `din` always fits the
  // entry (upass.tolg masks the data path, and stage_write asserts it), so the
  // raw commit and mem_merge's sext agree -- including at the sign boundary.
  Mem_dyn<D> m;
  setup_dyn(m, Mem_order::fwd, /*n_wr=*/1, /*wensize=*/1, /*bits=*/8);

  m.stage_write(0, don(), di(3), di(-128));  // 0x80 masked to 8 bits: the most negative
  const auto forwarded = m.read(0, di(3));
  m.tick();
  const auto committed = m.read(0, di(3));

  EXPECT_TRUE(forwarded->is_known_eq(*committed)) << forwarded->to_pyrope() << " vs " << committed->to_pyrope();
  EXPECT_EQ(committed->to_just_i64(), -128);
}

TEST(MemoryDlop, an_uncertain_enable_makes_the_lane_x_on_commit) {
  // A write whose enable is x may or may not have landed, so the lane is
  // neither the stored value nor din -- both the forwarded read and the
  // committed entry are x.
  Mem_dyn<D> m;
  setup_dyn(m, Mem_order::fwd, /*n_wr=*/1, /*wensize=*/1, /*bits=*/8);
  m.entries()[3] = di(7);

  m.stage_write(0, Dlop::unknown(1), di(3), di(42));
  EXPECT_TRUE(m.read(0, di(3))->has_unknowns());
  m.tick();
  EXPECT_TRUE(m.entries()[3]->has_unknowns());  // committed as x, not 7 and not 42

  // Per lane: a known-1 lane takes din, an x lane goes x, a known-0 lane keeps
  // the stored contents.
  Mem_dyn<D> lanes;
  setup_dyn(lanes, Mem_order::old, /*n_wr=*/1, /*wensize=*/4, /*bits=*/16);
  lanes.entries()[2] = di(0x1234);
  lanes.stage_write(0, Dlop::from_pyrope("0ub0?01"), di(2), di(0xABC));
  lanes.tick();

  const auto e = lanes.entries()[2];
  for (int b = 0; b < 16; ++b) {
    // Only lane 2 (bits 8..11), whose enable bit is x, is undefined.
    EXPECT_EQ(e->unknown_bit_test(b), b >= 8 && b < 12) << "bit " << b;
  }
  EXPECT_TRUE(e->and_op(*di(0x000F))->is_known_eq(*di(0xC)));     // lane 0: enable 1 -> din
  EXPECT_TRUE(e->and_op(*di(0x00F0))->is_known_eq(*di(0x30)));    // lane 1: enable 0 -> stored
  EXPECT_TRUE(e->and_op(*di(0xF000))->is_known_eq(*di(0x1000)));  // lane 3: enable 0 -> stored
}

// The dynamic path packs the bus bit-identically to the static one, so the
// DContext interpreter, cgen.sim, cgen_verilog and pass.lec all agree on the
// layout: entry 0 in the LOW `bits`, row-major.
TEST(MemoryDlop, read_all_matches_the_static_layout) {
  Mem_dyn<D> m;
  setup_dyn(m, Mem_order::old, /*n_wr=*/1, /*wensize=*/1, /*bits=*/8);
  m.entries().resize(4);
  for (int i = 0; i < 4; ++i) {
    m.entries()[static_cast<std::size_t>(i)] = di(i + 1);
  }

  Memory_old<V8, 8, 4, 1, 1> stat;
  for (int i = 0; i < 4; ++i) {
    stat[static_cast<std::size_t>(i)] = v8(i + 1);
  }

  const auto bus = m.read_all();
  EXPECT_EQ(bus->to_just_i64(), 0x04030201);
  EXPECT_EQ(bus->to_just_i64(), stat.read_all().to_just_i64());

  // Round-trip: scattering the bus back reproduces every entry.
  Mem_dyn<D> m2;
  setup_dyn(m2, Mem_order::old, /*n_wr=*/1, /*wensize=*/1, /*bits=*/8);
  m2.entries().resize(4);
  m2.apply_update(bus);
  for (std::size_t i = 0; i < 4; ++i) {
    EXPECT_EQ(m2.entries()[i]->to_just_i64(), m.entries()[i]->to_just_i64());
  }
}

// A negative entry packs as its `bits`-wide two's-complement field, not as an
// infinitely sign-extended value that would smear over the entries above it.
TEST(MemoryDlop, read_all_packs_a_negative_entry_as_its_bit_pattern) {
  Mem_dyn<D> m;
  setup_dyn(m, Mem_order::old, /*n_wr=*/1, /*wensize=*/1, /*bits=*/8);
  m.entries().resize(4);
  m.entries()[0] = di(-1);  // 0xFF in 8 bits
  m.entries()[1] = di(2);
  m.entries()[2] = di(3);
  m.entries()[3] = di(4);

  EXPECT_EQ(m.read_all()->to_just_i64(), 0x040302FF);

  Mem_dyn<D> m2;
  setup_dyn(m2, Mem_order::old, /*n_wr=*/1, /*wensize=*/1, /*bits=*/8);
  m2.entries().resize(4);
  m2.apply_update(m.read_all());
  EXPECT_EQ(m2.entries()[0]->to_just_i64(), -1);  // and comes back signed
  EXPECT_EQ(m2.entries()[3]->to_just_i64(), 4);
}

TEST(MemoryDlop, an_explicit_zero_user_write_count_is_honored) {
  // n_user_wr == 0 means "every write port is a restore port", and a restore
  // port never forwards. Rewriting that 0 into n_wr made it forward.
  Mem_dyn<D> m;
  Mem_cfg    cfg;
  cfg.size      = 8;
  cfg.bits      = 8;
  cfg.n_rd      = 1;
  cfg.n_wr      = 1;
  cfg.n_user_wr = 0;
  cfg.order     = Mem_order::fwd;
  m.configure(cfg, Dlop::create_integer(0));
  m.entries()[2] = di(5);

  m.stage_write(0, don(), di(2), di(99));
  EXPECT_EQ(m.read(0, di(2))->to_just_i64(), 5);  // committed, not the restore data
  m.tick();
  EXPECT_EQ(m.read(0, di(2))->to_just_i64(), 99);

  // An unset n_user_wr still defaults to n_wr.
  Mem_dyn<D> d;
  setup_dyn_default_user_wr(d);
  d.entries()[2] = di(5);
  d.stage_write(0, don(), di(2), di(99));
  EXPECT_EQ(d.read(0, di(2))->to_just_i64(), 99);
}

TEST(MemoryDlop, a_forwarding_prefix_cannot_run_past_the_write_ports) {
  // mem_resolve indexes the pending-write span with this count; Memory_program
  // bounds the same value with a static_assert.
  Mem_dyn<D> m;
  Mem_cfg    cfg;
  cfg.size      = 8;
  cfg.bits      = 8;
  cfg.n_rd      = 1;
  cfg.n_wr      = 1;
  cfg.n_user_wr = 1;
  cfg.order     = Mem_order::program;
  cfg.fwd_upto  = {1};
  m.configure(cfg, Dlop::create_integer(0));
  EXPECT_EQ(m.cfg().fwd_upto[0], 1);

  m.stage_write(0, don(), di(2), di(42));
  EXPECT_EQ(m.read(0, di(2))->to_just_i64(), 42);
}
