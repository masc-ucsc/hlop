
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

  h.add(s1.to2_aValid.is_known_true() ? 1 : 0);
  h.add(static_cast<uint64_t>(s1.to2_a.to_just_i64()));
  h.add(static_cast<uint64_t>(s1.to2_b.to_just_i64()));
  h.add(s1.to3_cValid.is_known_true() ? 1 : 0);
  h.add(static_cast<uint64_t>(s1.to3_c.to_just_i64()));
  h.add(static_cast<uint64_t>(s1.tmp.to_just_i64()));

  h.add(s2.to1_aValid.is_known_true() ? 1 : 0);
  h.add(static_cast<uint64_t>(s2.to1_a.to_just_i64()));
  h.add(s2.to2_eValid.is_known_true() ? 1 : 0);
  h.add(static_cast<uint64_t>(s2.to2_e.to_just_i64()));
  h.add(s2.to3_dValid.is_known_true() ? 1 : 0);
  h.add(static_cast<uint64_t>(s2.to3_d.to_just_i64()));
  h.add(static_cast<uint64_t>(s2.tmp.to_just_i64()));

  h.add(static_cast<uint64_t>(s3.to1_b.to_just_i64()));
  h.add(static_cast<uint64_t>(s3.tmp.to_just_i64()));
  h.add(static_cast<uint64_t>(s3.tmp2.to_just_i64()));
  for (const auto &m : s3.memory) {
    h.add(static_cast<uint64_t>(m.to_just_i64()));
  }

  return h.h;
}
