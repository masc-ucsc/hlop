//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// memory.hpp: the LiveHD Memory cell, one C++ type per same-cycle ordering mode
//
// LiveHD's Memory cell carries TWO comptime per-(read-port,write-port) matrices,
// `fwd` (pid 5) and `undef` (pid 15), bit (r*n_wr + w). They are mutually
// exclusive per pair and encode three states for a same-cycle same-address
// read/write collision on read port r against write port w:
//
//   fwd=1          -> read port r sees write port w's NEW data
//   undef=1        -> read port r's data is UNDEFINED
//   both 0         -> read port r sees the COMMITTED (old) contents
//
// upass.tolg builds both matrices from the Pyrope `ordering` attribute, and in
// every case a row is a PREFIX over the user write ports (upass_tolg.cpp
// ~3585). That is what makes four separate C++ types practical -- a row never
// needs a bit vector, only a count:
//
//   ordering     fwd_upto(r)          undef_upto(r)   type
//   ---------    ------------------   -------------   ---------------
//   "old"        0                    0               Memory_old
//   "fwd"        n_user_wr            0               Memory_fwd
//   "program"    rd_wr_before[r]      0               Memory_program
//   "none"       0                    n_user_wr       Memory_none
//
// The write ports are ordered so the n_user_wr USER ports come first and the
// reset/restore ports follow; a restore port never forwards and is never
// undefined, which falls out of the prefixes being capped at n_user_wr.
//
// -- Cycle protocol ----------------------------------------------------------
// Writes are STAGED, not applied, so a read can resolve against them:
//
//   mem.stage_write<0>(wen0, addr0, din0);   // every write port, comb walk
//   mem.stage_write<1>(wen1, addr1, din1);
//   auto d = mem.read<0>(raddr);             // latency-0: resolved value
//   ...
//   q0     = mem.read<0>(raddr);             // latency-1: latch the RESOLVED
//   mem.tick();                              //   value, then commit
//
// Both latencies resolve identically; latency 1 only differs in that the caller
// flops the result. This matches the cgen_memory_*.v wrapper, where the read
// register latches `d0_fwd` (the post-FWD/UNDEF value), NOT the raw array.
//
// Write-write priority is the slot index: at tick() the ports are committed in
// ascending order, so the highest-numbered enabled port wins a same-address
// collision, per lane. Read resolution walks the same direction for the same
// reason.
//
// -- Undefined values --------------------------------------------------------
// Slop has no x, so an undefined lane is filled from the seeded process PRNG
// (`hlop_random_seed`): a run stays reproducible under --seed while a design
// that leans on a collision value still breaks loudly. Dlop has real unknown
// bits, so it produces a genuine base/extra unknown plane -- the same thing
// cgen_verilog emits as `x` and pass.lec models as an X bit-plane. A fresh draw
// is taken per read; the value is deliberately NOT memoized per (addr,cycle),
// because two reads of one undefined location are not required to agree.
//
// -- Sub-word write enable (wensize) -----------------------------------------
// With WenSize > 1 the write enable is a WenSize-bit lane vector and every
// resolution is per lane: a lane whose enable bit is 0 falls through to the
// stored contents even when the port forwards, exactly like the Verilog
// wrapper's per-MASKSIZE always_comb blocks.
//
// An enable bit is THREE-valued on the Dlop backend. A known 1 writes its lane,
// a known 0 leaves the stored contents, and an x makes the lane UNDEFINED: the
// write may or may not have landed, so the lane is neither the old value nor
// `din` -- the same thing pass.lec's symbolic enable models. That x is committed
// at tick() as well, not just seen by a forwarding read. Slop's enable is always
// definite, so this case cannot arise there.

#pragma once

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include "dlop.hpp"
#include "slop.hpp"
#include "spool_ptr.hpp"

namespace hlop {

// Same-cycle read/write ordering, mirroring the Pyrope `ordering` attribute and
// upass.tolg's Mem_info::Mem_order. Used by the runtime-configured Mem_dyn; the
// static path encodes the mode in the C++ type instead.
enum class Mem_order : uint8_t { old, fwd, program, none };

// ---------------------------------------------------------------------------
// Backend adapter
// ---------------------------------------------------------------------------
// Slop is a value type with member ops; Dlop is reached through spool_ptr, so
// every op needs `->`. Everything backend-specific in this file goes through
// Mem_val so the resolution logic below is written once.
template <class V>
struct Mem_val;

template <int N>
struct Mem_val<Slop<N>> {
  using V = Slop<N>;

