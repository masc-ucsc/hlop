#include "sample_stage.hpp"

#include "livesim_types.hpp"
#include "sample_hash.hpp"

#ifdef SIMLIB_VCD
Sample_stage::Sample_stage(uint64_t _hidx, const std::string& parent_name, vcd::VCDWriter* writer)
    : hidx(_hidx)
    , scope_name(parent_name.empty() ? "sample" : parent_name + ".sample")
    , vcd_writer(writer)
    , s1(33, scope_name, writer)
    , s2(2123, scope_name, writer)
    , s3(122, scope_name, writer) {}

void Sample_stage::vcd_reset_cycle() {
  vcd_writer->change(parent_vcd_reset, "1");
  s1.vcd_reset_cycle();
  s2.vcd_reset_cycle();
  s3.vcd_reset_cycle();
}

void Sample_stage::vcd_negedge() {
  vcd_writer->change(parent_vcd_clk, "0");
  s1.vcd_negedge();
  s2.vcd_negedge();
  s3.vcd_negedge();
}

void Sample_stage::vcd_posedge() {
  vcd_writer->change(parent_vcd_clk, "1");
  vcd_writer->change(parent_vcd_reset, "0");
  s1.vcd_posedge();
  s2.vcd_posedge();
  s3.vcd_posedge();
}

void Sample_stage::vcd_comb() {
  auto s1_to2_aValid = s1.to2_aValid;
  auto s1_to2_a      = s1.to2_a;
  auto s1_to2_b      = s1.to2_b;
  auto s1_to3_cValid = s1.to3_cValid;
  auto s1_to3_c      = s1.to3_c;
  s1.vcd_comb(s3.to1_b, s2.to1_aValid, s2.to1_a);

  auto s2_to3_dValid = s2.to3_dValid;
  auto s2_to3_d      = s2.to3_d;
  s2.vcd_comb(s1_to2_aValid, s1_to2_a, s1_to2_b);

  s3.vcd_comb(s1_to3_cValid, s1_to3_c, s2_to3_dValid, s2_to3_d);
}
#else
Sample_stage::Sample_stage(uint64_t _hidx) : hidx(_hidx), s1(33), s2(2123), s3(122) {}

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
#endif

uint64_t Sample_stage::state_hash() const {
  Sample_hash h;

  h.add(s1.to2_aValid.as_single_word());
  h.add(s1.to2_a.as_single_word());
  h.add(s1.to2_b.as_single_word());
  h.add(s1.to3_cValid.as_single_word());
  h.add(s1.to3_c.as_single_word());
  h.add(s1.tmp.as_single_word());

  h.add(s2.to1_aValid.as_single_word());
  h.add(s2.to1_a.as_single_word());
  h.add(s2.to2_eValid.as_single_word());
  h.add(s2.to2_e.as_single_word());
  h.add(s2.to3_dValid.as_single_word());
  h.add(s2.to3_d.as_single_word());
  h.add(s2.tmp.as_single_word());

  h.add(s3.to1_b.as_single_word());
  h.add(s3.tmp.as_single_word());
  h.add(s3.tmp2.as_single_word());
  for (const auto& m : s3.memory) {
    h.add(m.as_single_word());
  }

  return h.h;
}
