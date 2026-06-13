#include "Sample_Stage3.h"

#include <stdio.h>

// Include the other stages that generate outputs used by this block
#include "Sample_Stage1.h"
#include "Sample_Stage2.h"

Sample_Stage3::Sample_Stage3(Output_Sample_Stage3 *o, Output_Sample_Stage1 *_s1out, Output_Sample_Stage2 *_s2out) {
  output = o;
  s1out  = _s1out;
  s2out  = _s2out;

  reset_iterator = 0;

  memory.resize(256);
}

void Sample_Stage3::reset_cycle() {
  tmp       = 0;
  tmp2      = 0;
  pcv.to1_b = 0;

  memory[reset_iterator] = 0;  // clear first, then advance (matches the Verilog NBA order)
  reset_iterator         = reset_iterator + 1;
}

void Sample_Stage3::cycle() {
  if (__builtin_expect(((tmp & 0xFFFF) == 45339), 0)) {
    if ((tmp2 & 15) == 0) {
      printf("memory[127] = %u\n", memory[127]);
    }
    tmp2 = (tmp2 + 1) & 0x7FFFFFFF;
  }

  pcv.to1_b = memory[tmp & 0xff];  // old memory contents, even if written below

  if (s1out->to3_cValid && s2out->to3_dValid) {
    memory[(s1out->to3_c + tmp) & 0xff] = s2out->to3_d;
  }

  tmp = (tmp + 7) & 0x7FFFFFFF;  // A prime number
}

void Sample_Stage3::update() { *output = pcv; }

void Sample_Stage3::hash_state(Sample_hash &h) const {
  h.add(output->to1_b);
  h.add(tmp);
  h.add(tmp2);
  for (const auto &m : memory) {
    h.add(m);
  }
}
