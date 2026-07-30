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
    ms.tick();  // commit the staged writes
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
  assert(bits->is_just_i64());
  return {.outputs = {value->sext_op(bits)}};
}

DResult DContext::exec_get_mask(const DCall& call) {
  assert(call.inputs.size() >= 2);
  auto value = call.inputs[0].value;
  auto mask  = call.inputs[1].value;
  // get_mask is a gather/pack (extract the bits where mask==1 and pack them down
  // to the low bits), not a plain AND. Route to the canonical, tested op.
  return {.outputs = {value->get_mask_op(mask)}};
}

DResult DContext::exec_set_mask(const DCall& call) {
  assert(call.inputs.size() >= 3);
  auto base  = call.inputs[0].value;
  auto mask  = call.inputs[1].value;
  auto value = call.inputs[2].value;

  if (mask->is_known_false()) {
    return {.outputs = {base}};
  }

  // set_mask is a scatter (consume value's bits from bit 0 and place them into the
  // mask-selected positions), not an in-place (base&~mask)|(value&mask). Route to
  // the canonical, tested op.
  return {.outputs = {base->set_mask_op(mask, value)}};
}

DResult DContext::exec_shl(const DCall& call) {
  assert(call.inputs.size() >= 2);
  auto value  = call.inputs[0].value;
  auto amount = call.inputs[1].value;
  // shl_op(const Dlop&) handles the unknown / non-numeric amount cases.
  return {.outputs = {value->shl_op(amount)}};
}

