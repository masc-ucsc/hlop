//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// dlop_robust_check: brute-force robustness sweep over every Dlop *_op.
//
// Goal: confirm that NO operand value (any type/size/encoding combination) can
// make a public *_op segfault or trip an assertion. Each (op, operands) case
// runs in a forked child so a crash in one case does not stop the sweep — the
// parent records every crashing combination and prints a full report.
//
// Build/run under asan + assertions:
//   bazel run --config=asan //hlop:dlop_robust_check

#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "dlop.hpp"

using SP = spool_ptr<Dlop>;

namespace {

struct Operand {
  const char*         name;
  std::function<SP()> make;
};

std::vector<Operand> operands() {
  std::vector<Operand> v;
  auto add = [&](const char* n, std::function<SP()> f) { v.push_back({n, std::move(f)}); };

  add("int_0", [] { return Dlop::create_integer(0); });
  add("int_1", [] { return Dlop::create_integer(1); });
  add("int_-1", [] { return Dlop::create_integer(-1); });
  add("int_42", [] { return Dlop::create_integer(42); });
  add("int_64", [] { return Dlop::create_integer(64); });
  add("int_INT64_MIN", [] { return Dlop::create_integer(INT64_MIN); });
  add("int_INT64_MAX", [] { return Dlop::create_integer(INT64_MAX); });
  add("bool_true", [] { return Dlop::create_bool(true); });
  add("bool_false", [] { return Dlop::create_bool(false); });
  add("str_empty", [] { return Dlop::create_string(""); });
  add("str_hi", [] { return Dlop::create_string("hi"); });
  add("str_long", [] { return Dlop::create_string("hello world long string"); });
  add("nil", [] { return Dlop::nil(); });
  add("invalid", [] { return Dlop::invalid(); });
  add("ref", [] { return Dlop::from_ref("myref"); });
  add("unknown_1", [] { return Dlop::unknown(1); });
  add("unknown_4", [] { return Dlop::unknown(4); });
  add("unknown_64", [] { return Dlop::unknown(64); });
  add("unknown_130", [] { return Dlop::unknown(130); });
  add("unknown_bool", [] { return Dlop::unknown_bool(); });
  add("unknown_pos_8", [] { return Dlop::unknown_positive(8); });
  add("unknown_neg_8", [] { return Dlop::unknown_negative(8); });
  add("mixed_unk", [] { return Dlop::from_binary("1010?011", false); });
  add("unk_low", [] { return Dlop::from_pyrope("0sb1010_????"); });
  add("big_pos", [] { return Dlop::from_pyrope("0x123456789ABCDEF0123456789ABCDEF0"); });
  add("big_neg", [] { return Dlop::from_pyrope("-0x123456789ABCDEF0123456789ABCDEF0"); });
  return v;
}

// Exercise the result so a malformed (but non-crashing-to-build) value is
// flushed out by a downstream consumer too.
void consume(const SP& r) {
  if (!r) {
    return;
  }
  volatile auto s0 = r->to_pyrope();
  volatile auto s1 = r->to_binary();
  volatile auto s2 = r->to_verilog();
  volatile auto s3 = r->to_string();
  volatile auto s4 = r->to_decimal_string();
  volatile auto s5 = r->to_hex_string();
  (void)r->get_bits();
  (void)r->is_negative();
  (void)r->is_mask();
  (void)r->is_power2();
  (void)r->popcount();
  (void)r->hash();
  (void)r->has_unknowns();
  auto ser = r->serialize();
  auto rt  = Dlop::unserialize(ser);
  (void)rt;
}

struct BinOp {
  const char*                           name;
  std::function<SP(const SP&, const SP&)> run;
};

struct UnOp {
  const char*                  name;
  std::function<SP(const SP&)> run;
};

std::vector<BinOp> binops() {
  return {
      {"add_op", [](const SP& a, const SP& b) { return a->add_op(*b); }},
      {"sub_op", [](const SP& a, const SP& b) { return a->sub_op(*b); }},
      {"mult_op", [](const SP& a, const SP& b) { return a->mult_op(*b); }},
      {"div_op", [](const SP& a, const SP& b) { return a->div_op(*b); }},
      {"mod_op", [](const SP& a, const SP& b) { return a->mod_op(*b); }},
      {"or_op", [](const SP& a, const SP& b) { return a->or_op(*b); }},
      {"and_op", [](const SP& a, const SP& b) { return a->and_op(*b); }},
      {"xor_op", [](const SP& a, const SP& b) { return a->xor_op(*b); }},
      {"shl_op", [](const SP& a, const SP& b) { return a->shl_op(*b); }},
      {"sra_op", [](const SP& a, const SP& b) { return a->sra_op(*b); }},
      {"eq_op", [](const SP& a, const SP& b) { return a->eq_op(*b); }},
      {"lt_op", [](const SP& a, const SP& b) { return a->lt_op(*b); }},
      {"le_op", [](const SP& a, const SP& b) { return a->le_op(*b); }},
      {"gt_op", [](const SP& a, const SP& b) { return a->gt_op(*b); }},
      {"ge_op", [](const SP& a, const SP& b) { return a->ge_op(*b); }},
      {"ror_op2", [](const SP& a, const SP& b) { return a->ror_op(*b); }},
      {"sext_op", [](const SP& a, const SP& b) { return a->sext_op(*b); }},
      {"get_mask_op_m", [](const SP& a, const SP& b) { return a->get_mask_op(*b); }},
      {"set_mask_op", [](const SP& a, const SP& b) { return a->set_mask_op(*b, *b); }},
      {"concat_op", [](const SP& a, const SP& b) { return a->concat_op(*b); }},
      {"mux_op", [](const SP& a, const SP& b) { return Dlop::mux_op(*a, {b, b}); }},
      {"hotmux_op", [](const SP& a, const SP& b) { return Dlop::hotmux_op(*a, {b, b}); }},
      {"lut_op", [](const SP& a, const SP& b) { return Dlop::lut_op(*a, *b); }},
      {"sum_op", [](const SP& a, const SP& b) { return Dlop::sum_op({a}, {b}); }},
  };
}

std::vector<UnOp> unops() {
  return {
      {"neg_op", [](const SP& a) { return a->neg_op(); }},
      {"not_op", [](const SP& a) { return a->not_op(); }},
      {"ror_op", [](const SP& a) { return a->ror_op(); }},
      {"rand_op", [](const SP& a) { return a->rand_op(); }},
      {"rxor_op", [](const SP& a) { return a->rxor_op(); }},
      {"popcount_op", [](const SP& a) { return a->popcount_op(); }},
      {"get_mask_op", [](const SP& a) { return a->get_mask_op(); }},
      {"get_mask_value", [](const SP& a) { return a->get_mask_value(); }},
      {"to_known_rand", [](const SP& a) { return a->to_known_rand(); }},
  };
}

// Integer-amount ops, exercised with edge amounts.
const std::vector<int64_t> kAmounts = {0, 1, 63, 64, 65, 127, 200, -1, -64};

const char* status_str(int status, char* buf, size_t n) {
  if (WIFSIGNALED(status)) {
    snprintf(buf, n, "SIGNAL %d (%s)", WTERMSIG(status), strsignal(WTERMSIG(status)));
  } else if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
    snprintf(buf, n, "exit %d", WEXITSTATUS(status));
  } else {
    snprintf(buf, n, "ok");
  }
  return buf;
}

