//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// dcontext.hpp: Dynamic execution adapter for hlop
//
// Provides the traversal-friendly dynamic API used by LNAST and LGraph
// traversals. Wraps Dlop operations with a DCall/DResult envelope.
//
// The DContext adapter translates dynamic inputs into Dlop method calls,
// sharing the same semantics as the slop kernel path in eval.hpp.

#pragma once

#include <cassert>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "dlop.hpp"
#include "spool_ptr.hpp"

namespace hlop {

using DValue = spool_ptr<Dlop>;

// Op types matching LGraph Ntype_op
enum class Ntype_op {
  Invalid,
  Sum,
  Mult,
  Div,
  And,
  Or,
  Xor,
  Ror,
  Not,
  Get_mask,
  Set_mask,
  Sext,
  LT,
  EQ,
  SHL,
  SRA,
  Mux,
  LUT,
  Flop,
  Latch,
  Fflop,
  Memory,
  Const,
  IO,
  Sub,
  TupAdd,
  TupGet,
  AttrSet,
  AttrGet,
};

struct DInput {
  int         pid = -1;
  std::string pin;
  DValue      value;
};

struct DCall {
  Ntype_op            op = Ntype_op::Invalid;
  std::string         state_id;
  std::vector<DInput> inputs;
};

struct DResult {
  std::vector<DValue> outputs;
};

class DContext {
public:
  DResult execute(const DCall& call);
  void    advance_clock();

private:
  // Flop/latch state: keyed by state_id
  struct FlopState {
    DValue curr;
    DValue next;
  };
  std::unordered_map<std::string, FlopState> flop_state_;
  std::unordered_map<std::string, FlopState> latch_state_;

  // Memory state: keyed by state_id
  struct MemoryState {
    std::vector<DValue> curr;
    std::vector<DValue> next;
    bool                fwd         = false;
    int64_t             size        = 0;
    int64_t             bits        = 0;
    bool                initialized = false;
  };
  std::unordered_map<std::string, MemoryState> memory_state_;

  // Helpers for collecting inputs
  static std::vector<DValue> collect_values(const std::vector<DInput>& inputs);
  static DValue              find_pin(const std::vector<DInput>& inputs, const std::string& name);
  static DValue              find_pid(const std::vector<DInput>& inputs, int pid);
  static std::vector<DValue> collect_pin(const std::vector<DInput>& inputs, const std::string& name);

  // Per-op dispatch
  DResult exec_sum(const DCall& call);
  DResult exec_mult(const DCall& call);
  DResult exec_div(const DCall& call);
  DResult exec_and(const DCall& call);
  DResult exec_or(const DCall& call);
  DResult exec_xor(const DCall& call);
  DResult exec_ror(const DCall& call);
  DResult exec_not(const DCall& call);
  DResult exec_get_mask(const DCall& call);
  DResult exec_set_mask(const DCall& call);
  DResult exec_sext(const DCall& call);
  DResult exec_lt(const DCall& call);
  DResult exec_eq(const DCall& call);
  DResult exec_shl(const DCall& call);
  DResult exec_sra(const DCall& call);
  DResult exec_mux(const DCall& call);
  DResult exec_lut(const DCall& call);
  DResult exec_flop(const DCall& call);
  DResult exec_latch(const DCall& call);
  DResult exec_fflop(const DCall& call);
  DResult exec_memory(const DCall& call);
};

}  // namespace hlop
