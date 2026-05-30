//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "dcontext.hpp"

#include <algorithm>
#include <cassert>
#include <stdexcept>

namespace hlop {

// =========================================================================
// Input helpers
// =========================================================================

std::vector<DValue> DContext::collect_values(const std::vector<DInput>& inputs) {
  std::vector<DValue> vals;
  vals.reserve(inputs.size());
  for (auto& in : inputs) {
    vals.push_back(in.value);
  }
  return vals;
}

DValue DContext::find_pin(const std::vector<DInput>& inputs, const std::string& name) {
  for (auto& in : inputs) {
    if (in.pin == name) {
      return in.value;
    }
  }
  return {};
}

DValue DContext::find_pid(const std::vector<DInput>& inputs, int pid) {
  for (auto& in : inputs) {
    if (in.pid == pid) {
      return in.value;
    }
  }
  return {};
}

std::vector<DValue> DContext::collect_pin(const std::vector<DInput>& inputs, const std::string& name) {
  std::vector<DValue> vals;
  for (auto& in : inputs) {
    if (in.pin == name) {
      vals.push_back(in.value);
    }
  }
  return vals;
}

// =========================================================================
// Main dispatch
// =========================================================================

DResult DContext::execute(const DCall& call) {
  switch (call.op) {
    case Ntype_op::Sum     : return exec_sum(call);
    case Ntype_op::Mult    : return exec_mult(call);
    case Ntype_op::Div     : return exec_div(call);
    case Ntype_op::And     : return exec_and(call);
    case Ntype_op::Or      : return exec_or(call);
    case Ntype_op::Xor     : return exec_xor(call);
    case Ntype_op::Ror     : return exec_ror(call);
    case Ntype_op::Not     : return exec_not(call);
    case Ntype_op::Get_mask: return exec_get_mask(call);
    case Ntype_op::Set_mask: return exec_set_mask(call);
    case Ntype_op::Sext    : return exec_sext(call);
    case Ntype_op::LT      : return exec_lt(call);
    case Ntype_op::EQ      : return exec_eq(call);
    case Ntype_op::SHL     : return exec_shl(call);
    case Ntype_op::SRA     : return exec_sra(call);
    case Ntype_op::Mux     : return exec_mux(call);
    case Ntype_op::LUT     : return exec_lut(call);
    case Ntype_op::Flop    : return exec_flop(call);
    case Ntype_op::Latch   : return exec_latch(call);
    case Ntype_op::Fflop   : return exec_fflop(call);
    case Ntype_op::Memory  : return exec_memory(call);
    default                : assert(false && "Unsupported op in DContext::execute"); return {};
  }
}

void DContext::advance_clock() {
  for (auto& [id, st] : flop_state_) {
    st.curr = st.next;
  }
  for (auto& [id, st] : latch_state_) {
    st.curr = st.next;
  }
  for (auto& [id, ms] : memory_state_) {
    ms.curr = ms.next;
  }
}

// =========================================================================
// Pure ops
// =========================================================================

DResult DContext::exec_or(const DCall& call) {
  auto vals = collect_values(call.inputs);
  assert(!vals.empty());
  auto result = vals[0];
  for (size_t i = 1; i < vals.size(); ++i) {
    result = result->or_op(vals[i]);
  }
  return {.outputs = {result}};
}

DResult DContext::exec_and(const DCall& call) {
  auto vals = collect_values(call.inputs);
  assert(!vals.empty());
  auto result = vals[0];
  for (size_t i = 1; i < vals.size(); ++i) {
    result = result->and_op(vals[i]);
  }
  return {.outputs = {result}};
}

DResult DContext::exec_xor(const DCall& call) {
  auto vals = collect_values(call.inputs);
  assert(!vals.empty());
  auto result = vals[0];
  for (size_t i = 1; i < vals.size(); ++i) {
    result = result->xor_op(vals[i]);
  }
  return {.outputs = {result}};
}

DResult DContext::exec_ror(const DCall& call) {
  auto vals = collect_values(call.inputs);
  assert(!vals.empty());
  for (auto& v : vals) {
    if (v->is_known_true()) {
      return {.outputs = {Dlop::create_bool(true)}};
    }
  }
  for (auto& v : vals) {
    if (v->has_unknowns()) {
      return {.outputs = {Dlop::unknown(1)}};
    }
  }
  return {.outputs = {Dlop::create_bool(false)}};
}

DResult DContext::exec_mult(const DCall& call) {
  auto vals = collect_values(call.inputs);
  assert(!vals.empty());
  auto result = vals[0];
  for (size_t i = 1; i < vals.size(); ++i) {
    result = result->mult_op(vals[i]);
  }
  return {.outputs = {result}};
}

DResult DContext::exec_not(const DCall& call) {
  assert(!call.inputs.empty());
  auto result = call.inputs[0].value->not_op();
  return {.outputs = {result}};
}

DResult DContext::exec_div(const DCall& call) {
  assert(call.inputs.size() >= 2);
  auto a = call.inputs[0].value;
  auto b = call.inputs[1].value;
  return {.outputs = {a->div_op(b)}};
}

DResult DContext::exec_sum(const DCall& call) {
  auto plus_vals  = collect_pin(call.inputs, "A");
  auto minus_vals = collect_pin(call.inputs, "B");

  return {.outputs = {Dlop::sum_op(plus_vals, minus_vals)}};
}

DResult DContext::exec_lt(const DCall& call) {
  assert(call.inputs.size() >= 2);
  auto a = call.inputs[0].value;
  auto b = call.inputs[1].value;
  return {.outputs = {a->lt_op(b)}};
}

DResult DContext::exec_eq(const DCall& call) {
  assert(call.inputs.size() >= 2);
  auto a = call.inputs[0].value;
  auto b = call.inputs[1].value;
  return {.outputs = {a->eq_op(b)}};
}

DResult DContext::exec_sext(const DCall& call) {
  assert(call.inputs.size() >= 2);
  auto value = call.inputs[0].value;
  auto bits  = call.inputs[1].value;
  assert(bits->is_i());
  return {.outputs = {value->sext_op(static_cast<int>(bits->to_i()))}};
}

DResult DContext::exec_get_mask(const DCall& call) {
  assert(call.inputs.size() >= 2);
  auto value = call.inputs[0].value;
  auto mask  = call.inputs[1].value;
  return {.outputs = {value->and_op(mask)}};
}

DResult DContext::exec_set_mask(const DCall& call) {
  assert(call.inputs.size() >= 3);
  auto base  = call.inputs[0].value;
  auto mask  = call.inputs[1].value;
  auto value = call.inputs[2].value;

  if (mask->is_known_false()) {
    return {.outputs = {base}};
  }

  auto not_mask = mask->not_op();
  auto cleared  = base->and_op(not_mask);
  auto masked_v = value->and_op(mask);
  return {.outputs = {cleared->or_op(masked_v)}};
}

DResult DContext::exec_shl(const DCall& call) {
  assert(call.inputs.size() >= 2);
  auto value  = call.inputs[0].value;
  auto amount = call.inputs[1].value;
  if (amount->has_unknowns()) {
    return {.outputs = {Dlop::unknown(value->get_bits() + 64)}};
  }
  assert(amount->is_i());
  return {.outputs = {value->shl_op(amount->to_i())}};
}

DResult DContext::exec_sra(const DCall& call) {
  assert(call.inputs.size() >= 2);
  auto value  = call.inputs[0].value;
  auto amount = call.inputs[1].value;
  if (amount->has_unknowns()) {
    return {.outputs = {Dlop::unknown(value->get_bits())}};
  }
  assert(amount->is_i());
  return {.outputs = {value->sra_op(amount->to_i())}};
}

DResult DContext::exec_mux(const DCall& call) {
  // pid 0: selector, pid 1+: data options
  auto sel = find_pid(call.inputs, 0);
  assert(sel);

  std::vector<DValue> data;
  for (int i = 1;; ++i) {
    auto d = find_pid(call.inputs, i);
    if (!d) {
      break;
    }
    data.push_back(d);
  }
  assert(!data.empty());

  if (sel->has_unknowns()) {
    auto result = data[0];
    for (size_t i = 1; i < data.size(); ++i) {
      result = result->or_op(data[i]);
    }
    return {.outputs = {Dlop::unknown(result->get_bits())}};
  }

  assert(sel->is_i());
  int64_t idx = sel->to_i();
  if (idx < 0 || static_cast<size_t>(idx) >= data.size()) {
    return {.outputs = {Dlop::create_integer(0)}};
  }
  return {.outputs = {data[idx]}};
}

DResult DContext::exec_lut(const DCall& call) {
  // First input (pid 0 or first) is the LUT value (truth table)
  // Remaining inputs are the LUT inputs
  assert(call.inputs.size() >= 2);
  auto lut_val = call.inputs[0].value;

  for (size_t i = 1; i < call.inputs.size(); ++i) {
    if (call.inputs[i].value->has_unknowns()) {
      return {.outputs = {Dlop::unknown(1)}};
    }
  }

  int64_t index = 0;
  for (size_t i = 1; i < call.inputs.size(); ++i) {
    if (call.inputs[i].value->is_known_true()) {
      index |= (int64_t(1) << (i - 1));
    }
  }

  bool bit = lut_val->bit_test(static_cast<int>(index));
  return {.outputs = {Dlop::create_bool(bit)}};
}

// =========================================================================
// Stateful ops
// =========================================================================

DResult DContext::exec_flop(const DCall& call) {
  assert(!call.state_id.empty());

  auto din       = find_pin(call.inputs, "din");
  auto clock_pin = find_pin(call.inputs, "clock_pin");
  assert(din);
  assert(clock_pin);

  auto enable   = find_pin(call.inputs, "enable");
  auto reset    = find_pin(call.inputs, "reset_pin");
  auto initial  = find_pin(call.inputs, "initial");
  auto async_v  = find_pin(call.inputs, "async");
  auto posclk   = find_pin(call.inputs, "posclk");
  auto negreset = find_pin(call.inputs, "negreset");

  // Initialize state if needed
  auto& st = flop_state_[call.state_id];
  if (!st.curr) {
    auto init_val = initial ? initial : Dlop::create_integer(0);
    st.curr       = init_val;
    st.next       = init_val;
  }

  // Handle reset
  if (reset && reset->is_known_true()) {
    bool neg_rst = negreset && negreset->is_known_true();
    if (!neg_rst) {
      auto init_val = initial ? initial : Dlop::create_integer(0);
      st.next       = init_val;
      if (async_v && async_v->is_known_true()) {
        st.curr = init_val;
        return {.outputs = {init_val}};
      }
      return {.outputs = {st.curr}};
    }
  }
  if (reset && reset->is_known_false()) {
    bool neg_rst = negreset && negreset->is_known_true();
    if (neg_rst) {
      auto init_val = initial ? initial : Dlop::create_integer(0);
      st.next       = init_val;
      if (async_v && async_v->is_known_true()) {
        st.curr = init_val;
        return {.outputs = {init_val}};
      }
      return {.outputs = {st.curr}};
    }
  }

  // Check clock
  bool clock_active = clock_pin->is_known_true();
  bool pos_edge     = !posclk || posclk->is_known_true();
  if (!pos_edge) {
    clock_active = clock_pin->is_known_false();
  }

  if (clock_active) {
    bool enabled = !enable || enable->is_known_true();
    if (enabled) {
      st.next = din;
    }
  }

  return {.outputs = {st.curr}};
}

DResult DContext::exec_latch(const DCall& call) {
  assert(!call.state_id.empty());

  auto din    = find_pin(call.inputs, "din");
  auto enable = find_pin(call.inputs, "enable");
  auto posclk = find_pin(call.inputs, "posclk");
  assert(din);
  assert(enable);

  auto& st = latch_state_[call.state_id];
  if (!st.curr) {
    st.curr = Dlop::create_integer(0);
    st.next = Dlop::create_integer(0);
  }

  bool active_high = !posclk || posclk->is_known_true();
  bool transparent = active_high ? enable->is_known_true() : enable->is_known_false();

  if (transparent) {
    st.curr = din;
    st.next = din;
  }

  return {.outputs = {st.curr}};
}

DResult DContext::exec_fflop(const DCall& call) {
  // Simplified: same as flop for now
  return exec_flop(call);
}

DResult DContext::exec_memory(const DCall& call) {
  assert(!call.state_id.empty());

  auto& ms = memory_state_[call.state_id];

  // Extract global config pins
  auto bits_v = find_pin(call.inputs, "bits");
  auto size_v = find_pin(call.inputs, "size");
  auto fwd_v  = find_pin(call.inputs, "fwd");

  // Initialize memory if needed
  if (!ms.initialized && size_v && bits_v) {
    ms.size   = size_v->to_i();
    ms.bits   = bits_v->to_i();
    ms.fwd    = fwd_v && fwd_v->is_known_true();
    auto zero = Dlop::create_integer(0);
    ms.curr.resize(ms.size, zero);
    ms.next.resize(ms.size, zero);
    ms.initialized = true;
  }

  if (!ms.initialized) {
    return {.outputs = {Dlop::create_integer(0)}};
  }

  // Process ports by pid blocks (11 pids per port)
  // Port layout: pid+0=addr, pid+2=clk_enable, pid+3=data, pid+4=wmask, pid+10=wen
  // Read ports: pid+0=addr, pid+3=enable, pid+5=fwd_flag

  // Collect all pid-based inputs, find the max pid to determine port count
  int max_pid = -1;
  for (auto& in : call.inputs) {
    if (in.pid >= 0 && in.pid > max_pid) {
      max_pid = in.pid;
    }
  }

  // Process write ports first (ports with wen), then read ports
  std::vector<DValue> read_outputs;

  for (int port_base = 0; port_base <= max_pid; port_base += 11) {
    auto addr = find_pid(call.inputs, port_base);
    if (!addr) {
      continue;
    }

    auto wen = find_pid(call.inputs, port_base + 10);
    if (wen && wen->is_known_true()) {
      // Write port
      auto clk_en = find_pid(call.inputs, port_base + 2);
      auto data   = find_pid(call.inputs, port_base + 3);
      auto wmask  = find_pid(call.inputs, port_base + 4);

      if (clk_en && clk_en->is_known_true() && data && addr->is_i()) {
        int64_t a = addr->to_i();
        if (a >= 0 && a < ms.size) {
          if (wmask) {
            auto not_mask = wmask->not_op();
            auto cleared  = ms.next[a]->and_op(not_mask);
            auto masked_v = data->and_op(wmask);
            ms.next[a]    = cleared->or_op(masked_v);
          } else {
            ms.next[a] = data;
          }
        }
      }
    } else {
      // Read port
      auto ren = find_pid(call.inputs, port_base + 3);

      if (ren && ren->is_known_true() && addr->is_i()) {
        int64_t a = addr->to_i();
        if (a >= 0 && a < ms.size) {
          auto fwd_flag = find_pid(call.inputs, port_base + 5);
          bool do_fwd   = ms.fwd && fwd_flag && fwd_flag->is_known_true();
          read_outputs.push_back(do_fwd ? ms.next[a] : ms.curr[a]);
        } else {
          read_outputs.push_back(Dlop::create_integer(0));
        }
      } else {
        read_outputs.push_back(Dlop::create_integer(0));
      }
    }
  }

  if (read_outputs.empty()) {
    read_outputs.push_back(Dlop::create_integer(0));
  }

  return {.outputs = read_outputs};
}

}  // namespace hlop
