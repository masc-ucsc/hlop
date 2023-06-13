# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

cc_library(
    name = "hlop",
    srcs = glob(
        ["*.cpp"],
        exclude = [
            "*test*.cpp",
        ],
    ),
    hdrs = glob(["*.hpp"]),
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [
        "@fmt",
        "@iassert",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/types:span",
        # The following are needed ONLY for lconst (to remove in the future)
        "@boost//:multiprecision",
    ]
)

cc_test(
    name = "lconst_test",
    srcs = ["tests/lconst_test.cpp"],
    deps = [
        ":hlop",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "lconst_bench",
    srcs = ["tests/lconst_bench.cpp"],
    deps = [
        ":hlop",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_benchmark//:benchmark",
    ],
)

cc_test(
    name = "blop_test",
    srcs = ["tests/blop_test.cpp"],
    deps = [
        ":hlop",
        "@com_google_googletest//:gtest_main",
    ],
)
