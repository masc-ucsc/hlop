# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# C++ implementations of the benchref/sample benchmark design (see README.md).
# The Verilog flows (verilator, icarus, yosys/CXXRTL, vcs) stay as per-dir
# Makefiles — they shell out to external tools.

load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library")

CPP23_COPTS = ["-std=c++23"]

cc_library(
    name = "sample_hash",
    hdrs = ["sample_hash.hpp"],
    includes = ["."],
    copts = CPP23_COPTS,
    visibility = ["//visibility:public"],
)

cc_binary(
    name = "sample_cpp_native",
    srcs = [
        "cpp_native/main.cpp",
        "cpp_native/sample1_stage.cpp",
        "cpp_native/sample1_stage.hpp",
        "cpp_native/sample2_stage.cpp",
        "cpp_native/sample2_stage.hpp",
        "cpp_native/sample3_stage.cpp",
        "cpp_native/sample3_stage.hpp",
        "cpp_native/sample_stage.cpp",
        "cpp_native/sample_stage.hpp",
    ],
    copts = CPP23_COPTS,
    deps = [":sample_hash"],
)

cc_binary(
    name = "sample_cpp_oldprp",
    srcs = [
        "cpp_oldprp/Sample_Stage1.cpp",
        "cpp_oldprp/Sample_Stage1.h",
        "cpp_oldprp/Sample_Stage2.cpp",
        "cpp_oldprp/Sample_Stage2.h",
        "cpp_oldprp/Sample_Stage3.cpp",
        "cpp_oldprp/Sample_Stage3.h",
        "cpp_oldprp/Stage.h",
        "cpp_oldprp/main.cpp",
    ],
    copts = CPP23_COPTS,
    deps = [":sample_hash"],
)

SIMLIB_SRCS = [
    "simlib/livesim_types.hpp",
    "simlib/sample1_stage.cpp",
    "simlib/sample1_stage.hpp",
    "simlib/sample2_stage.cpp",
    "simlib/sample2_stage.hpp",
    "simlib/sample3_stage.cpp",
    "simlib/sample3_stage.hpp",
    "simlib/sample_stage.cpp",
    "simlib/sample_stage.hpp",
]

cc_binary(
    name = "sample_simlib",
    srcs = SIMLIB_SRCS + ["simlib/main.cpp"],
    copts = CPP23_COPTS,
    deps = [
        ":sample_hash",
        "//benchref",
        "//benchref:vcd_writer",
    ],
)

cc_binary(
    name = "sample_simlib_vcd",
    srcs = SIMLIB_SRCS + ["simlib/main_vcd_writer.cpp"],
    copts = CPP23_COPTS,
    defines = ["SIMLIB_VCD"],
    deps = [
        ":sample_hash",
        "//benchref",
        "//benchref:vcd_writer",
    ],
)

cc_binary(
    name = "sample_simlib_raw",
    srcs = [
        "simlib_raw/livesim_types.hpp",
        "simlib_raw/main.cpp",
        "simlib_raw/sample1_stage.cpp",
        "simlib_raw/sample1_stage.hpp",
        "simlib_raw/sample2_stage.cpp",
        "simlib_raw/sample2_stage.hpp",
        "simlib_raw/sample3_stage.cpp",
        "simlib_raw/sample3_stage.hpp",
        "simlib_raw/sample_stage.cpp",
        "simlib_raw/sample_stage.hpp",
    ],
    copts = CPP23_COPTS,
    deps = [
        ":sample_hash",
        "//benchref",
    ],
)

cc_binary(
    name = "sample_slop",
    srcs = [
        "slop/main.cpp",
        "slop/sample1_stage.cpp",
        "slop/sample1_stage.hpp",
        "slop/sample2_stage.cpp",
        "slop/sample2_stage.hpp",
        "slop/sample3_stage.cpp",
        "slop/sample3_stage.hpp",
        "slop/sample_stage.cpp",
        "slop/sample_stage.hpp",
    ],
    copts = CPP23_COPTS,
    deps = [
        ":sample_hash",
        "//hlop",
    ],
)

cc_binary(
    name = "sample_dlop",
    srcs = [
        "dlop/main.cpp",
        "dlop/sample1_stage.cpp",
        "dlop/sample1_stage.hpp",
        "dlop/sample2_stage.cpp",
        "dlop/sample2_stage.hpp",
        "dlop/sample3_stage.cpp",
        "dlop/sample3_stage.hpp",
        "dlop/sample_stage.cpp",
        "dlop/sample_stage.hpp",
    ],
    copts = CPP23_COPTS,
    deps = [
        ":sample_hash",
        "//hlop",
    ],
)