  static V       zero() { return V::create_integer(0); }
  static V       ones(int nbits) { return V::create_integer(-1).adjust_bits(nbits); }
  // No x in Slop: an undefined value is a fresh draw from the seeded PRNG.
  static V       undef(int nbits) { return V::unknown(nbits); }
  static V       or_(const V& a, const V& b) { return a.or_op(b); }
  static V       and_(const V& a, const V& b) { return a.and_op(b); }
  static V       not_(const V& a) { return a.not_op(); }
  static V       shl_(const V& a, int64_t n) { return a.shl_op(n); }
  static V       sra_(const V& a, int64_t n) { return a.sra_op(n); }
  static V       sext_(const V& a, int from_bit) { return a.sext_op(from_bit); }
  // unknown_lanes already re-canonicalizes to N bits.
  static V       undef_lanes(const V& a, const V& mask, int) { return a.unknown_lanes(mask); }
  static bool    bit_test(const V& a, int pos) { return a.bit_test(pos); }
  static bool    truthy(const V& a) { return a.is_known_true(); }
  // Slop carries no unknowns, so an enable is always definitely on or definitely
  // off -- never uncertain, and no individual enable bit is ever x.
  static bool    uncertain(const V&) { return false; }
  static bool    bit_unknown(const V&, int) { return false; }
  static int     nbits(const V& a) { return a.get_bits(); }
  // Slop never carries unknowns, so an address is always a usable integer.
  static bool    addr_known(const V& a) { return a.is_just_i64(); }
  static int64_t to_i64(const V& a) { return a.to_just_i64(); }
};

template <>
struct Mem_val<spool_ptr<Dlop>> {
  using V = spool_ptr<Dlop>;

  static V zero() { return Dlop::create_integer(0); }
  // ~(-1 << nbits), NOT `create_integer(-1)->adjust_bits(nbits)`: adjust_bits
  // cannot widen a single-word value, so masking -1 to nbits >= 64 hands back
  // -1 (all ones to infinity) and every lane mask built from it covers the
  // whole entry. The shift/not form stays a finite nbits-wide mask at any width.
  static V ones(int nbits) { return Dlop::create_integer(-1)->shl_op(*Dlop::create_integer(nbits))->not_op(); }
  // Dlop has a real unknown (base/extra) plane -- the `x` cgen_verilog emits.
  static V undef(int nbits) { return Dlop::unknown(nbits); }
  static V or_(const V& a, const V& b) { return a->or_op(*b); }
  static V and_(const V& a, const V& b) { return a->and_op(*b); }
  static V not_(const V& a) { return a->not_op(); }
  // The int64 shift/sext kernels are protected; the public API takes a Dlop.
  static V shl_(const V& a, int64_t n) { return a->shl_op(*Dlop::create_integer(n)); }
  static V sra_(const V& a, int64_t n) { return a->sra_op(*Dlop::create_integer(n)); }
  static V sext_(const V& a, int from_bit) { return a->sext_op(*Dlop::create_integer(from_bit)); }
  // Re-canonicalize to `bits` like Slop's unknown_lanes does: without it the
  // untouched sign extension above the entry width stays KNOWN, so a fully
  // undefined read of a negative entry still has a definite sign.
  static V undef_lanes(const V& a, const V& mask, int bits) {
    auto v = a->make_unknown_bits(*mask);
    return bits > 0 ? sext_(v, bits - 1) : v;
  }
  static bool    bit_test(const V& a, int pos) { return a->bit_test(pos); }
  static bool    truthy(const V& a) { return a->is_known_true(); }
  // Neither definitely on nor definitely off: the write MAY have happened, so
  // its lanes are neither the old nor the new value -- they are x.
  static bool    uncertain(const V& a) { return !a->is_known_true() && !a->is_known_false(); }
  static bool    bit_unknown(const V& a, int pos) { return a->unknown_bit_test(pos); }
  static int     nbits(const V& a) { return a->get_bits(); }
  static bool    addr_known(const V& a) { return !a->has_unknowns() && a->is_just_i64(); }
  static int64_t to_i64(const V& a) { return a->to_just_i64(); }
};

// Declared entry width of a backend value type: the fixed N for Slop<N>, and -1
// for Dlop (arbitrary precision). Lets Mem_base reject a Slop whose width does
// not match the cell's `bits` pin -- a codegen slip that would otherwise only
// show up as silently truncated entries.
template <class V>
struct Mem_width {
  static constexpr int value = -1;
};
template <int N>
struct Mem_width<Slop<N>> {
  static constexpr int value = N;
};

// ---------------------------------------------------------------------------
// One write port's staged state for the current cycle
// ---------------------------------------------------------------------------
// `lanes` is the write enable already expanded to an entry-width BIT MASK, so
// resolution never has to know the enable's own width (the address and the
// enable are separate, narrower buses in the generated code).
//
// `xlanes` holds the lanes whose ENABLE BIT IS UNKNOWN. Such a lane is neither
// the stored value nor `din` -- the write may or may not have happened -- so it
// resolves and commits as x. Slop has no unknown enable, so `xlanes` is always
// empty there and the mask costs nothing.
template <class V>
struct Mem_write {
  V       din{};
  V       lanes{};    // entry-width mask of the bits this port definitely writes
  V       xlanes{};   // entry-width mask of the bits whose write is uncertain
  int64_t addr  = 0;  // resolved, in-range; meaningful only when `fired`
  bool    fired = false;

