// VCD waveform variant (built with -DSIMLIB_VCD): runs the same design but
// dumps every register change to dump.vcd, 10 time units per cycle
// (posedge +5, comb +2, negedge +3). Short run by default; --hash is ignored.
#include <stdio.h>

#include "sample_hash.hpp"
#include "sample_stage.hpp"
#include "vcd_writer.hpp"

int main(int argc, char **argv) {
  Sample_args args;
  args.cycles = 10000;  // waveform dumps are for debug/compare, not performance
  if (!args.parse(argc, argv)) {
    return 1;
  }

  vcd::VCDWriter writer("dump.vcd");
  Sample_stage   top(0, "", &writer);

  for (uint64_t i = 0; i < args.reset; ++i) {
    vcd::advance_to_posedge();
    writer.change(top.parent_vcd_clk, "1");
    top.vcd_reset_cycle();
    vcd::advance_to_comb();
    vcd::advance_to_negedge();
    top.vcd_negedge();
  }

  for (uint64_t i = 0; i < args.cycles; ++i) {
    vcd::advance_to_posedge();
    top.vcd_posedge();
    vcd::advance_to_comb();
    top.vcd_comb();
    vcd::advance_to_negedge();
    top.vcd_negedge();
  }

  // no writer.close(): ~VCDWriter flushes, and close() before it would make
  // the destructor's flush() throw

  return 0;
}
