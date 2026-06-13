#pragma once

#include <cstdint>

#include "sample1_stage.hpp"
#include "sample2_stage.hpp"
#include "sample3_stage.hpp"

struct Sample_stage {
  uint64_t hidx;

  Sample_stage(uint64_t _hidx);
  void reset_cycle();
  void cycle();

  Sample1_stage s1;
  Sample2_stage s2;
  Sample3_stage s3;

  uint64_t state_hash() const;
};