  void clear() { fired = false; }
};

// ---------------------------------------------------------------------------
// Resolution primitives, shared by the static and dynamic containers
// ---------------------------------------------------------------------------

// The two entry-width lane masks a staged write carries.
template <class V>
struct Mem_lanes {
  V on{};  // the enable bit is a known 1: the lane takes `din`
  V x{};   // the enable bit is unknown: the lane becomes x
};

// Expand a write enable into those masks. `wensize == 1` means the enable is a
// plain bit covering the whole entry. `E` is the enable's own value type, which
// in generated code is a narrower Slop than the entry.
//
// An enable bit is three-valued for Dlop: a known 1 writes its lane, a known 0
// leaves the stored contents, and an x makes the lane UNDEFINED -- the write may
// or may not have landed, so the lane is neither the old value nor the new one.
// Slop's enable is always definite, so `x` stays empty there.
template <class V, class E>
Mem_lanes<V> mem_lane_masks(const E& wen, int bits, int wensize) {
  using val  = Mem_val<V>;
  using eval = Mem_val<E>;
  if (wensize <= 1) {
    if (eval::truthy(wen)) {
      return {val::ones(bits), val::zero()};  // definitely on
    }
    if (eval::uncertain(wen)) {
      return {val::zero(), val::ones(bits)};  // may or may not have written
    }
    return {val::zero(), val::zero()};  // definitely off
  }
  const int    lane_bits = bits / wensize;
  Mem_lanes<V> m{val::zero(), val::zero()};
  for (int l = 0; l < wensize; ++l) {
    const bool unknown = eval::bit_unknown(wen, l);
    if (!unknown && !eval::bit_test(wen, l)) {
      continue;  // known 0
    }
    const V lane = val::shl_(val::ones(lane_bits), static_cast<int64_t>(l) * lane_bits);
    if (unknown) {
      m.x = val::or_(m.x, lane);
    } else {
      m.on = val::or_(m.on, lane);
    }
  }
  return m;
}

// Bring a staged `din` to the entry's width, so the forwarding path and the
// commit path see one and the same value.
//
// Slop carries its width in the type (Mem_base static_asserts Mem_width == Bits)
// and upass.tolg emits a get_mask/wrap on the data path, so the two widths must
// already match -- a wider `din` is a compiler bug, not a case with defined
// semantics. Dlop has no width of its own, so the memory imposes one, signed,
// exactly like mem_merge below. A NARROWER `din` is fine either way: it is just
// a sign-extended entry.
template <class V>
V mem_canon_din(const V& din, int bits) {
  using val = Mem_val<V>;
  if constexpr (Mem_width<V>::value > 0) {
    assert(val::nbits(din) <= bits && "Memory `din` is wider than the entry -- missing get_mask/wrap on the data path");
    (void)bits;
    return din;
  } else {
    return bits > 0 ? val::sext_(din, bits - 1) : din;
  }
}

// Bits selected by `mask` come from `src`, the rest from `dst`; the result is
// re-canonicalized to a signed `bits`-wide value (Slop's invariant, and what
// every downstream op assumes).
template <class V>
V mem_merge(const V& dst, const V& src, const V& mask, int bits) {
  using val     = Mem_val<V>;
  const V kept  = val::and_(dst, val::not_(mask));
  const V taken = val::and_(src, mask);
  return val::sext_(val::or_(kept, taken), bits - 1);
}

// Resolve read address `raddr` against the staged writes. `fwd_upto` /
// `undef_upto` are the read port's prefix lengths over the user write ports.
template <class V>
V mem_resolve(const V& stored, std::span<const Mem_write<V>> pend, int64_t raddr, int fwd_upto, int undef_upto, int bits) {
  using val      = Mem_val<V>;
  V         v    = stored;
  const int upto = fwd_upto > undef_upto ? fwd_upto : undef_upto;
  for (int w = 0; w < upto; ++w) {
    const auto& p = pend[static_cast<std::size_t>(w)];
    if (!p.fired || p.addr != raddr) {
      continue;
    }
    // UNDEF is checked before FWD within a port (the matrices are mutually
    // exclusive per pair, so in practice only one prefix is ever non-zero).
    if (w < undef_upto) {
      // Both the definitely-written and the uncertain lanes are undefined here.
      v = val::undef_lanes(v, val::or_(p.lanes, p.xlanes), bits);  // Slop: random; Dlop: x
    } else {
      v = mem_merge<V>(v, p.din, p.lanes, bits);
      if (val::truthy(p.xlanes)) {
        v = val::undef_lanes(v, p.xlanes, bits);  // the write MAY have landed
      }
    }
  }
  return v;
}

// ---------------------------------------------------------------------------
// Shared storage + port plumbing (compile-time shape)
// ---------------------------------------------------------------------------
// Bits/Size/NRd/NWr are compile-time so the emitted simulator keeps a bare
// std::array and unrolls the per-port loops. NUserWr splits the user write
// ports (which may forward) from the trailing reset/restore ports (which never
// do). See Mem_dyn below for the runtime-sized variant.
template <class V, int Bits, std::size_t Size, int NRd, int NWr, int NUserWr, int WenSize>
class Mem_base {
  static_assert(Bits > 0, "memory entry width must be positive");
  static_assert(Size > 0, "memory must have at least one entry");
  static_assert(NRd >= 0 && NWr >= 0, "port counts cannot be negative");
  static_assert(NUserWr >= 0 && NUserWr <= NWr, "user write ports are a prefix of all write ports");
  static_assert(WenSize >= 1, "wensize must be at least 1");
  static_assert(Bits % WenSize == 0, "wensize must divide the entry width into equal lanes");
  static_assert(Mem_width<V>::value < 0 || Mem_width<V>::value == Bits,
                "the value type's width must match the memory's `bits` (use Slop<Bits>)");

public:
  using value_type = V;
  using array_type = std::array<V, Size>;
  using val        = Mem_val<V>;

