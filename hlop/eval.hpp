//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// eval.hpp: Shared semantic kernels for hlop execution layer
//
// These templated kernels are used by both:
//   - dlop (dynamic path): called via DContext adapter
//   - slop (static path): called directly with Slop<N> values
//
// The value type V is typically spool_ptr<Dlop> or Slop<N>.

#pragma once

#include <cassert>
#include <cstdint>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "dlop.hpp"
#include "slop.hpp"

namespace hlop {

// =========================================================================
// Argument structs for multi-sink ops
// =========================================================================

template <class V>
struct SumArgs {
  std::span<const V> plus;
  std::span<const V> minus;
};

template <class S, class V>
struct MuxArgs {
  const S&           sel;
  std::span<const V> data;
};

template <class V>
struct LutArgs {
  const V&           lut_val;  // truth table as a value
  std::span<const V> inputs;
};

// =========================================================================
// Stateful argument structs
// =========================================================================

template <class V>
struct FlopArgs {
  const V& din;
  const V& clock_pin;
  const V* enable    = nullptr;
  const V* reset_pin = nullptr;
  const V* initial   = nullptr;
  const V* async_    = nullptr;
  const V* posclk    = nullptr;
  const V* negreset  = nullptr;
};

template <class V>
struct LatchArgs {
  const V& din;
  const V& enable;
  const V* posclk = nullptr;
};

template <class V>
struct MemoryReadArgs {
  const V& addr;
  const V& enable;
  const V* fwd = nullptr;
};

template <class V>
struct MemoryWriteArgs {
  const V& addr;
  const V& data;
  const V& enable;
  const V* wmask = nullptr;
};

// =========================================================================
// State containers
// =========================================================================

// Dynamic state container for dlop (keyed by string state_id)
template <class V>
struct DynState {
  V curr;
  V next;
};

template <class V>
class DynStateMap {
  std::unordered_map<std::string, DynState<V>> flops_;
  std::unordered_map<std::string, DynState<V>> latches_;

public:
  DynState<V>& get_flop(const std::string& id, const V& init) {
    auto it = flops_.find(id);
    if (it == flops_.end()) {
      it = flops_.emplace(id, DynState<V>{init, init}).first;
    }
    return it->second;
  }

  DynState<V>& get_latch(const std::string& id, const V& init) {
    auto it = latches_.find(id);
    if (it == latches_.end()) {
      it = latches_.emplace(id, DynState<V>{init, init}).first;
    }
    return it->second;
  }

  void advance_clock() {
    for (auto& [id, st] : flops_) {
      st.curr = st.next;
    }
    for (auto& [id, st] : latches_) {
      st.curr = st.next;
    }
  }
};

// Static state container for slop (index-based)
template <class V>
struct RegState {
  std::vector<V> curr;
  std::vector<V> next;

  RegState() = default;
  RegState(size_t n, const V& init) : curr(n, init), next(n, init) {}

  void advance_clock() { curr = next; }
};

// Memory state for slop
template <class V>
struct MemState {
  std::vector<V> curr;
  std::vector<V> next;
  bool           fwd = false;

  MemState() = default;
  MemState(size_t depth, const V& init, bool fwd_enable = false) : curr(depth, init), next(depth, init), fwd(fwd_enable) {}

