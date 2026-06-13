#include "sample2_stage.hpp"

#include "livesim_types.hpp"

#ifdef SIMLIB_VCD
Sample2_stage::Sample2_stage(uint64_t _hidx, const std::string& parent_name, vcd::VCDWriter* writer)
    : hidx(_hidx), scope_name(parent_name + ".s2"), vcd_writer(writer) {}

void Sample2_stage::vcd_posedge() {}

void Sample2_stage::vcd_negedge() {}

void Sample2_stage::vcd_reset_cycle() {
  tmp = 1;
  vcd_writer->change(vcd_tmp, tmp.to_string_binary());
  to1_aValid = false;
  vcd_writer->change(vcd_to1_aValid, to1_aValid.to_string_binary());
  to1_a = 0;
  vcd_writer->change(vcd_to1_a, to1_a.to_string_binary());
  to2_eValid = false;
  vcd_writer->change(vcd_to2_eValid, to2_eValid.to_string_binary());
  to2_e = 0;
  vcd_writer->change(vcd_to2_e, to2_e.to_string_binary());
  to3_dValid = false;
  vcd_writer->change(vcd_to3_dValid, to3_dValid.to_string_binary());
  to3_d = 0;
  vcd_writer->change(vcd_to3_d, to3_d.to_string_binary());
}
#else
Sample2_stage::Sample2_stage(uint64_t _hidx) : hidx(_hidx) {}

void Sample2_stage::reset_cycle() {
  tmp        = 1;
  to1_aValid = false;
  to1_a      = 0;
  to2_eValid = false;
  to2_e      = 0;
  to3_dValid = false;
  to3_d      = 0;
}
#endif