  static constexpr int         bits        = Bits;
  static constexpr int         n_rd        = NRd;
  static constexpr int         n_wr        = NWr;
  static constexpr int         n_user_wr   = NUserWr;
  static constexpr int         wensize     = WenSize;
  static constexpr int         lane_bits   = Bits / WenSize;
  static constexpr std::size_t entry_count = Size;

  static constexpr std::size_t size() { return Size; }

  // --- Storage access (checkpoint, VCD, whole-array bridging) ---
  array_type&       entries() { return data_; }
  const array_type& entries() const { return data_; }
  V&                operator[](std::size_t i) { return data_[i]; }
  const V&          operator[](std::size_t i) const { return data_[i]; }
  auto              begin() { return data_.begin(); }
  auto              begin() const { return data_.begin(); }
  auto              end() { return data_.end(); }
  auto              end() const { return data_.end(); }

  // Seed every entry. Required before first use when V is a spool_ptr (the
  // default-constructed pointer is null); Slop default-constructs to integer 0,
  // so cgen.sim's reset path is the only caller there.
  void fill(const V& v) {
    data_.fill(v);
    for (auto& p : pend_) {
      p.din    = v;
      p.lanes  = v;
      p.xlanes = v;
      p.clear();
    }
  }

  // Drop every staged write without committing it. Used when a whole-array
  // reset overrides this cycle's per-port writes.
  void clear_pending() {
    for (auto& p : pend_) {
      p.clear();
    }
  }

  // --- Whole-array bridging (the Memory cell's `read_all` / `update` buses) ---
  // Entry 0 in the LOW bits, row-major -- identical to cgen_verilog and
  // pass.lec, so LEC and sim agree. Slop-only here (the bus width has to be a
  // template parameter); Mem_dyn carries the same layout for either backend.
  auto read_all() const { return slop_read_all(data_); }
  template <int W>
  void apply_update(const Slop<W>& bus) {
    slop_apply_update(data_, bus);
  }

