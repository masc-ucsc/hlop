# VCS harness for benchref/sample (see ../README.md). VCS is commercial and
# usually absent; run_bench.sh skips this flow when `vcs` is not in PATH.
# The testbench is the same self-contained sample.v as verilog_icarus/.
# Plusargs: +cycles=T +reset=R +hash=K +vcd

simv: sample.v
	vcs -full64 -sverilog -q sample.v

run: simv
	./simv

check: simv
	./simv +hash=10000 | grep -E '^(hash|memory)' | diff - ../golden_hash_100k.txt
	@echo PASS

clean:
	@rm -rf simv* csrc ucli.key dump.vcd
