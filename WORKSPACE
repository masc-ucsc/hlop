# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

workspace(name = "hlop")

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")
load("@bazel_tools//tools/build_defs/repo:git.bzl", "new_git_repository")

http_archive(
    name = "rules_foreign_cc",
    sha256 = "530719f85c171d3f8db26157e94b23af9ebde6ec2098a88813b91c02c8c1f94d",
    strip_prefix = "rules_foreign_cc-0.14.0",
    url = "https://github.com/bazelbuild/rules_foreign_cc/archive/0.14.0.zip",
)

load("@rules_foreign_cc//foreign_cc:repositories.bzl", "rules_foreign_cc_dependencies")

rules_foreign_cc_dependencies()

# google benchmark
http_archive(
    name = "com_google_benchmark",
    sha256 = "7a273667fbc23480df1306f82bdb960672811dd29a0342bb34e14040307cf820",
    strip_prefix = "benchmark-1.9.4",
    urls = ["https://github.com/google/benchmark/archive/refs/tags/v1.9.4.zip"],
)

# google tests
http_archive(
  name = "com_google_googletest",
  sha256 = "f179ec217f9b3b3f3c6e8b02d3e7eda997b49e4ce26d6b235c9053bec9c0bf9f",
  urls = ["https://github.com/google/googletest/archive/refs/tags/v1.15.2.zip"],
  strip_prefix = "googletest-1.15.2",
)

# fmt
http_archive(
    name = "fmt",
    build_file = "fmt.BUILD",
    sha256 = "ebb31f63e14048ffba9200bbe27b339ec50226cbe8e96ca8b62ee16a0a4fb1af",
    strip_prefix = "fmt-11.2.0",
    urls = [
        "https://github.com/fmtlib/fmt/archive/refs/tags/11.2.0.zip",
    ],
)

# iassert
http_archive(
    name = "iassert",
    sha256 = "9479439270fc42c58a3959d7aa5ecd207ebf0bbe9210e1d17b426530f0b096b2",
    strip_prefix = "iassert-c2136ed8809ec1addbc48eb836c58d5b895e3f2b",
    urls = [
        "https://github.com/masc-ucsc/iassert/archive/c2136ed8809ec1addbc48eb836c58d5b895e3f2b.zip",
    ],
)

# abseil
http_archive(
  name = "com_google_absl",
  strip_prefix = "abseil-cpp-20250512.0",
  urls = ["https://github.com/abseil/abseil-cpp/archive/refs/tags/20250512.0.zip"],
  sha256 = "520551a68a57f58be691151d59918a7ddcd8d727f81c0bb530bca272f985e674",
)

# Boost (needed for lconst. To remove afterwards)
http_archive(
    name = "com_github_nelhage_rules_boost",
    strip_prefix = "rules_boost-96e9b631f104b43a53c21c87b01ac538ad6f3b48",
    url = "https://github.com/nelhage/rules_boost/archive/96e9b631f104b43a53c21c87b01ac538ad6f3b48.tar.gz",
)

load("@com_github_nelhage_rules_boost//:boost/boost.bzl", "boost_deps")

boost_deps()

