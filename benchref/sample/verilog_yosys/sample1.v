// Stage 1 of benchref/sample — canonical spec in ../README.md.

module sample1
( input                  clk,
  input                  reset,

  input  logic           to1_aValid,
  input  logic [32-1:0]  to1_a, // from stage 2

  input  logic [32-1:0]  to1_b, // from stage 3

  output logic           to2_aValid /*verilator public_flat_rd*/,
  output logic [32-1:0]  to2_a      /*verilator public_flat_rd*/,
  output logic [32-1:0]  to2_b      /*verilator public_flat_rd*/,

  output logic           to3_cValid /*verilator public_flat_rd*/,
  output logic [32-1:0]  to3_c      /*verilator public_flat_rd*/
);

  logic [32-1:0] tmp /*verilator public_flat_rd*/;

  always @(posedge clk) begin
    if (reset) begin
      to2_b <= '0;
    end else begin
      to2_b <= (to1_b + 1) & 32'h7FFFFFFF;
    end
  end

  always @(posedge clk) begin
    if (reset) begin
      to2_a      <= '0;
      to2_aValid <= 0;
    end else begin
      to2_a      <= (to1_a + to1_b + 2) & 32'h7FFFFFFF;
      to2_aValid <= to1_aValid;
    end
  end

  always @(posedge clk) begin
    if (reset) begin
      tmp <= 0;
    end else begin
      tmp <= (tmp + 23) & 32'h7FFFFFFF;
    end
  end

  always @(posedge clk) begin
    if (reset) begin
      to3_cValid <= 0;
      to3_c      <= 0;
    end else begin
      to3_cValid <= tmp[0];
      to3_c      <= (tmp + to1_a) & 32'h7FFFFFFF;
    end
  end

endmodule
