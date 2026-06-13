# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

CPP23_COPTS = ["-std=c++23"]

cc_library(
    name = "benchref",
    srcs = ["lconst.cpp"],
    hdrs = [
        "graph_sizing.hpp",
        "lconst.hpp",
        "lrand.hpp",
        "sint.hpp",
        "uint.hpp",
        "woothash.hpp",
    ],
    includes = ["."],
    copts = CPP23_COPTS,
    visibility = ["//visibility:public"],
    deps = [
        "//hlop:hlop",
        "//hlop:support",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:span",
        "@boost.multiprecision",
        "@iassert",
    ],
)

cc_library(
    name = "vcd_writer",
    srcs = ["vcd_writer.cpp"],
    hdrs = [
        "vcd_utils.hpp",
        "vcd_writer.hpp",
    ],
    includes = ["."],
    copts = CPP23_COPTS,
    visibility = ["//visibility:public"],
)

cc_test(
    name = "lconst_test",
    srcs = ["tests/lconst_test.cpp"],
    copts = CPP23_COPTS,
    deps = [
        ":benchref",
        "//hlop:hlop",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "lconst_test2",
    srcs = ["tests/lconst_test2.cpp"],
    copts = CPP23_COPTS,
    deps = [
        ":benchref",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "lconst_bench",
    srcs = ["tests/lconst_bench.cpp"],
    copts = CPP23_COPTS,
    deps = [
        ":benchref",
        "//hlop:hlop",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@com_google_benchmark//:benchmark",
    ],
)

cc_test(
    name = "cross_test",
    srcs = ["tests/cross_test.cpp"],
    copts = CPP23_COPTS,
    deps = [
        ":benchref",
        "//hlop:hlop",
        "@com_google_googletest//:gtest_main",
    ],
)
