# Verilator flow for benchref/sample (spec in ../README.md).
# vsample: plain/hash binary; vsample_vcd: --trace build that dumps output.vcd.
# -I../.. lets dut_sample.cpp (compiled inside the obj_dir) find sample_hash.hpp.

VSRCS = sample.v sample1.v sample2.v sample3.v

all: vsample

vsample: $(VSRCS) dut_sample.cpp ../sample_hash.hpp
	verilator -O3 -CFLAGS "-O3 -I../.." $(VSRCS) --cc --exe dut_sample.cpp
	make -j -C obj_dir -f Vsample.mk Vsample
	cp -f obj_dir/Vsample vsample

vsample_vcd: $(VSRCS) dut_sample.cpp ../sample_hash.hpp
	verilator -O3 -CFLAGS "-O3 -DTRACE -I../.." --Mdir obj_dir_vcd $(VSRCS) --cc --trace --exe dut_sample.cpp
	make -j -C obj_dir_vcd -f Vsample.mk Vsample
	cp -f obj_dir_vcd/Vsample vsample_vcd

clean:
	rm -f vsample vsample_vcd
	rm -rf obj_dir obj_dir_vcd
	rm -f output.vcd