  // --- Stage a write for this cycle ---
  // `wen` is the WenSize-bit lane enable (a plain 1-bit enable when WenSize==1)
  // and `addr` is the address bus; both are their own (narrower) value types in
  // generated code, so neither is tied to the entry width.
  //
  // A write with no enabled lane, an unknown address, or an out-of-range address
  // does not fire: it neither commits nor participates in collision resolution.
  template <int W, class E, class A>
  void stage_write(const E& wen, const A& addr, const V& din) {
    static_assert(W >= 0 && W < NWr, "write port index out of range");
    stage_write_(W, wen, addr, din);
  }
  template <class E, class A>
  void stage_write(int w, const E& wen, const A& addr, const V& din) {
    assert(w >= 0 && w < NWr);
    stage_write_(w, wen, addr, din);
  }

  // --- Commit the staged writes and start a new cycle ---
  // Ascending port order, so the highest-numbered enabled port wins a
  // same-address collision (per lane), matching cgen_memory_*.v.
  void tick() {
    for (int w = 0; w < NWr; ++w) {
      auto& p = pend_[static_cast<std::size_t>(w)];
      if (!p.fired) {
        continue;
      }
      auto& slot = data_[static_cast<std::size_t>(p.addr)];
      if (val::truthy(p.xlanes)) {
        // An uncertain enable commits x, not the old value and not `din`.
        slot = val::undef_lanes(mem_merge<V>(slot, p.din, p.lanes, Bits), p.xlanes, Bits);
      } else if constexpr (WenSize == 1) {
        // No canonicalization: `din` is asserted to fit in `Bits` at staging,
        // which makes this identical to what mem_merge would produce.
        slot = p.din;
      } else {
        slot = mem_merge<V>(slot, p.din, p.lanes, Bits);
      }
      p.clear();
    }
  }

protected:
  // Out-of-range reads return 0, matching the pre-existing hlop and cgen.sim
  // behavior (the Verilog wrapper cannot have an out-of-range address). An
  // unknown address makes the whole read undefined.
  template <class A>
  V read_at_(const A& addr, int fwd_upto, int undef_upto) const {
    if (!Mem_val<A>::addr_known(addr)) {
      return val::undef(Bits);
    }
    const int64_t a = Mem_val<A>::to_i64(addr);
    if (a < 0 || static_cast<std::size_t>(a) >= Size) {
      return val::zero();
    }
    const V& stored = data_[static_cast<std::size_t>(a)];
    if constexpr (NWr == 0) {
      return stored;
    } else {
      if (fwd_upto == 0 && undef_upto == 0) {
        return stored;  // ordering="old": never touches the staged writes
      }
      return mem_resolve<V>(stored, std::span<const Mem_write<V>>{pend_}, a, fwd_upto, undef_upto, Bits);
    }
  }

  template <class E, class A>
  void stage_write_(int w, const E& wen, const A& addr, const V& din) {
    auto& p = pend_[static_cast<std::size_t>(w)];
    p.clear();
    if (!Mem_val<A>::addr_known(addr)) {
      return;
    }
    const int64_t a = Mem_val<A>::to_i64(addr);
    if (a < 0 || static_cast<std::size_t>(a) >= Size) {
      return;  // out of range: the write is dropped, as in the pre-existing hlop
    }
    auto lanes = mem_lane_masks<V, E>(wen, Bits, WenSize);
    if (!val::truthy(lanes.on) && !val::truthy(lanes.x)) {
      return;  // no enabled and no uncertain lane
    }
    p.din    = mem_canon_din<V>(din, Bits);
    p.lanes  = lanes.on;
    p.xlanes = lanes.x;
    p.addr   = a;
    p.fired  = true;
  }

  array_type                    data_{};
  std::array<Mem_write<V>, NWr> pend_{};
};

// ---------------------------------------------------------------------------
// ordering="old"
// ---------------------------------------------------------------------------
// Nothing forwards and a colliding read is the DEFINED committed value. This is
// what every Verilog reader produces (a nonblocking write is never visible to a
// same-timestep read) and what a whole-array cell always gets.
template <class V, int Bits, std::size_t Size, int NRd, int NWr, int NUserWr = NWr, int WenSize = 1>
class Memory_old : public Mem_base<V, Bits, Size, NRd, NWr, NUserWr, WenSize> {
  using base = Mem_base<V, Bits, Size, NRd, NWr, NUserWr, WenSize>;

public:
  static constexpr Mem_order order = Mem_order::old;

