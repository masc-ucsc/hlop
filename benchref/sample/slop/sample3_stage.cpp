
#include "sample3_stage.hpp"

#include <stdio.h>

void Sample3_stage::reset_cycle() {
  tmp   = 0;
  tmp2  = 0;
  to1_b = 0;

  memory[reset_iterator] = 0;  // clear first, then advance (matches the Verilog NBA order)
  reset_iterator         = reset_iterator + 1;
}

void Sample3_stage::cycle(Slop<1> s1_to3_cValid, Slop<32> s1_to3_c, Slop<1> s2_to3_dValid, Slop<32> s2_to3_d) {
  constexpr Slop<32> mask31(0x7FFFFFFF);

  if (__builtin_expect(tmp.and_op(0xFFFF).is_known_eq(45339), 0)) {
    if (tmp2.and_op(15).is_known_false()) {
      printf("memory[127] = %u\n", (unsigned)memory[127].to_just_i64());
    }
    tmp2 = tmp2.add_op(1).and_op(mask31);
  }

  to1_b = memory[tmp.and_op(0xff).to_just_i64()];

  if (s1_to3_cValid.is_known_true() && s2_to3_dValid.is_known_true()) {
    memory[s1_to3_c.add_op(tmp).and_op(0xff).to_just_i64()] = s2_to3_d;
  }

  tmp = tmp.add_op(7).and_op(mask31);  // A prime number
}
