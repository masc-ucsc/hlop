// Top wrapper for the benchref/sample design (see ../README.md for the
// canonical specification). The clock/reset driver lives in the C++ harness
// (dut_sample.cpp for verilator, main.cpp for CXXRTL).

module sample(input clk, input reset);

  logic           to2_aValid;
  logic [32-1:0]  to2_a;
  logic [32-1:0]  to2_b;

  logic           to3_cValid;
  logic [32-1:0]  to3_c;

  logic           to1_aValid;
  logic [32-1:0]  to1_a;

  logic           to2_eValid;
  logic [32-1:0]  to2_e;

  logic           to3_dValid;
  logic [32-1:0]  to3_d;

  logic [32-1:0]  to1_b;

  sample1 s1 (.*);
  sample2 s2 (.*);
  sample3 s3 (.*);

endmodule