  template <int R, class A>
  V read(const A& addr) const {
    static_assert(R >= 0 && R < NRd, "read port index out of range");
    return base::read_at_(addr, 0, 0);
  }
  template <class A>
  V read(int r, const A& addr) const {
    assert(r >= 0 && r < NRd);
    (void)r;
    return base::read_at_(addr, 0, 0);
  }
};

// ---------------------------------------------------------------------------
// ordering="fwd"
// ---------------------------------------------------------------------------
// Pure transparency: every read of an address written this cycle returns the
// new data, regardless of the read's textual position.
template <class V, int Bits, std::size_t Size, int NRd, int NWr, int NUserWr = NWr, int WenSize = 1>
class Memory_fwd : public Mem_base<V, Bits, Size, NRd, NWr, NUserWr, WenSize> {
  using base = Mem_base<V, Bits, Size, NRd, NWr, NUserWr, WenSize>;

public:
  static constexpr Mem_order order = Mem_order::fwd;

  template <int R, class A>
  V read(const A& addr) const {
    static_assert(R >= 0 && R < NRd, "read port index out of range");
    return base::read_at_(addr, NUserWr, 0);
  }
  template <class A>
  V read(int r, const A& addr) const {
    assert(r >= 0 && r < NRd);
    (void)r;
    return base::read_at_(addr, NUserWr, 0);
  }
};

// ---------------------------------------------------------------------------
// ordering="none"
// ---------------------------------------------------------------------------
// No ordering hardware: a read of an address written this same cycle is
// UNDEFINED (Slop: a PRNG draw; Dlop: real unknown bits; emitted Verilog: x;
// formal: a don't-care). Reads of non-written addresses are the stored value.
template <class V, int Bits, std::size_t Size, int NRd, int NWr, int NUserWr = NWr, int WenSize = 1>
class Memory_none : public Mem_base<V, Bits, Size, NRd, NWr, NUserWr, WenSize> {
  using base = Mem_base<V, Bits, Size, NRd, NWr, NUserWr, WenSize>;

public:
  static constexpr Mem_order order = Mem_order::none;

  template <int R, class A>
  V read(const A& addr) const {
    static_assert(R >= 0 && R < NRd, "read port index out of range");
    return base::read_at_(addr, 0, NUserWr);
  }
  template <class A>
  V read(int r, const A& addr) const {
    assert(r >= 0 && r < NRd);
    (void)r;
    return base::read_at_(addr, 0, NUserWr);
  }
};

// ---------------------------------------------------------------------------
// ordering="program" (the Pyrope default)
// ---------------------------------------------------------------------------
// Sequential same-cycle semantics: read port r forwards exactly the writes that
// textually PRECEDE it. `Fwd_upto[r]` is upass.tolg's rd_wr_before[r].
template <class V, int Bits, std::size_t Size, int NRd, int NWr, int NUserWr, int WenSize,
          std::array<uint16_t, static_cast<std::size_t>(NRd)> Fwd_upto>
class Memory_program : public Mem_base<V, Bits, Size, NRd, NWr, NUserWr, WenSize> {
  using base = Mem_base<V, Bits, Size, NRd, NWr, NUserWr, WenSize>;

public:
  static constexpr Mem_order order = Mem_order::program;

  template <int R, class A>
  V read(const A& addr) const {
    static_assert(R >= 0 && R < NRd, "read port index out of range");
    static_assert(Fwd_upto[static_cast<std::size_t>(R)] <= NUserWr, "forwarding prefix exceeds the user write ports");
    return base::read_at_(addr, static_cast<int>(Fwd_upto[static_cast<std::size_t>(R)]), 0);
  }
  template <class A>
  V read(int r, const A& addr) const {
    assert(r >= 0 && r < NRd);
    return base::read_at_(addr, static_cast<int>(Fwd_upto[static_cast<std::size_t>(r)]), 0);
  }
};

// ---------------------------------------------------------------------------
// Runtime-configured memory (the dcontext / interpreter path)
// ---------------------------------------------------------------------------
// Same semantics as the four static types, with the shape and the mode read
// from the Memory cell's comptime pins at run time. The static types are what
// cgen.sim emits; this is what a traversal-driven consumer uses when the shape
// is only known once the graph is walked.
struct Mem_cfg {
  std::size_t           size      = 0;
  int                   bits      = 0;
  int                   n_rd      = 0;
  int                   n_wr      = 0;
  // Leading write ports that may forward / be undefined; the rest are
  // reset/restore ports, which never do. -1 means "unset": configure() fills it
  // with n_wr. A caller that means "no user write ports" passes an explicit 0.
  int                   n_user_wr = -1;
  int                   wensize   = 1;
  Mem_order             order     = Mem_order::program;
  std::vector<uint16_t> fwd_upto;  // per read port; only read when order == program
};

template <class V>
class Mem_dyn {
public:
  using value_type = V;
  using val        = Mem_val<V>;

  Mem_dyn() = default;

