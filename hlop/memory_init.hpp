// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace hlop {
// A string-valued Memory.initial is a startup command, never packed reset data.
// Keep this versioned spelling stable across serialized LGraph/LNAST artifacts.
struct Memory_image {
  int         radix;
  std::string path;
};
inline std::optional<Memory_image> memory_image(std::string_view text) {
  int radix = 0;
  if (text.starts_with("@readmemh:1:")) {
    radix = 16;
  } else if (text.starts_with("@readmemb:1:")) {
    radix = 2;
  } else {
    return std::nullopt;
  }
  text.remove_prefix(std::string_view("@readmemh:1:").size());
  auto hex = [](char c) -> int {
    if (c >= '0' && c <= '9') {
      return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
      return c - 'a' + 10;
    }
    return -1;
  };
  std::string path;
  for (std::size_t i = 0; i < text.size(); ++i) {
    if (text[i] == '%') {
      if (i + 2 >= text.size() || hex(text[i + 1]) < 0 || hex(text[i + 2]) < 0) {
        return std::nullopt;
      }
      path += static_cast<char>(hex(text[i + 1]) * 16 + hex(text[i + 2]));
      i    += 2;
    } else {
      path += text[i];
    }
  }
  return Memory_image{radix, std::move(path)};
}
inline std::string memory_image_command(int radix, std::string_view path) {
  std::string                command = radix == 16 ? "@readmemh:1:" : "@readmemb:1:";
  // Keep the descriptor safe in Dlop's single-quoted textual serialization.
  constexpr std::string_view hex     = "0123456789abcdef";
  for (unsigned char c : path) {
    if (c == '%' || c == '\'' || c == '"' || c == '\\' || c < 32) {
      command += '%';
      command += hex[c >> 4];
      command += hex[c & 15];
    } else {
      command += static_cast<char>(c);
    }
  }
  return command;
}
}  // namespace hlop
