# yosys/CXXRTL flow for benchref/sample (spec in ../README.md).
# yosys translates the four .v files into sample.cc (a CXXRTL model) and
# main.cpp #includes it. -O6 is safe here: every architectural register is a
# flip-flop output, so it stays a member of the generated p_sample struct and
# the state hash can read it directly (see state_hash in main.cpp).
# -I.. lets main.cpp find sample_hash.hpp.

CXX ?= g++
CXXRTL_RUNTIME = $(shell yosys-config --datdir)/include/backends/cxxrtl/runtime
CXXFLAGS = -std=c++17 -O3 -I.. -I$(CXXRTL_RUNTIME)

VSRCS = sample.v sample1.v sample2.v sample3.v

sample: sample.cc main.cpp ../sample_hash.hpp
	$(CXX) $(CXXFLAGS) -o $@ main.cpp

sample.cc: $(VSRCS)
	yosys -q -p "read_verilog -sv $(VSRCS) ; write_cxxrtl -O6 sample.cc"

clean:
	@rm -f sample.cc sample
