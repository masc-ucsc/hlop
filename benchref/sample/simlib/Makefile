CXX ?= g++
CXXFLAGS = -std=c++23 -O3 -I.. -I../..

SRCS = sample_stage.cpp sample1_stage.cpp sample2_stage.cpp sample3_stage.cpp
HDRS = $(wildcard *.hpp) ../sample_hash.hpp ../../uint.hpp ../../sint.hpp

all: sample sample_vcd

sample: main.cpp $(SRCS) $(HDRS)
	$(CXX) $(CXXFLAGS) -o $@ main.cpp $(SRCS)

sample_vcd: main_vcd_writer.cpp $(SRCS) $(HDRS) ../../vcd_writer.cpp
	$(CXX) $(CXXFLAGS) -DSIMLIB_VCD -o $@ main_vcd_writer.cpp $(SRCS) ../../vcd_writer.cpp

clean:
	@rm -f sample sample_vcd dump.vcd