  void advance_clock() { curr = next; }
};

// =========================================================================
// Pure kernels: single-sink ops
// =========================================================================

// --- Or: bitwise OR of all inputs ---
template <class V>
V eval_or(std::span<const V> inputs) {
  assert(!inputs.empty());
  V result = inputs[0];
  for (size_t i = 1; i < inputs.size(); ++i) {
    result = result.or_op(inputs[i]);
  }
  return result;
}

// --- And: bitwise AND of all inputs ---
template <class V>
V eval_and(std::span<const V> inputs) {
  assert(!inputs.empty());
  V result = inputs[0];
  for (size_t i = 1; i < inputs.size(); ++i) {
    result = result.and_op(inputs[i]);
  }
  return result;
}

// --- Xor: bitwise XOR of all inputs ---
template <class V>
V eval_xor(std::span<const V> inputs) {
  assert(!inputs.empty());
  V result = inputs[0];
  for (size_t i = 1; i < inputs.size(); ++i) {
    result = result.xor_op(inputs[i]);
  }
  return result;
}

// --- Ror: reduction-OR. Each input is reduced to 1-bit (OR of all its bits),
//     then all 1-bit results are OR'd together. Result is 1-bit. ---
template <class V>
V eval_ror(std::span<const V> inputs) {
  assert(!inputs.empty());
  for (size_t i = 0; i < inputs.size(); ++i) {
    if (inputs[i].is_known_true()) {
      return V::create_bool(true);
    }
  }
  // If any input has unknowns and no known-true input exists, result is unknown
  for (size_t i = 0; i < inputs.size(); ++i) {
    if (inputs[i].has_unknowns()) {
      return V::unknown(1);
    }
  }
  return V::create_bool(false);
}

// --- Mult: multiply all inputs ---
template <class V>
V eval_mult(std::span<const V> inputs) {
  assert(!inputs.empty());
  V result = inputs[0];
  for (size_t i = 1; i < inputs.size(); ++i) {
    result = result.mult_op(inputs[i]);
  }
  return result;
}

// --- Not: bitwise complement (unary) ---
template <class V>
V eval_not(const V& value) {
  return value.not_op();
}

// --- Div: signed division (binary, not commutative) ---
template <class V>
V eval_div(const V& a, const V& b) {
  return a.div_op(b);
}

// --- LT: signed less-than (binary) -> boolean ---
template <class V>
V eval_lt(const V& a, const V& b) {
  return a.lt_op(b);
}

// --- EQ: equality (binary) -> boolean ---
template <class V>
V eval_eq(const V& a, const V& b) {
  return a.eq_op(b);
}

// --- Sext: sign-extend from bit position ---
template <class V>
V eval_sext(const V& value, int bits) {
  return value.sext_op(bits);
}

// --- Get_mask: extract bits where mask is set ---
template <class V>
V eval_get_mask(const V& value, const V& mask) {
  // get_mask extracts the mask from the value
  // For positive mask: just AND with the mask (simplified for common case)
  // The full semantic: extract bits at positions where mask is 1, pack them
  return value.and_op(mask);
}

// --- Set_mask: scatter operation (inverse of get_mask) ---
// set_mask(base, mask, value): replaces bits in base at positions where mask is 1
// with bits packed from value.
template <class V>
V eval_set_mask(const V& base, const V& mask, const V& value) {
  // set_mask(base, 0, value) -> base
  if (mask.is_known_false()) {
    return base;
  }
  // set_mask(base, -1, value) -> value (all bits replaced)
  // For the general case: clear masked bits in base, then OR in value bits at mask positions
  // Simplified implementation for common cases:
  // result = (base AND NOT mask) OR (value AND mask)
  auto not_mask = mask.not_op();
  auto cleared  = base.and_op(not_mask);
  auto masked_v = value.and_op(mask);
  return cleared.or_op(masked_v);
}

// --- SHL: shift left ---
template <class V>
V eval_shl(const V& value, const V& amount) {
  if (amount.has_unknowns()) {
    return V::unknown(value.get_bits() + 64);  // conservative
  }
  assert(amount.is_i());
  return value.shl_op(amount.to_i());
}

// --- SRA: arithmetic shift right ---
template <class V>
V eval_sra(const V& value, const V& amount) {
  if (amount.has_unknowns()) {
    return V::unknown(value.get_bits());  // conservative
  }
  assert(amount.is_i());
  return value.sra_op(amount.to_i());
}

// =========================================================================
// Pure kernels: multi-sink ops
// =========================================================================

// --- Sum: A inputs contribute positively, B inputs contribute negatively ---
template <class V>
V eval_sum(const SumArgs<V>& args) {
  assert(!args.plus.empty() || !args.minus.empty());

  V result = V::create_integer(0);

  for (size_t i = 0; i < args.plus.size(); ++i) {
    result = result.add_op(args.plus[i]);
  }
  for (size_t i = 0; i < args.minus.size(); ++i) {
    result = result.sub_op(args.minus[i]);
  }

  return result;
}

// --- Mux: selector picks one of the data inputs ---
template <class S, class V>
V eval_mux(const MuxArgs<S, V>& args) {
  assert(!args.data.empty());

  if (args.sel.has_unknowns()) {
    // Conservative: OR all possible outputs, mark unknown
    V result = args.data[0];
    for (size_t i = 1; i < args.data.size(); ++i) {
      result = result.or_op(args.data[i]);
    }
    return V::unknown(result.get_bits());
  }

  assert(args.sel.is_i());
  int64_t idx = args.sel.to_i();
  if (idx < 0 || static_cast<size_t>(idx) >= args.data.size()) {
    return V::create_integer(0);  // out of range
  }
  return args.data[idx];
}

// Convenience: same-type selector and data
template <class V>
V eval_mux(const MuxArgs<V, V>& args) {
  return eval_mux<V, V>(args);
}

// --- LUT: truth-table lookup ---
template <class V>
V eval_lut(const LutArgs<V>& args) {
  assert(!args.inputs.empty());

  // Check for unknowns in inputs
  for (size_t i = 0; i < args.inputs.size(); ++i) {
    if (args.inputs[i].has_unknowns()) {
      return V::unknown(1);
    }
  }

  // Build index from input bits
  int64_t index = 0;
  for (size_t i = 0; i < args.inputs.size(); ++i) {
    if (args.inputs[i].is_known_true()) {
      index |= (int64_t(1) << i);
    }
  }

  // Look up the truth table bit
  if (args.lut_val.bit_test(static_cast<int>(index))) {
    return V::create_bool(true);
  }
  return V::create_bool(false);
}

// =========================================================================
// Stateful kernels
// =========================================================================

// --- Flop: edge-triggered register ---
// Returns current value. Schedules din -> next (based on clock/enable).
// Write becomes visible only after advance_clock().
template <class V, class State>
V eval_flop(State& st, uint32_t slot, const FlopArgs<V>& args) {
  assert(slot < st.curr.size());

  // Handle reset
  if (args.reset_pin && args.reset_pin->is_known_true()) {
    bool neg_reset = args.negreset && args.negreset->is_known_true();
    if (!neg_reset) {
      // Active-high reset is asserted
      V init_val    = args.initial ? *args.initial : V::create_integer(0);
      st.next[slot] = init_val;
      if (args.async_ && args.async_->is_known_true()) {
        // Async reset: immediately visible
        st.curr[slot] = init_val;
        return init_val;
      }
      return st.curr[slot];
    }
  }
  if (args.reset_pin && args.reset_pin->is_known_false()) {
    bool neg_reset = args.negreset && args.negreset->is_known_true();
    if (neg_reset) {
      // Active-low reset, signal is low -> reset asserted
      V init_val    = args.initial ? *args.initial : V::create_integer(0);
      st.next[slot] = init_val;
      if (args.async_ && args.async_->is_known_true()) {
        st.curr[slot] = init_val;
        return init_val;
      }
      return st.curr[slot];
    }
  }

  // Check clock edge (simplified: posclk means we trigger on clock_pin == 1)
  bool clock_active = args.clock_pin.is_known_true();
  bool pos_edge     = !args.posclk || args.posclk->is_known_true();
  if (!pos_edge) {
    clock_active = args.clock_pin.is_known_false();
  }

  if (clock_active) {
    // Check enable
    bool enabled = !args.enable || args.enable->is_known_true();
    if (enabled) {
      st.next[slot] = args.din;
    }
  }

  return st.curr[slot];
}

// --- Latch: level-sensitive ---
// When enable is active, output follows din. When inactive, holds last value.
template <class V, class State>
V eval_latch(State& st, uint32_t slot, const LatchArgs<V>& args) {
  assert(slot < st.curr.size());

  bool active_high = !args.posclk || args.posclk->is_known_true();

  bool is_transparent;
  if (active_high) {
    is_transparent = args.enable.is_known_true();
  } else {
    is_transparent = args.enable.is_known_false();
  }

  if (is_transparent) {
    st.curr[slot] = args.din;
    st.next[slot] = args.din;
  }

  return st.curr[slot];
}

// --- Fflop: fluid flop (simplified - treat as flop with valid/stop handshake) ---
// For now, same as flop. Exact semantics TBD per implementation plan.
template <class V, class State>
V eval_fflop(State& st, uint32_t slot, const FlopArgs<V>& args) {
  return eval_flop(st, slot, args);
}

// =========================================================================
// Memory kernels
// =========================================================================

template <class V>
V eval_memory_read(MemState<V>& mem, const MemoryReadArgs<V>& args) {
  if (!args.enable.is_known_true()) {
    return V::create_integer(0);
  }

  if (args.addr.has_unknowns()) {
    return V::unknown(64);  // conservative
  }

  assert(args.addr.is_i());
  int64_t addr = args.addr.to_i();

  if (addr < 0 || static_cast<size_t>(addr) >= mem.curr.size()) {
    return V::create_integer(0);  // out of range
  }

  // Check fwd flag: if set, reads see pending writes
  bool do_fwd = mem.fwd && args.fwd && args.fwd->is_known_true();
  if (do_fwd) {
    return mem.next[addr];
  }

  return mem.curr[addr];
}

template <class V>
void eval_memory_write(MemState<V>& mem, const MemoryWriteArgs<V>& args) {
  if (!args.enable.is_known_true()) {
    return;
  }

  if (args.addr.has_unknowns()) {
    return;  // cannot write to unknown address
  }

  assert(args.addr.is_i());
  int64_t addr = args.addr.to_i();

  if (addr < 0 || static_cast<size_t>(addr) >= mem.curr.size()) {
    return;  // out of range
  }

  if (args.wmask) {
    // Partial write: set_mask semantics
    mem.next[addr] = eval_set_mask(mem.next[addr], *args.wmask, args.data);
  } else {
    mem.next[addr] = args.data;
  }
}

}  // namespace hlop