int g_fail = 0;
int g_run  = 0;

// Run `body` in a forked child. Returns true if it completed cleanly.
bool run_case(const std::string& label, const std::function<void()>& body) {
  ++g_run;
  fflush(stdout);
  pid_t pid = fork();
  if (pid == 0) {
    // Bound each case: a runaway allocation or hang (e.g. shift by a huge
    // amount) must self-terminate rather than thrash the machine. RLIMIT_AS is
    // unusable under ASan (its shadow reservation dwarfs any sane cap), so a
    // wall-clock alarm is the portable backstop; it is recorded as a failure.
    alarm(8);  // SIGALRM after 8s
    body();
    _exit(0);
  }
  int status = 0;
  waitpid(pid, &status, 0);
  bool ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;
  if (!ok) {
    char buf[128];
    printf("FAIL  %-60s -> %s\n", label.c_str(), status_str(status, buf, sizeof(buf)));
    ++g_fail;
  }
  return ok;
}

}  // namespace

int main() {
  auto ops_bin = binops();
  auto ops_un  = unops();
  auto vals    = operands();

  // Binary ops over every ordered pair.
  for (const auto& op : ops_bin) {
    for (const auto& a : vals) {
      for (const auto& b : vals) {
        std::string label = std::string(op.name) + "(" + a.name + ", " + b.name + ")";
        run_case(label, [&] {
          SP r = op.run(a.make(), b.make());
          consume(r);
        });
      }
    }
  }

  // Unary ops over every operand.
  for (const auto& op : ops_un) {
    for (const auto& a : vals) {
      std::string label = std::string(op.name) + "(" + a.name + ")";
      run_case(label, [&] {
        SP r = op.run(a.make());
        consume(r);
      });
    }
  }

  // Shift/sext driven through the public Dlop-operand forms with edge amounts
  // (the int64 overloads are protected — the operand form is the real API).
  for (const auto& a : vals) {
    for (int64_t amt : kAmounts) {
      auto amount = Dlop::create_integer(amt);
      run_case("shl_op_i(" + std::string(a.name) + ", " + std::to_string(amt) + ")", [&] {
        consume(a.make()->shl_op(*amount));
      });
      run_case("sra_op_i(" + std::string(a.name) + ", " + std::to_string(amt) + ")", [&] {
        consume(a.make()->sra_op(*amount));
      });
      run_case("sext_op_i(" + std::string(a.name) + ", " + std::to_string(amt) + ")", [&] {
        consume(a.make()->sext_op(*amount));
      });
      if (amt > 0) {
        run_case("adjust_bits(" + std::string(a.name) + ", " + std::to_string(amt) + ")", [&] {
          consume(a.make()->adjust_bits(static_cast<int>(amt)));
        });
      }
    }
  }

  printf("\n==== ran %d cases, %d failed ====\n", g_run, g_fail);
  return g_fail ? 1 : 0;
}