  // Enforces at run time what Mem_base states with a static_assert; a shape the
  // static twin refuses to compile must not be silently mis-simulated here.
  void configure(const Mem_cfg& cfg, const V& init) {
    cfg_ = cfg;
    if (cfg_.n_user_wr < 0 || cfg_.n_user_wr > cfg_.n_wr) {
      cfg_.n_user_wr = cfg_.n_wr;
    }
    assert(cfg_.bits > 0 && "memory entry width must be positive");
    if (cfg_.bits < 1) {
      cfg_.bits = 1;  // matches cgen_sim's `if (m.bits <= 0) m.bits = 1;`
    }
    if (cfg_.wensize < 1) {
      cfg_.wensize = 1;
    }
    // A wensize that does not divide the width leaves the top bits of every
    // entry permanently unwritable (mem_lane_mask truncates lane_bits).
    // cgen_sim makes this a fatal diag; fall back to a whole-entry enable.
    assert(cfg_.bits % cfg_.wensize == 0 && "wensize must divide the entry width into equal lanes");
    if (cfg_.bits % cfg_.wensize != 0) {
      cfg_.wensize = 1;
    }
    // `program` needs one prefix per read port; a caller that omits them gets
    // ordering="old" behavior, which is a silent sim/LEC divergence.
    assert((cfg_.order != Mem_order::program || cfg_.fwd_upto.size() == static_cast<std::size_t>(cfg_.n_rd))
           && "Mem_order::program needs a fwd_upto entry per read port");
    cfg_.fwd_upto.resize(static_cast<std::size_t>(cfg_.n_rd), 0);
    // mem_resolve indexes pend_ with these counts; Memory_program bounds the
    // same value with a static_assert.
    for (auto& f : cfg_.fwd_upto) {
      assert(f <= static_cast<uint16_t>(cfg_.n_user_wr) && "forwarding prefix exceeds the user write ports");
      if (f > static_cast<uint16_t>(cfg_.n_user_wr)) {
        f = static_cast<uint16_t>(cfg_.n_user_wr);
      }
    }
    data_.assign(cfg_.size, init);
    // Seed the staged slots with a real value rather than a default-constructed
    // one: for the Dlop backend V is a spool_ptr, whose default is null and
    // whose copy constructor asserts on null.
    pend_.clear();
    pend_.resize(static_cast<std::size_t>(cfg_.n_wr));
    for (auto& p : pend_) {
      p.din    = init;
      p.lanes  = init;
      p.xlanes = init;
    }
    last_access_  = init;  // nothing has driven a port yet
    bulk_         = init;  // seeded so the Dlop spool_ptr is never null
    bulk_pending_ = false;
    configured_   = true;
  }

  bool           configured() const { return configured_; }
  const Mem_cfg& cfg() const { return cfg_; }

  // The last value driven on any of this memory's ports: the most recent
  // resolved read dout, or the most recent staged write din.
  //
  // A read port whose enable is low never accesses the array, so its dout HOLDS
  // -- the bus simply does not switch. Reporting a fresh x (or a PRNG draw)
  // instead would be logically defensible but would invent switching activity a
  // gated port cannot have, which matters to anything estimating power.
  const V& last_access() const { return last_access_; }

  std::vector<V>&       entries() { return data_; }
  const std::vector<V>& entries() const { return data_; }
  std::size_t           size() const { return data_.size(); }

  void fill(const V& v) { data_.assign(data_.size(), v); }
  void clear_pending() {
    for (auto& p : pend_) {
      p.clear();
    }
  }

  // --- Whole-array bridging (the Memory cell's `read_all` / `update` buses) ---
  // Entry 0 in the LOW `bits`, row-major -- identical to Mem_base's Slop-only
  // slop_read_all/slop_apply_update, and to cgen_verilog and pass.lec, so LEC
  // and sim agree. Unlike those, this works for Dlop too: the bus is just a
  // `size * bits` wide value, which an arbitrary-precision type can hold.
  V read_all() const {
    V bus = val::zero();
    for (std::size_t i = 0; i < data_.size(); ++i) {
      const V field = val::and_(data_[i], val::ones(cfg_.bits));  // the entry's bit pattern
      bus           = val::or_(bus, val::shl_(field, static_cast<int64_t>(i) * cfg_.bits));
    }
    return bus;
  }

  void apply_update(const V& bus) {
    for (std::size_t i = 0; i < data_.size(); ++i) {
      // Each slice is truncated and sign-fit to the canonical signed entry,
      // matching Mem_base's `Slop<B>{bus.sra_op(i * B)}`.
      data_[i] = val::sext_(val::sra_(bus, static_cast<int64_t>(i) * cfg_.bits), cfg_.bits - 1);
    }
  }

