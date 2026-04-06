# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

cc_library(
    name = "support",
    hdrs = [
        "likely.hpp",
        "str_tools.hpp",
    ],
    includes = ["."],
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
    visibility = ["//visibility:public"],
    deps = [
        ":support",
        "@fmt",
        "@iassert",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:span",
    ]
)

cc_test(
    name = "blop_test",
    srcs = ["tests/blop_test.cpp"],
    deps = [
        ":hlop",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "dlop_test",
    srcs = ["tests/dlop_test.cpp"],
    deps = [
        ":hlop",
        "//benchref:benchref",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "slop_test",
    srcs = ["tests/slop_test.cpp"],
    deps = [
        ":hlop",
        "@com_google_googletest//:gtest_main",
    ],
)
