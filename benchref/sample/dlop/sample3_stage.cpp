#include "sample3_stage.hpp"

#include <stdio.h>

void Sample3_stage::reset_cycle() {
  tmp.init_integer(0);
  tmp2.init_integer(0);
  to1_b.init_integer(0);

  memory[reset_iterator].init_integer(0);  // clear first, then advance (matches the Verilog NBA order)
  reset_iterator = reset_iterator + 1;
}

void Sample3_stage::cycle(const Dlop& s1_to3_cValid, const Dlop& s1_to3_c, const Dlop& s2_to3_dValid, const Dlop& s2_to3_d) {
  if (__builtin_expect(tmp.and_op(kffff)->is_known_eq(k45339), 0)) {
    if (tmp2.and_op(k15)->is_known_zero()) {
      printf("memory[127] = %u\n", (unsigned)memory[127].to_just_i64());
    }
    tmp2 = tmp2.add_op(k1)->and_op(mask31);
  }

  to1_b = memory[tmp.and_op(kff)->to_just_i64()];

  if (s1_to3_cValid.is_known_true() && s2_to3_dValid.is_known_true()) {
    memory[s1_to3_c.add_op(tmp)->and_op(kff)->to_just_i64()] = s2_to3_d;
  }

  tmp = tmp.add_op(k7)->and_op(mask31);  // A prime number
}
