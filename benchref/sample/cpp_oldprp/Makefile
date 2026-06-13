CXX ?= g++
CXXFLAGS = -std=c++17 -O3 -I..

SRCS = main.cpp Sample_Stage1.cpp Sample_Stage2.cpp Sample_Stage3.cpp

sample: $(SRCS) $(wildcard *.h) ../sample_hash.hpp
	$(CXX) $(CXXFLAGS) -o $@ $(SRCS)

clean:
	@rm -f sample
