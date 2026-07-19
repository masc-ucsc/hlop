//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// The shared Slop/Dlop formatting API — to_decimal(digits, sep),
// to_hex(digits, sep, upper), and the to_binary(digits, sep) DISPLAY overload
// — must behave identically in both classes: this is what makes comptime
// string interpolation (constprop's __fmt fold over Dlop) and the sim
// driver's runtime interpolation (over Slop) one algorithm. Conventions
// under test:
//   * `digits` zero-pads after any leading '-';
//   * `sep` groups '_' every 4 digits from the LSB (3 for decimal);
//   * the display to_binary STRIPS leading zeros; the no-arg to_binary()
//     stays full declared width (VCD/raw callers);
//   * everything is exact at ANY width (no 64-bit truncation).

#include <string>
#include <vector>

#include "dlop.hpp"
#include "gtest/gtest.h"
#include "slop.hpp"

namespace {

using S = Slop<200>;  // headroom well past one 64-bit word

struct Case {
  const char* lit;      // pyrope literal, parsed by BOTH classes
  int         digits;
  bool        sep;
  bool        upper;
  const char* dec;      // expected to_decimal(digits, sep)
  const char* hex;      // expected to_hex(digits, sep, upper)
  const char* bin;      // expected to_binary(digits, sep)
};

TEST(FormatApi, SlopDlopParity) {
  const std::vector<Case> cases = {
      {"255", 0, false, false, "255", "ff", "11111111"},
      {"255", 4, false, false, "0255", "00ff", "11111111"},
      {"255", 12, true, false, "000_000_000_255", "0000_0000_00ff", "0000_1111_1111"},
      {"255", 0, false, true, "255", "FF", "11111111"},
      {"0", 0, false, false, "0", "0", "0"},
      {"0", 4, false, false, "0000", "0000", "0000"},
      {"1234567", 0, true, false, "1_234_567", "12_d687", "1_0010_1101_0110_1000_0111"},
      {"-255", 6, false, false, "-000255", "-0000ff", ""},  // bin: skip (2c view)
      // > 64 bits: 2^76 + 0xAB — the case a 64-bit round-trip truncates.
      {"0x100000000000000000AB", 0, false, false, "75557863725914323419307", "100000000000000000ab", ""},
      {"0x100000000000000000AB", 0, true, true, "75_557_863_725_914_323_419_307", "1000_0000_0000_0000_00AB", ""},
      {"0x100000000000000000AB", 24, false, false, "075557863725914323419307", "0000100000000000000000ab", ""},
  };
  for (const auto& c : cases) {
    auto d = Dlop::from_pyrope(c.lit);
    ASSERT_TRUE(d) << c.lit;
    auto s = S::from_pyrope(c.lit);
    EXPECT_EQ(d->to_decimal(c.digits, c.sep), c.dec) << "Dlop dec " << c.lit;
    EXPECT_EQ(s.to_decimal(c.digits, c.sep), c.dec) << "Slop dec " << c.lit;
    EXPECT_EQ(d->to_hex(c.digits, c.sep, c.upper), c.hex) << "Dlop hex " << c.lit;
    EXPECT_EQ(s.to_hex(c.digits, c.sep, c.upper), c.hex) << "Slop hex " << c.lit;
    if (c.bin[0] != '\0') {
      EXPECT_EQ(d->to_binary(c.digits, c.sep), c.bin) << "Dlop bin " << c.lit;
      EXPECT_EQ(s.to_binary(c.digits, c.sep), c.bin) << "Slop bin " << c.lit;
    }
    // Cross-class parity holds even where the table has no pinned expectation.
    EXPECT_EQ(d->to_binary(c.digits, c.sep), s.to_binary(c.digits, c.sep)) << "bin parity " << c.lit;
  }
}

TEST(FormatApi, DisplayBinaryStripsButRawKeepsWidth) {
  // Display overload: leading zeros drop (then pad extends deliberately).
  auto d = Dlop::from_pyrope("16");
  EXPECT_EQ(d->to_binary(0, false), "10000");
  EXPECT_EQ(S::from_pyrope("16").to_binary(0, false), "10000");
  // No-arg to_binary(): full value width, leading zeros preserved — the
  // VCD/raw-caller contract (only prefix-invariance is pinned, not the exact
  // declared width across classes).
  auto raw = S::from_pyrope("16").to_binary();
  EXPECT_TRUE(raw.size() >= 5 && raw.substr(raw.size() - 5) == "10000") << raw;
}

}  // namespace
