# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

workspace(name = "hlop")

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")
load("@bazel_tools//tools/build_defs/repo:git.bzl", "new_git_repository")

http_archive(
    name = "rules_foreign_cc",
    sha256 = "5303e3363fe22cbd265c91fce228f84cf698ab0f98358ccf1d95fba227b308f6",
    strip_prefix = "rules_foreign_cc-0.9.0",
    url = "https://github.com/bazelbuild/rules_foreign_cc/archive/0.9.0.zip",
)

load("@rules_foreign_cc//foreign_cc:repositories.bzl", "rules_foreign_cc_dependencies")

rules_foreign_cc_dependencies()

# google benchmark
http_archive(
    name = "com_google_benchmark",
    sha256 = "0094b77c01e9973876f1831f76cbae43fdaf09b424e467dc4171abaa6884e455",
    strip_prefix = "benchmark-1.8.2",
    urls = ["https://github.com/google/benchmark/archive/refs/tags/v1.8.2.zip"],
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
    sha256 = "5bf4d5358301fdf3bd100c01b9d4c1fbb2091dc2267fb4fa6d7cd522b3e47179",
    strip_prefix = "fmt-10.0.0",
    urls = [
        "https://github.com/fmtlib/fmt/archive/refs/tags/10.0.0.zip",
    ],
)

# iassert
http_archive(
    name = "iassert",
    sha256 = "c6bf66a76d5a1de57c45dba137c9b51ab3b4f3a31e5de9e3c3496d7d36a128f8",
    strip_prefix = "iassert-5c18eb082262532f621a23023f092f4119a44968",
    urls = [
        "https://github.com/masc-ucsc/iassert/archive/5c18eb082262532f621a23023f092f4119a44968.zip",
    ],
)

# abseil
http_archive(
  name = "com_google_absl",
  strip_prefix = "abseil-cpp-20230125.3",
  urls = ["https://github.com/abseil/abseil-cpp/archive/refs/tags/20230125.3.zip"],
  sha256 = "51d676b6846440210da48899e4df618a357e6e44ecde7106f1e44ea16ae8adc7",
)

# Boost (needed for lconst. To remove afterwards)
http_archive(
    name = "com_github_nelhage_rules_boost",
    strip_prefix = "rules_boost-96e9b631f104b43a53c21c87b01ac538ad6f3b48",
    url = "https://github.com/nelhage/rules_boost/archive/96e9b631f104b43a53c21c87b01ac538ad6f3b48.tar.gz",
)

load("@com_github_nelhage_rules_boost//:boost/boost.bzl", "boost_deps")

boost_deps()

