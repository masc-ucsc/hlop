// Stage 3 of benchref/sample — canonical spec in ../README.md.

module sample3
( input                  clk,
  input                  reset,

  input  logic           to3_cValid,
  input  logic [32-1:0]  to3_c,

  input  logic           to3_dValid,
  input  logic [32-1:0]  to3_d,

  output logic [32-1:0]  to1_b /*verilator public_flat_rd*/
);

  logic [32-1:0] tmp  /*verilator public_flat_rd*/;
  logic [32-1:0] tmp2 /*verilator public_flat_rd*/;

  logic [32-1:0] memory[0:256-1] /*verilator public_flat_rd*/;

  always @(posedge clk) begin
    if (reset) begin
      tmp  <= 0;
      tmp2 <= 0;
    end else begin
      tmp <= (tmp + 7) & 32'h7FFFFFFF; // A prime number

      if ((tmp & 32'hFFFF) == 32'd45339) begin
        if ((tmp2 & 15) == 0) begin
          $display("memory[127] = %0d", memory[127]);
        end
        tmp2 <= (tmp2 + 1) & 32'h7FFFFFFF;
      end
    end
  end

  logic [8-1:0] reset_iterator;

  initial begin
    reset_iterator = 0;
  end

  always @(posedge clk) begin
    if (reset) begin
      memory[reset_iterator] <= '0;
      reset_iterator         <= reset_iterator + 1;
      to1_b                  <= '0;
    end else begin
      if (to3_cValid && to3_dValid) begin
        memory[(to3_c + tmp) & 32'hff] <= to3_d;
      end
      to1_b <= memory[tmp[7:0]];
    end
  end

endmodule
