#include "sample3_stage.hpp"

#include <stdio.h>

#include <bitset>

#include "livesim_types.hpp"

#ifdef SIMLIB_VCD
Sample3_stage::Sample3_stage(uint64_t _hidx, const std::string& parent_name, vcd::VCDWriter* writer)
    : hidx(_hidx), scope_name(parent_name + ".s3"), vcd_writer(writer) {}

void Sample3_stage::vcd_reset_cycle() {
  tmp = 0;
  vcd_writer->change(vcd_tmp, tmp.to_string_binary());
  tmp2 = 0;
  vcd_writer->change(vcd_tmp2, tmp2.to_string_binary());
  to1_b = 0;
  vcd_writer->change(vcd_to1_b, to1_b.to_string_binary());

  memory[reset_iterator] = 0;  // clear first, then advance (matches the Verilog NBA order)
  reset_iterator         = reset_iterator + 1;
  vcd_writer->change(vcd_reset_iterator, 'b' + std::bitset<8>(reset_iterator).to_string());
}

void Sample3_stage::vcd_posedge() {}

void Sample3_stage::vcd_negedge() {}

void Sample3_stage::vcd_comb(UInt<1> s1_to3_cValid, UInt<32> s1_to3_c, UInt<1> s2_to3_dValid, UInt<32> s2_to3_d) {
  if (__builtin_expect(((tmp & UInt<32>(0xFFFF)) == UInt<32>(45339)), 0)) {
    if ((tmp2 & UInt<32>(15)) == UInt<32>(0)) {
      printf("memory[127] = %u\n", static_cast<unsigned>(memory[127].as_single_word()));
    }
    tmp2 = tmp2.addw(UInt<32>(1)) & UInt<32>(0x7FFFFFFF);
    vcd_writer->change(vcd_tmp2, tmp2.to_string_binary());
  }

  to1_b = memory[(tmp & UInt<32>(0xff)).as_single_word()];
  vcd_writer->change(vcd_to1_b, to1_b.to_string_binary());

  if (s1_to3_cValid && s2_to3_dValid) {
    UInt<32> tmp3                                    = s1_to3_c.addw(tmp);
    memory[(tmp3 & UInt<32>(0xff)).as_single_word()] = s2_to3_d;
  }

  tmp = tmp.addw(UInt<32>(7)) & UInt<32>(0x7FFFFFFF);
  vcd_writer->change(vcd_tmp, tmp.to_string_binary());
}
#else
Sample3_stage::Sample3_stage(uint64_t _hidx) : hidx(_hidx) {}

void Sample3_stage::reset_cycle() {
  tmp   = 0;
  tmp2  = 0;
  to1_b = 0;

  memory[reset_iterator] = 0;  // clear first, then advance (matches the Verilog NBA order)
  reset_iterator         = reset_iterator + 1;
}

void Sample3_stage::cycle(UInt<1> s1_to3_cValid, UInt<32> s1_to3_c, UInt<1> s2_to3_dValid, UInt<32> s2_to3_d) {
  if (__builtin_expect(((tmp & UInt<32>(0xFFFF)) == UInt<32>(45339)), 0)) {
    if ((tmp2 & UInt<32>(15)) == UInt<32>(0)) {
      printf("memory[127] = %u\n", static_cast<unsigned>(memory[127].as_single_word()));
    }
    tmp2 = tmp2.addw(UInt<32>(1)) & UInt<32>(0x7FFFFFFF);
  }

  to1_b = memory[(tmp & UInt<32>(0xff)).as_single_word()];

  if (s1_to3_cValid && s2_to3_dValid) {
    UInt<32> tmp3                                    = s1_to3_c.addw(tmp);
    memory[(tmp3 & UInt<32>(0xff)).as_single_word()] = s2_to3_d;
  }

  tmp = tmp.addw(UInt<32>(7)) & UInt<32>(0x7FFFFFFF);
}
#endif
