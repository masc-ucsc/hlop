#include "sample1_stage.hpp"

#include "livesim_types.hpp"

Sample1_stage::Sample1_stage(uint64_t _hidx) : hidx(_hidx) {}

void Sample1_stage::reset_cycle() {
  tmp        = UInt<32>(0);
  to2_aValid = UInt<1>(0);
  to2_a      = UInt<32>(0);
  to2_b      = UInt<32>(0);
  to3_cValid = UInt<1>(0);
  to3_c      = UInt<32>(0);
}

void Sample1_stage::cycle(UInt<32> s3_to1_b, UInt<1> s2_to1_aValid, UInt<32> s2_to1_a) {
  to2_b = s3_to1_b.addw(UInt<32>(1)) & UInt<32>(0x7FFFFFFF);

  auto tmp3  = s2_to1_a.addw(s3_to1_b);
  to2_a      = tmp3.addw(UInt<32>(2)) & UInt<32>(0x7FFFFFFF);
  to2_aValid = s2_to1_aValid;

  to3_cValid = tmp.bit<0>();
  to3_c      = tmp.addw(s2_to1_a) & UInt<32>(0x7FFFFFFF);

  tmp = tmp.addw(UInt<32>(23)) & UInt<32>(0x7FFFFFFF);
}
