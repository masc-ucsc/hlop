CXX ?= g++
CXXFLAGS = -std=c++23 -O3 -I.. -I../..

SRCS = main.cpp sample_stage.cpp sample1_stage.cpp sample2_stage.cpp sample3_stage.cpp

sample: $(SRCS) $(wildcard *.hpp) ../sample_hash.hpp ../../uint.hpp ../../sint.hpp
	$(CXX) $(CXXFLAGS) -o $@ $(SRCS)

clean:
	@rm -f sample
