# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

CPP23_COPTS = ["-std=c++23"]

cc_library(
    name = "support",
    hdrs = [
        "likely.hpp",
        "str_tools.hpp",
    ],
    includes = ["."],
    copts = CPP23_COPTS,
    visibility = ["//visibility:public"],
    deps = [
        "@abseil-cpp//absl/strings",
        "@iassert",
    ],
)

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
    copts = CPP23_COPTS,
    visibility = ["//visibility:public"],
    deps = [
        ":support",
        "@iassert",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:span",
    ]
)

cc_test(
    name = "blop_test",
    srcs = ["tests/blop_test.cpp"],
    copts = CPP23_COPTS,
    deps = [
        ":hlop",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "dlop_test",
    srcs = ["tests/dlop_test.cpp"],
    copts = CPP23_COPTS,
    deps = [
        ":hlop",
        "//benchref:benchref",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "slop_test",
    srcs = ["tests/slop_test.cpp"],
    copts = CPP23_COPTS,
    deps = [
        ":hlop",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "eval_test",
    srcs = ["tests/eval_test.cpp"],
    copts = CPP23_COPTS,
    deps = [
        ":hlop",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "slop_dlop_diff_test",
    srcs = ["tests/slop_dlop_diff_test.cpp"],
    copts = CPP23_COPTS,
    deps = [
        ":hlop",
        "@com_google_googletest//:gtest_main",
    ],
)
