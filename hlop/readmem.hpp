// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cctype>
#include <cstddef>
#include <fstream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "memory_init.hpp"

namespace hlop {
struct Memory_image_word {
  std::size_t address;
  std::string binary;
};
// Parse before committing any entries: bad files never leave a half-loaded memory.
// @addresses are hexadecimal in both formats. Partial images preserve other entries.
inline std::vector<Memory_image_word> read_memory_image(const std::string& path, int radix, int bits, std::size_t size) {
  const auto fail = [&](std::string_view why) -> void { throw std::runtime_error("readmem: " + path + ": " + std::string(why)); };
  if ((radix != 2 && radix != 16) || bits <= 0 || size == 0) {
    fail("invalid memory shape or radix");
  }
  if (path.find('\0') != std::string::npos) {
    fail("filename contains NUL");
  }
  std::ifstream file(path);
  if (!file) {
    fail("cannot open image");
  }
  std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  if (file.bad()) {
    fail("cannot read image");
  }
  std::vector<Memory_image_word> words;
  std::size_t                    pos = 0, address = 0;
  auto                           digit = [](char c) -> int {
    if (c >= '0' && c <= '9') {
      return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
      return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
      return c - 'A' + 10;
    }
    return -1;
  };
  while (pos < text.size()) {
    if (std::isspace(static_cast<unsigned char>(text[pos]))) {
      ++pos;
      continue;
    }
    if (text.compare(pos, 2, "//") == 0) {
      auto end = text.find('\n', pos + 2);
      pos      = end == std::string::npos ? text.size() : end + 1;
      continue;
    }
    if (text.compare(pos, 2, "/*") == 0) {
      auto end = text.find("*/", pos + 2);
      if (end == std::string::npos) {
        fail("unterminated block comment");
      }
      pos = end + 2;
      continue;
    }
    std::string token;
    while (pos < text.size() && !std::isspace(static_cast<unsigned char>(text[pos])) && text[pos] != '/') {
      if (text[pos] != '_') {
        token += text[pos];
      }
      ++pos;
    }
    if (token.empty()) {
      fail("invalid token");
    }
    if (token[0] == '@') {
      if (token.size() == 1) {
        fail("empty address");
      }
      address = 0;
      for (std::size_t i = 1; i < token.size(); ++i) {
        int v = digit(token[i]);
        if (v < 0 || address > (std::numeric_limits<std::size_t>::max() - v) / 16) {
          fail("invalid address");
        }
        address = address * 16 + v;
      }
      if (address >= size) {
        fail("address outside memory");
      }
      continue;
    }
    if (address >= size) {
      fail("image exceeds memory depth");
    }
    std::string binary;
    for (char c : token) {
      const int width = radix == 16 ? 4 : 1;
      if (c == 'x' || c == 'X' || c == 'z' || c == 'Z' || c == '?') {
        binary.append(width, '?');
      } else {
        int v = digit(c);
        if (v < 0 || v >= radix) {
          fail("invalid data digit");
        }
        for (int b = width - 1; b >= 0; --b) {
          binary += (v & (1 << b)) ? '1' : '0';
        }
      }
    }
    // Like $readmemh, a word is truncated to the destination width.
    if (binary.size() > static_cast<std::size_t>(bits)) {
      binary.erase(0, binary.size() - bits);
    } else {
      binary.insert(0, static_cast<std::size_t>(bits) - binary.size(), '0');
    }
    words.push_back({address++, std::move(binary)});
  }
  return words;
}
}  // namespace hlop
