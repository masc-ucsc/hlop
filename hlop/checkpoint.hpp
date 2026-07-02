// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
//
// Slop simulation checkpoint helpers — the editable, name-keyed state primitive.
//
// A checkpoint is a directory `ckp<cycle>/` holding:
//   regs.json   {"<hier>.<flop>": "<pyrope-literal>", ...}   (flops/regs)
//   <hier>.hex  one bare-hex entry per line, address 0 first (per memory)
//   tb.json     {"<local>": "<long>", ...}                   (testbench frame)
//   meta.json   {"cycle":..,"design_hash":..,"seed":..,"clock":..}
//
// Editable + cross-version by construction: registers round-trip through the
// Slop from_pyrope/to_pyrope codec, memories through a $readmemh-style hex codec.
// The generated `dump_state`/`load_state` (cgen_sim) call these; the generated
// driver (prp_sim) drives the fork cadence / prune / restart with them.
//
// Self-contained: depends only on slop.hpp + the C++23 standard library + POSIX
// (fork/dirent/mkdir), so the host-compiled sim driver links with no extra deps.

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <climits>
#include <cstdint>
#include <cstdlib>
#include <dirent.h>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

#include "slop.hpp"

namespace hlop::ckpt {

// One observable scalar signal (flop / pipe stage / sync-read reg / input), keyed
// by hierarchical name. Produced by the generated describe_signals() for
// `lhd sim --list-signals` and used to validate --probe / --break-when names.
struct Signal {
  std::string name;
  int         bits = 0;
  std::string kind;  // "flop" | "pipe" | "memrd" | "input"
};

// ── per-entry hex codec (memory images, $readmemh-compatible) ───────────────
// The raw unsigned N-bit pattern as MSB-first hex (no `0x`), leading zeros
// trimmed. Negatives print their two's-complement bit pattern (a memory image
// is the raw bits, not a magnitude).
template <int N>
inline std::string slop_to_hex(const Slop<N>& s) {
  std::string out;
  int         nib = (N + 3) / 4;
  for (int h = nib - 1; h >= 0; --h) {
    int v = 0;
    for (int b = 0; b < 4; ++b) {
      int pos = h * 4 + b;
      if (pos < N && s.bit_test(pos)) {
        v |= (1 << b);
      }
    }
    out.push_back("0123456789abcdef"[v]);
  }
  auto nz = out.find_first_not_of('0');
  return nz == std::string::npos ? std::string("0") : out.substr(nz);
}

inline int hex_val(char c) {
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
}

// Parse a bare/`0x`-prefixed hex string into the exact N-bit unsigned pattern.
template <int N>
inline Slop<N> slop_from_hex(std::string_view h) {
  size_t b = h.find_first_not_of(" \t");
  if (b == std::string_view::npos) {
    return Slop<N>::create_integer(0);
  }
  size_t e = h.find_last_not_of(" \t\r\n");
  h        = h.substr(b, e - b + 1);
  if (h.size() >= 2 && h[0] == '0' && (h[1] == 'x' || h[1] == 'X')) {
    h = h.substr(2);
  }
  std::string bin;
  for (char c : h) {
    int v = hex_val(c);
    if (v < 0) {
      continue;
    }
    for (int k = 3; k >= 0; --k) {
      bin.push_back(((v >> k) & 1) ? '1' : '0');
    }
  }
  if (static_cast<int>(bin.size()) > N) {
    bin = bin.substr(bin.size() - N);  // keep the low N bits
  } else if (static_cast<int>(bin.size()) < N) {
    bin = std::string(N - bin.size(), '0') + bin;
  }
  return Slop<N>::from_binary(bin, /*unsigned_result=*/true);
}

// ── memory hex file (one entry/line, address 0 first; honors `@addr` + `//`) ──
template <int B, std::size_t S>
inline void write_mem_hex(const std::string& path, const std::array<Slop<B>, S>& a) {
  std::ofstream f(path, std::ios::trunc);
  for (std::size_t i = 0; i < S; ++i) {
    f << slop_to_hex<B>(a[i]) << "\n";
  }
}

template <int B, std::size_t S>
inline bool read_mem_hex(const std::string& path, std::array<Slop<B>, S>& a) {
  std::ifstream f(path);
  if (!f) {
    return false;
  }
  std::string line;
  std::size_t i = 0;
  while (std::getline(f, line)) {
    size_t nb = line.find_first_not_of(" \t\r");
    if (nb == std::string::npos) {
      continue;
    }
    if (line[nb] == '@') {  // $readmemh address marker
      i = std::strtoul(line.c_str() + nb + 1, nullptr, 16);
      continue;
    }
    if (line[nb] == '/' || line[nb] == '#') {  // comment
      continue;
    }
    if (i < S) {
      a[i] = slop_from_hex<B>(line);
    }
    ++i;
  }
  return true;
}

// ── flat string-keyed JSON {"k":"v",...} (regs / tb / meta) ──────────────────
inline std::string json_escape(std::string_view s) {
  std::string o;
  for (char c : s) {
    switch (c) {
      case '\\': o += "\\\\"; break;
      case '"': o += "\\\""; break;
      case '\n': o += "\\n"; break;
      case '\r': o += "\\r"; break;
      case '\t': o += "\\t"; break;
      default: o += c;
    }
  }
  return o;
}

inline void write_str_map(const std::string& path, const std::map<std::string, std::string>& m) {
  std::ofstream f(path, std::ios::trunc);
  f << "{\n";
  bool first = true;
  for (const auto& [k, v] : m) {
    f << (first ? "" : ",\n") << "  \"" << json_escape(k) << "\": \"" << json_escape(v) << "\"";
    first = false;
  }
  f << (first ? "" : "\n") << "}\n";
}

// Minimal flat-object parser: the file is a sequence of "string" tokens that
// alternate key, value (the `:`/`,`/`{`/`}` punctuation is simply skipped), so
// reading strings in pairs reconstructs the map. Honors \\ \" \n \r \t escapes.
inline std::map<std::string, std::string> read_str_map(const std::string& path) {
  std::map<std::string, std::string> m;
  std::ifstream                      f(path);
  if (!f) {
    return m;
  }
  std::stringstream ss;
  ss << f.rdbuf();
  std::string s = ss.str();
  size_t      i = 0;
  auto        next_str = [&](std::string& out) -> bool {
    while (i < s.size() && s[i] != '"') {
      ++i;
    }
    if (i >= s.size()) {
      return false;
    }
    ++i;
    out.clear();
    while (i < s.size() && s[i] != '"') {
      if (s[i] == '\\' && i + 1 < s.size()) {
        char n = s[++i];
        switch (n) {
          case 'n': out += '\n'; break;
          case 'r': out += '\r'; break;
          case 't': out += '\t'; break;
          default: out += n;
        }
      } else {
        out += s[i];
      }
      ++i;
    }
    if (i < s.size()) {
      ++i;
    }
    return true;
  };
  std::string k, v;
  while (next_str(k) && next_str(v)) {
    m[k] = v;
  }
  return m;
}

// ── checkpoint directory management ──────────────────────────────────────────
inline std::string ckpt_path(const std::string& base, long cycle) {
  return base + "/ckp" + std::to_string(cycle);
}

// POSIX recursive mkdir (ignores EEXIST). std::filesystem is avoided so the
// header-only driver build has no extra link dependency.
inline void make_dirs(const std::string& path) {
  for (size_t i = 1; i < path.size(); ++i) {
    if (path[i] == '/') {
      ::mkdir(path.substr(0, i).c_str(), 0755);
    }
  }
  ::mkdir(path.c_str(), 0755);
}

// Remove a checkpoint dir (only flat files inside: regs.json / *.hex / tb.json /
// meta.json — no nested dirs).
inline void remove_dir(const std::string& path) {
  if (DIR* d = ::opendir(path.c_str())) {
    while (dirent* e = ::readdir(d)) {
      std::string n = e->d_name;
      if (n == "." || n == "..") {
        continue;
      }
      ::unlink((path + "/" + n).c_str());
    }
    ::closedir(d);
  }
  ::rmdir(path.c_str());
}

// A checkpoint is COMPLETE only once its writer (the fork child) has created the
// `_done` marker as its last step. Until then prune must not evict it and restart
// must not load it (its files may be mid-write). This makes a checkpoint visible
// atomically without a temp-dir rename.
inline std::string done_marker(const std::string& base, long cycle) { return ckpt_path(base, cycle) + "/_done"; }
inline void        mark_complete(const std::string& dir) { std::ofstream(dir + "/_done", std::ios::trunc) << "1\n"; }
inline bool        checkpoint_complete(const std::string& base, long cycle) {
  return ::access(done_marker(base, cycle).c_str(), F_OK) == 0;
}

// Sorted list of COMPLETE checkpoint cycles under `base` (parses `ckp<N>`, skips
// any still being written — no `_done` marker yet).
inline std::vector<long> list_checkpoint_cycles(const std::string& base) {
  std::vector<long> out;
  if (DIR* d = ::opendir(base.c_str())) {
    while (dirent* e = ::readdir(d)) {
      std::string n = e->d_name;
      if (n.rfind("ckp", 0) == 0 && n.size() > 3) {
        char* end = nullptr;
        long  c   = std::strtol(n.c_str() + 3, &end, 10);
        if (end != nullptr && *end == '\0' && checkpoint_complete(base, c)) {
          out.push_back(c);
        }
      }
    }
    ::closedir(d);
  }
  std::sort(out.begin(), out.end());
  return out;
}

// The largest checkpoint cycle <= target (-1 if none). The restart entry point.
inline long nearest_checkpoint_cycle(const std::string& base, long target) {
  long best = -1;
  for (long c : list_checkpoint_cycles(base)) {
    if (c <= target && c > best) {
      best = c;
    }
  }
  return best;
}

// Keep at most `max` checkpoints, removing interior points to keep the set as
// evenly spaced as possible (always keep the earliest + latest). For debugging,
// a restart near a failure then refills dense local checkpoints via the cadence.
inline void prune_checkpoints(const std::string& base, long max) {
  if (max < 2) {
    max = 2;
  }
  std::vector<long> cyc = list_checkpoint_cycles(base);
  while (static_cast<long>(cyc.size()) > max) {
    size_t victim   = 1;
    long   best_gap = LONG_MAX;
    for (size_t i = 1; i + 1 < cyc.size(); ++i) {
      long gap = cyc[i + 1] - cyc[i - 1];  // removing i leaves this neighbor gap
      if (gap < best_gap) {
        best_gap = gap;
        victim   = i;
      }
    }
    remove_dir(ckpt_path(base, cyc[victim]));
    cyc.erase(cyc.begin() + static_cast<long>(victim));
  }
}

// ── fork-based async checkpoint ──────────────────────────────────────────────
// Reap finished checkpoint children without blocking (call periodically + at
// shutdown so they do not linger as zombies).
inline void reap_checkpoints() {
  int status = 0;
  while (::waitpid(-1, &status, WNOHANG) > 0) {
    // discard
  }
}

// BLOCKING drain — wait for every in-flight checkpoint child to finish (so its
// `_done` marker is written). Call at shutdown so a checkpoint left mid-write is
// completed before the process exits (else a prompt restart could read partial state).
inline void drain_checkpoints() {
  int status = 0;
  for (;;) {
    pid_t r = ::waitpid(-1, &status, 0);  // blocking
    if (r > 0) {
      continue;  // reaped one; keep draining
    }
    if (r < 0 && errno == EINTR) {
      continue;  // interrupted by a signal; retry
    }
    break;  // ECHILD (no children) or a real error
  }
}

// Fork; the child runs `fn` (writes the checkpoint dir) then `_exit(0)` — NOT
// exit(): no atexit/stdio-flush, so the parent's open files / VCD writer are
// untouched. The parent returns immediately and keeps simulating. If fork fails
// (or use_fork is false) `fn` runs synchronously.
inline void fork_checkpoint(const std::function<void()>& fn, bool use_fork = true) {
  reap_checkpoints();
  if (use_fork) {
    pid_t pid = ::fork();
    if (pid == 0) {  // child
      fn();
      ::_exit(0);
    }
    if (pid > 0) {  // parent
      return;
    }
    // fork failed -> synchronous fallback below
  }
  fn();
}

// ── design-hash fold (meta.json design_hash; cross-version warn, never reject) ─
constexpr uint64_t kFnvOffset = 1469598103934665603ULL;
constexpr uint64_t kFnvPrime  = 1099511628211ULL;
inline uint64_t    fnv1a(uint64_t h, std::string_view s) {
  for (unsigned char c : s) {
    h ^= c;
    h *= kFnvPrime;
  }
  return h;
}
inline uint64_t fnv1a_u64(uint64_t h, uint64_t v) {
  for (int i = 0; i < 8; ++i) {
    h ^= (v & 0xff);
    h *= kFnvPrime;
    v >>= 8;
  }
  return h;
}

// ── adaptive cadence (fork keeps the parent-side cost tiny; min_secs is the
// floor, max_overhead a secondary stretch) ──────────────────────────────────
struct Cadence {
  bool                                  enabled      = false;
  double                                min_secs     = 10.0;
  double                                max_overhead = 0.10;
  double                                interval     = 10.0;  // current target (s)
  std::chrono::steady_clock::time_point last;

  void init(bool en, double mn, double mo) {
    enabled      = en;
    min_secs     = mn;
    max_overhead = mo;
    interval     = mn;
    last         = std::chrono::steady_clock::now();
  }
  bool due() const {
    if (!enabled) {
      return false;
    }
    double el = std::chrono::duration<double>(std::chrono::steady_clock::now() - last).count();
    return el >= interval;
  }
  // Call right after taking a checkpoint, with the measured parent-side cost (s).
  // Stretch the interval so that cost stays under max_overhead of it.
  void taken(double parent_cost) {
    last        = std::chrono::steady_clock::now();
    double want = (max_overhead > 0.0) ? parent_cost / max_overhead : min_secs;
    interval    = (want > min_secs) ? want : min_secs;
  }
};

}  // namespace hlop::ckpt