  // Stage a whole-array next-state for this cycle. Applied at tick() BEFORE the
  // per-port writes, so the priority is reset > per-port write > update, exactly
  // as cgen_sim emits it. A reset also drops the staged per-port writes rather
  // than letting them land on top of the restored contents.
  void stage_bulk(const V& bus, bool is_reset) {
    bulk_          = bus;
    bulk_pending_  = true;
    bulk_is_reset_ = is_reset;
  }

  template <class E, class A>
  void stage_write(int w, const E& wen, const A& addr, const V& din) {
    if (w < 0 || static_cast<std::size_t>(w) >= pend_.size()) {
      return;
    }
    auto& p = pend_[static_cast<std::size_t>(w)];
    p.clear();
    if (!Mem_val<A>::addr_known(addr)) {
      return;
    }
    const int64_t a = Mem_val<A>::to_i64(addr);
    if (a < 0 || static_cast<std::size_t>(a) >= data_.size()) {
      return;
    }
    auto lanes = mem_lane_masks<V, E>(wen, cfg_.bits, cfg_.wensize);
    if (!val::truthy(lanes.on) && !val::truthy(lanes.x)) {
      return;  // no enabled and no uncertain lane
    }
    p.din        = mem_canon_din<V>(din, cfg_.bits);
    p.lanes      = lanes.on;
    p.xlanes     = lanes.x;
    p.addr       = a;
    p.fired      = true;
    last_access_ = p.din;
  }

  template <class A>
  V read(int r, const A& addr) const {
    last_access_ = read_(r, addr);
    return last_access_;
  }

  void tick() {
    // The bulk update lands FIRST, so the per-port writes below override the
    // entries they touch; a reset outranks them and drops them entirely.
    if (bulk_pending_) {
      apply_update(bulk_);
      if (bulk_is_reset_) {
        clear_pending();
      }
      bulk_pending_ = false;
    }
    for (auto& p : pend_) {
      if (!p.fired) {
        continue;
      }
      auto& slot = data_[static_cast<std::size_t>(p.addr)];
      if (val::truthy(p.xlanes)) {
        // An uncertain enable commits x, not the old value and not `din`.
        slot = val::undef_lanes(mem_merge<V>(slot, p.din, p.lanes, cfg_.bits), p.xlanes, cfg_.bits);
      } else if (cfg_.wensize <= 1) {
        // No canonicalization: `din` is canonicalized to `bits` at staging,
        // which makes this identical to what mem_merge would produce.
        slot = p.din;
      } else {
        slot = mem_merge<V>(slot, p.din, p.lanes, cfg_.bits);
      }
      p.clear();
    }
  }

private:
  template <class A>
  V read_(int r, const A& addr) const {
    if (!Mem_val<A>::addr_known(addr)) {
      return val::undef(cfg_.bits > 0 ? cfg_.bits : 64);
    }
    const int64_t a = Mem_val<A>::to_i64(addr);
    if (a < 0 || static_cast<std::size_t>(a) >= data_.size()) {
      return val::zero();
    }
    const auto [fwd_upto, undef_upto] = prefixes_(r);
    if (fwd_upto == 0 && undef_upto == 0) {
      return data_[static_cast<std::size_t>(a)];
    }
    return mem_resolve<V>(data_[static_cast<std::size_t>(a)],
                          std::span<const Mem_write<V>>{pend_},
                          a,
                          fwd_upto,
                          undef_upto,
                          cfg_.bits);
  }

  std::pair<int, int> prefixes_(int r) const {
    switch (cfg_.order) {
      case Mem_order::old : return {0, 0};
      case Mem_order::fwd : return {cfg_.n_user_wr, 0};
      case Mem_order::none: return {0, cfg_.n_user_wr};
      case Mem_order::program:
        if (r >= 0 && static_cast<std::size_t>(r) < cfg_.fwd_upto.size()) {
          return {static_cast<int>(cfg_.fwd_upto[static_cast<std::size_t>(r)]), 0};
        }
        return {0, 0};
    }
    return {0, 0};
  }

  Mem_cfg                   cfg_{};
  std::vector<V>            data_;
  std::vector<Mem_write<V>> pend_;
  // Observational only (what the port bus last drove), so `read()` stays const.
  mutable V                 last_access_{};
  V                         bulk_{};  // staged whole-array next-state; see stage_bulk
  bool                      bulk_pending_  = false;
  bool                      bulk_is_reset_ = false;
  bool                      configured_    = false;
};

}  // namespace hlop
