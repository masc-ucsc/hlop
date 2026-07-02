//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "checkpoint.hpp"

#include <array>
#include <cstdlib>
#include <map>
#include <string>

#include "gtest/gtest.h"
#include "slop.hpp"

using namespace hlop::ckpt;

namespace {
std::string tmp_root() {
  const char* t = std::getenv("TEST_TMPDIR");
  std::string base = (t != nullptr) ? std::string(t) : std::string("/tmp");
  return base + "/ckpt_test";
}
}  // namespace

class Checkpoint_test : public ::testing::Test {};

// ── hex codec round-trips at several widths (incl. top-bit-set + wide) ───────
TEST_F(Checkpoint_test, hex_roundtrip_small) {
  for (int v : {0, 1, 5, 15, 16, 200, 255}) {
    auto s = Slop<8>::create_integer(v & 0xff);
    auto r = slop_from_hex<8>(slop_to_hex<8>(s));
    EXPECT_EQ(r.to_just_i64() & 0xff, v & 0xff) << "v=" << v << " hex=" << slop_to_hex<8>(s);
  }
}

TEST_F(Checkpoint_test, hex_topbit_set) {
  auto s = Slop<8>::from_pyrope("0xff");  // bit pattern 11111111
  EXPECT_EQ(slop_to_hex<8>(s), "ff");
  auto r = slop_from_hex<8>("ff");
  EXPECT_EQ(slop_to_hex<8>(r), "ff");
}

TEST_F(Checkpoint_test, hex_with_0x_prefix) {
  auto r = slop_from_hex<16>("0x00ab");
  EXPECT_EQ(slop_to_hex<16>(r), "ab");
}

TEST_F(Checkpoint_test, hex_roundtrip_wide) {
  // > 64 bits: a value with bits in both words.
  auto s = Slop<96>::from_pyrope("0x1234500000000000000abcde");
  auto r = slop_from_hex<96>(slop_to_hex<96>(s));
  EXPECT_EQ(slop_to_hex<96>(r), slop_to_hex<96>(s));
  EXPECT_EQ(slop_to_hex<96>(s), "1234500000000000000abcde");
}

TEST_F(Checkpoint_test, hex_zero) { EXPECT_EQ(slop_to_hex<32>(Slop<32>::create_integer(0)), "0"); }

// ── memory hex file round-trip ───────────────────────────────────────────────
TEST_F(Checkpoint_test, mem_hex_roundtrip) {
  make_dirs(tmp_root());
  std::array<Slop<12>, 6> a{};
  for (int i = 0; i < 6; ++i) {
    a[i] = Slop<12>::create_integer((i * 37 + 1) & 0xfff);
  }
  std::string path = tmp_root() + "/m.hex";
  write_mem_hex(path, a);

  std::array<Slop<12>, 6> b{};
  ASSERT_TRUE(read_mem_hex(path, b));
  for (int i = 0; i < 6; ++i) {
    EXPECT_EQ(b[i].to_just_i64() & 0xfff, a[i].to_just_i64() & 0xfff) << "i=" << i;
  }
  // a missing file leaves the array untouched and returns false
  std::array<Slop<12>, 6> c{};
  EXPECT_FALSE(read_mem_hex(tmp_root() + "/nope.hex", c));
}

TEST_F(Checkpoint_test, mem_hex_honors_addr_and_comments) {
  make_dirs(tmp_root());
  std::string path = tmp_root() + "/m2.hex";
  {
    std::ofstream f(path);
    f << "// comment\n";
    f << "1\n";       // addr 0
    f << "@3\n";      // jump to addr 3
    f << "ff\n";      // addr 3
  }
  std::array<Slop<8>, 4> a{};
  ASSERT_TRUE(read_mem_hex(path, a));
  EXPECT_EQ(a[0].to_just_i64() & 0xff, 1);
  EXPECT_EQ(a[1].to_just_i64() & 0xff, 0);
  EXPECT_EQ(a[3].to_just_i64() & 0xff, 0xff);
}

// ── flat json (regs / tb / meta) round-trip, incl. escapes ───────────────────
TEST_F(Checkpoint_test, str_map_roundtrip) {
  make_dirs(tmp_root());
  std::map<std::string, std::string> m{
      {"top.cpu0.pc", "0x40"},
      {"top.cpu0.flag", "true"},
      {"top.s", "'hi there'"},
      {"weird", "a\"b\\c"},  // quote + backslash must survive
  };
  std::string path = tmp_root() + "/regs.json";
  write_str_map(path, m);
  auto r = read_str_map(path);
  EXPECT_EQ(r, m);
}

TEST_F(Checkpoint_test, str_map_missing_file) { EXPECT_TRUE(read_str_map(tmp_root() + "/none.json").empty()); }

// ── checkpoint directory scan / nearest / prune ──────────────────────────────
TEST_F(Checkpoint_test, dir_scan_and_nearest) {
  std::string base = tmp_root() + "/ckdir";
  remove_dir(base);  // clean slate (also removes if it was a file)
  make_dirs(base);
  for (long c : {0L, 100L, 250L, 400L}) {
    make_dirs(ckpt_path(base, c));
    mark_complete(ckpt_path(base, c));  // only completed checkpoints are listed
  }
  // an in-progress checkpoint (no _done marker) must NOT be listed
  make_dirs(ckpt_path(base, 500));
  auto cyc = list_checkpoint_cycles(base);
  ASSERT_EQ(cyc.size(), 4u);
  EXPECT_EQ(cyc.front(), 0);
  EXPECT_EQ(cyc.back(), 400);
  EXPECT_EQ(nearest_checkpoint_cycle(base, 300), 250);
  EXPECT_EQ(nearest_checkpoint_cycle(base, 400), 400);
  EXPECT_EQ(nearest_checkpoint_cycle(base, 99), 0);
  EXPECT_EQ(nearest_checkpoint_cycle(base, -5), -1);
}

TEST_F(Checkpoint_test, prune_keeps_endpoints_and_spaces) {
  std::string base = tmp_root() + "/prunedir";
  remove_dir(base);
  make_dirs(base);
  // 0,10,20,30,40,50,60 -> prune to 4: keep 0 and 60, evenly space the rest.
  for (long c = 0; c <= 60; c += 10) {
    make_dirs(ckpt_path(base, c));
    mark_complete(ckpt_path(base, c));
  }
  prune_checkpoints(base, 4);
  auto cyc = list_checkpoint_cycles(base);
  EXPECT_EQ(cyc.size(), 4u);
  EXPECT_EQ(cyc.front(), 0);
  EXPECT_EQ(cyc.back(), 60);
}

// ── design-hash fold is deterministic + sensitive ────────────────────────────
TEST_F(Checkpoint_test, design_hash_fold) {
  uint64_t a = fnv1a_u64(fnv1a(kFnvOffset, "count"), 8);
  uint64_t b = fnv1a_u64(fnv1a(kFnvOffset, "count"), 8);
  uint64_t c = fnv1a_u64(fnv1a(kFnvOffset, "count"), 16);  // width change
  EXPECT_EQ(a, b);
  EXPECT_NE(a, c);
}