DResult DContext::exec_sra(const DCall& call) {
  assert(call.inputs.size() >= 2);
  auto value  = call.inputs[0].value;
  auto amount = call.inputs[1].value;
  // sra_op(const Dlop&) handles the unknown / non-numeric amount cases.
  return {.outputs = {value->sra_op(amount)}};
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

  assert(sel->is_just_i64());
  int64_t idx = sel->to_just_i64();
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

// Memory sink pids are laid out in PORT BLOCKS of this stride (LiveHD
// graph/cell.hpp Ntype::Memory_port_stride): port i's per-port pin at base
// offset `off` lives at pid = i*kMemPortStride + off. The 0..stride-1 block
// also holds the cell-global pins, which is why the per-port offsets (0/3/4/10)
// and the global ones (1/2/5..9/11..15) never collide.
static constexpr int kMemPortStride = 16;

// Per-port offsets
static constexpr int kMemOffAddr   = 0;
static constexpr int kMemOffDin    = 3;
static constexpr int kMemOffEnable = 4;  // wensize-bit lane vector when wensize > 1
static constexpr int kMemOffRdport = 10;  // comptime: 1 = read port, 0 = write port

// The number of leading set bits in row `r` of a per-(read,write) matrix,
// bit (r*n_wr + w). Every `ordering` mode produces a PREFIX row (upass.tolg),
// so a count is lossless. Returns -1 when the row is NOT a prefix, which means
// the matrix did not come from an `ordering` attribute (a legacy numeric `fwd=`
// escape hatch could in principle produce one).
static int matrix_row_prefix(const DValue& m, int r, int n_wr) {
  if (!m) {
    return 0;
  }
  int prefix = 0;
  while (prefix < n_wr && m->bit_test(r * n_wr + prefix)) {
    ++prefix;
  }
  for (int w = prefix; w < n_wr; ++w) {
    if (m->bit_test(r * n_wr + w)) {
      return -1;  // a hole: not a prefix
    }
  }
  return prefix;
}

DResult DContext::exec_memory(const DCall& call) {
  assert(!call.state_id.empty());

  auto& ms = memory_state_[call.state_id];

  // ---- port census (one pass over the pid blocks) ----
  int max_pid = -1;
  for (auto& in : call.inputs) {
    if (in.pid > max_pid) {
      max_pid = in.pid;
    }
  }
  const int n_ports = max_pid < 0 ? 0 : (max_pid / kMemPortStride) + 1;

  std::vector<int> rd_of_port(static_cast<size_t>(n_ports), -1);  // read ordinal, or -1
  std::vector<int> wr_of_port(static_cast<size_t>(n_ports), -1);  // write ordinal, or -1
  int              n_rd = 0;
  int              n_wr = 0;
  for (int p = 0; p < n_ports; ++p) {
    const int base = p * kMemPortStride;
    if (!find_pid(call.inputs, base + kMemOffAddr)) {
      continue;  // this block is not a port
    }
    auto rdport = find_pid(call.inputs, base + kMemOffRdport);
    if (rdport && rdport->is_known_true()) {
      rd_of_port[static_cast<size_t>(p)] = n_rd++;
    } else {
      wr_of_port[static_cast<size_t>(p)] = n_wr++;
    }
  }

  // ---- configure on first sight ----
  if (!ms.configured()) {
    auto bits_v = find_pin(call.inputs, "bits");
    auto size_v = find_pin(call.inputs, "size");
    if (!bits_v || !size_v) {
      return {.outputs = {Dlop::create_integer(0)}};
    }
    auto wensize_v = find_pin(call.inputs, "wensize");
    auto fwd_v     = find_pin(call.inputs, "fwd");
    auto undef_v   = find_pin(call.inputs, "undef");

    Mem_cfg cfg;
    cfg.size    = static_cast<size_t>(size_v->to_just_i64());
    cfg.bits    = static_cast<int>(bits_v->to_just_i64());
    cfg.n_rd    = n_rd;
    cfg.n_wr    = n_wr;
    cfg.wensize = wensize_v && wensize_v->is_just_i64() ? static_cast<int>(wensize_v->to_just_i64()) : 1;

    // Classify the two matrices into one of the four ordering modes. Both are
    // per-(read,write) prefixes, so a row is just a count.
    std::vector<uint16_t> fwd_upto(static_cast<size_t>(n_rd), 0);
    std::vector<uint16_t> undef_upto(static_cast<size_t>(n_rd), 0);
    int                   max_fwd = 0, max_undef = 0;
    bool                  fwd_uniform = true;
    for (int r = 0; r < n_rd; ++r) {
      const int f = n_wr > 0 ? matrix_row_prefix(fwd_v, r, n_wr) : 0;
      const int u = n_wr > 0 ? matrix_row_prefix(undef_v, r, n_wr) : 0;
      // A non-prefix row cannot come from an `ordering` attribute. Fail closed
      // rather than silently simulating a different memory.
      assert(f >= 0 && "Memory `fwd` row is not a prefix -- not an `ordering` matrix");
      assert(u >= 0 && "Memory `undef` row is not a prefix -- not an `ordering` matrix");
      fwd_upto[static_cast<size_t>(r)]   = static_cast<uint16_t>(f < 0 ? 0 : f);
      undef_upto[static_cast<size_t>(r)] = static_cast<uint16_t>(u < 0 ? 0 : u);
      max_fwd                            = std::max(max_fwd, f < 0 ? 0 : f);
      max_undef                          = std::max(max_undef, u < 0 ? 0 : u);
      if (r > 0 && fwd_upto[static_cast<size_t>(r)] != fwd_upto[0]) {
        fwd_uniform = false;
      }
    }
    // The restore/reset write ports are the tail that no row ever names, so the
    // widest prefix IS the user write-port count.
    if (max_undef > 0) {
      cfg.order     = Mem_order::none;
      cfg.n_user_wr = max_undef;
    } else if (max_fwd == 0) {
      cfg.order     = Mem_order::old;
      cfg.n_user_wr = n_wr;
    } else if (fwd_uniform && max_fwd == fwd_upto[0]) {
      cfg.order     = Mem_order::fwd;
      cfg.n_user_wr = max_fwd;
    } else {
      cfg.order     = Mem_order::program;
      cfg.n_user_wr = max_fwd;
      cfg.fwd_upto  = fwd_upto;
    }

    ms.configure(cfg, Dlop::create_integer(0));
  }

  // ---- stage every write BEFORE resolving any read ----
  // The read's ordering row decides which of them it observes; that decision is
  // the mode's, not the call order's.
  for (int p = 0; p < n_ports; ++p) {
    const int w = wr_of_port[static_cast<size_t>(p)];
    if (w < 0) {
      continue;
    }
    const int base   = p * kMemPortStride;
    auto      addr   = find_pid(call.inputs, base + kMemOffAddr);
    auto      din    = find_pid(call.inputs, base + kMemOffDin);
    auto      enable = find_pid(call.inputs, base + kMemOffEnable);
    if (!addr || !din) {
      continue;
    }
    ms.stage_write(w, enable ? enable : Dlop::create_bool(true), addr, din);
  }

  // ---- resolve the reads ----
  std::vector<DValue> read_outputs;
  for (int p = 0; p < n_ports; ++p) {
    const int r = rd_of_port[static_cast<size_t>(p)];
    if (r < 0) {
      continue;
    }
    const int base   = p * kMemPortStride;
    auto      addr   = find_pid(call.inputs, base + kMemOffAddr);
    auto      enable = find_pid(call.inputs, base + kMemOffEnable);
    if (enable && !enable->is_known_true()) {
      read_outputs.push_back(Dlop::create_integer(0));
      continue;
    }
    read_outputs.push_back(ms.read(r, addr));
  }

  if (read_outputs.empty()) {
    read_outputs.push_back(Dlop::create_integer(0));
  }

  return {.outputs = read_outputs};
}

}  // namespace hlop
