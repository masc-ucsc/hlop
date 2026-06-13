# iverilog/vvp harness for benchref/sample (see ../README.md).
# Plusargs: +cycles=T +reset=R +hash=K +vcd

IVERILOG ?= iverilog
VVP      ?= vvp

sample.vvp: sample.v
	$(IVERILOG) -g2012 -o $@ $<

run: sample.vvp
	$(VVP) sample.vvp

check: sample.vvp
	$(VVP) sample.vvp +hash=10000 | grep -E '^(hash|memory)' | diff - ../golden_hash_100k.txt
	@echo PASS

clean:
	@rm -f sample.vvp dump.vcd
