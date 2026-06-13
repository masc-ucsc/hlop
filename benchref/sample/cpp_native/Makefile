CXX ?= g++
CXXFLAGS = -std=c++17 -O3 -I..

SRCS = main.cpp sample_stage.cpp sample1_stage.cpp sample2_stage.cpp sample3_stage.cpp

sample: $(SRCS) $(wildcard *.hpp) ../sample_hash.hpp
	$(CXX) $(CXXFLAGS) -o $@ $(SRCS)

clean:
	@rm -f sample
