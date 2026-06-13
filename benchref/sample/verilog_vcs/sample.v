// Self-contained testbench for the benchref/sample design (see ../README.md
// for the canonical specification). Derived from livehd/simlib's
// verilog_vcs/sample.v, with the deterministic-reset and 31-bit-mask fixes
// applied so the register state matches every other implementation
// cycle-for-cycle.
//
// icarus: iverilog -g2012 -o sample.vvp sample.v && vvp sample.vvp +hash=10000
// vcs:    vcs -full64 -sverilog -q sample.v && ./simv +hash=10000
//
// plusargs: +cycles=T +reset=R +hash=K +vcd   (defaults: T=100000 R=1000 K=0)

module sample_stage1
( input                  clk,
  input                  reset,

  input  logic           to1_aValid,
  input  logic [32-1:0]  to1_a, // from stage 2

  input  logic [32-1:0]  to1_b, // from stage 3

  output logic           to2_aValid,
  output logic [32-1:0]  to2_a,
  output logic [32-1:0]  to2_b,

  output logic           to3_cValid,
  output logic [32-1:0]  to3_c
);

  logic [32-1:0] tmp;

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

module sample_stage2
( input                  clk,
  input                  reset,

  input  logic           to2_aValid,
  input  logic [32-1:0]  to2_a,
  input  logic [32-1:0]  to2_b,

  output logic           to1_aValid,
  output logic [32-1:0]  to1_a,

  output logic           to2_eValid,
  output logic [32-1:0]  to2_e,

  output logic           to3_dValid,
  output logic [32-1:0]  to3_d
);

  logic [32-1:0] tmp;

  always @(posedge clk) begin
    if (reset) begin
      tmp <= 1;
    end else begin
      tmp <= (tmp + 13) & 32'h7FFFFFFF; // A prime number
    end
  end

  always @(posedge clk) begin
    if (reset) begin
      to3_dValid <= 0;
      to3_d      <= '0;
    end else begin
      to3_dValid <= (tmp[0]) == 1'd0;
      to3_d      <= (tmp + to2_b) & 32'h7FFFFFFF;
    end
  end

  always @(posedge clk) begin
    if (reset) begin
      to2_eValid <= 0;
      to2_e      <= '0;
    end else begin
      to2_eValid <= (tmp[0]) == 1'b1 && to2_aValid && to1_aValid;
      to2_e      <= (tmp + to2_a + to1_a) & 32'h7FFFFFFF;
    end
  end

  always @(posedge clk) begin
    if (reset) begin
      to1_aValid <= 0;
      to1_a      <= '0;
    end else begin
      to1_aValid <= (tmp[1]) == 1'd1;
      to1_a      <= (tmp + 3) & 32'h7FFFFFFF;
    end
  end

endmodule

module sample_stage3
( input                  clk,
  input                  reset,

  input  logic           to3_cValid,
  input  logic [32-1:0]  to3_c,

  input  logic           to3_dValid,
  input  logic [32-1:0]  to3_d,

  output logic [32-1:0]  to1_b
);

  logic [32-1:0] tmp;
  logic [32-1:0] tmp2;

  logic [32-1:0] memory[0:256-1];

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

module top_sample();

  logic clk;
  logic reset;

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

  sample_stage1 s1 (.*);
  sample_stage2 s2 (.*);
  sample_stage3 s3 (.*);

  always begin
    #5 clk = !clk;
  end

  // FNV-1a 64-bit over the architectural registers, canonical order — must
  // stay bit-identical to sample_hash.hpp (see ../README.md).
  function automatic longint unsigned state_hash();
    longint unsigned h;
    integer          i;
    h = 64'hcbf29ce484222325;

    h = (h ^ {63'b0, to2_aValid}) * 64'h100000001b3;
    h = (h ^ {32'b0, to2_a})      * 64'h100000001b3;
    h = (h ^ {32'b0, to2_b})      * 64'h100000001b3;
    h = (h ^ {63'b0, to3_cValid}) * 64'h100000001b3;
    h = (h ^ {32'b0, to3_c})      * 64'h100000001b3;
    h = (h ^ {32'b0, s1.tmp})     * 64'h100000001b3;

    h = (h ^ {63'b0, to1_aValid}) * 64'h100000001b3;
    h = (h ^ {32'b0, to1_a})      * 64'h100000001b3;
    h = (h ^ {63'b0, to2_eValid}) * 64'h100000001b3;
    h = (h ^ {32'b0, to2_e})      * 64'h100000001b3;
    h = (h ^ {63'b0, to3_dValid}) * 64'h100000001b3;
    h = (h ^ {32'b0, to3_d})      * 64'h100000001b3;
    h = (h ^ {32'b0, s2.tmp})     * 64'h100000001b3;

    h = (h ^ {32'b0, to1_b})      * 64'h100000001b3;
    h = (h ^ {32'b0, s3.tmp})     * 64'h100000001b3;
    h = (h ^ {32'b0, s3.tmp2})    * 64'h100000001b3;
    for (i = 0; i < 256; i = i + 1) begin
      h = (h ^ {32'b0, s3.memory[i]}) * 64'h100000001b3;
    end

    return h;
  endfunction

  longint unsigned n_cycles;
  longint unsigned n_reset;
  longint unsigned hash_k;
  longint unsigned n;

  initial begin
    if (!$value$plusargs("cycles=%d", n_cycles)) n_cycles = 100000;
    if (!$value$plusargs("reset=%d", n_reset))   n_reset  = 1000;
    if (!$value$plusargs("hash=%d", hash_k))     hash_k   = 0;

    if (n_reset < 256) begin
      $display("ERROR: +reset=%0d but the design needs >= 256 reset cycles", n_reset);
      $finish;
    end

    if ($test$plusargs("vcd")) begin
      $dumpfile("dump.vcd");
      $dumpvars(0, top_sample);
    end

    clk   = 0;
    reset = 1;

    repeat (n_reset) @(posedge clk);
    @(negedge clk);
    reset = 0;

    for (n = 1; n <= n_cycles; n = n + 1) begin
      @(posedge clk);
      if (hash_k != 0 && ((n % hash_k) == 0 || n == n_cycles)) begin
        #1; // let the non-blocking updates of this cycle settle
        $display("hash %0d %h", n, state_hash());
      end
    end

    $finish;
  end

endmodule
