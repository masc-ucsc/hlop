
#include "sample_stage.hpp"

#include "sample_hash.hpp"

void Sample_stage::reset_cycle() {
  s1.reset_cycle();
  s2.reset_cycle();
  s3.reset_cycle();
}

void Sample_stage::cycle() {
  auto s1_to2_aValid = s1.to2_aValid;
  auto s1_to2_a      = s1.to2_a;
  auto s1_to2_b      = s1.to2_b;
  auto s1_to3_cValid = s1.to3_cValid;
  auto s1_to3_c      = s1.to3_c;
  s1.cycle(s3.to1_b, s2.to1_aValid, s2.to1_a);

  auto s2_to3_dValid = s2.to3_dValid;
  auto s2_to3_d      = s2.to3_d;
  s2.cycle(s1_to2_aValid, s1_to2_a, s1_to2_b);

  s3.cycle(s1_to3_cValid, s1_to3_c, s2_to3_dValid, s2_to3_d);
}

uint64_t Sample_stage::state_hash() const {
  Sample_hash h;

  h.add(s1.to2_aValid);
  h.add(s1.to2_a);
  h.add(s1.to2_b);
  h.add(s1.to3_cValid);
  h.add(s1.to3_c);
  h.add(s1.tmp);

  h.add(s2.to1_aValid);
  h.add(s2.to1_a);
  h.add(s2.to2_eValid);
  h.add(s2.to2_e);
  h.add(s2.to3_dValid);
  h.add(s2.to3_d);
  h.add(s2.tmp);

  h.add(s3.to1_b);
  h.add(s3.tmp);
  h.add(s3.tmp2);
  for (const auto &m : s3.memory) {
    h.add(m);
  }

  return h.h;
}
