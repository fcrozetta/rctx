#include "uuid.hpp"

#include <chrono>
#include <cstdint>
#include <iomanip>
#include <random>
#include <sstream>

namespace rctx {

namespace {

uint64_t random_u64() {
  static thread_local std::mt19937_64 rng{std::random_device{}()};
  static thread_local std::uniform_int_distribution<uint64_t> dist;
  return dist(rng);
}

std::string hex16(uint64_t v) {
  std::ostringstream out;
  out << std::hex << std::setfill('0') << std::setw(16) << v;
  return out.str();
}

}  // namespace

std::string new_sortable_uuid() {
  const auto ts_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::system_clock::now().time_since_epoch())
                          .count();
  constexpr uint64_t kTsMask = 0xFFFFFFFFFFFFULL;  // 48 bits
  const uint64_t inverted_ts = kTsMask - (static_cast<uint64_t>(ts_ms) & kTsMask);

  // hi: 48-bit inverted timestamp | version (8) | 12 bits of randomness.
  const uint64_t hi = (inverted_ts << 16) | (0x8ULL << 12) | (random_u64() & 0x0FFFULL);
  // lo: variant (0b10) | 62 bits of randomness.
  const uint64_t lo = (0x2ULL << 62) | (random_u64() & 0x3FFFFFFFFFFFFFFFULL);

  const std::string h = hex16(hi);
  const std::string l = hex16(lo);
  return h.substr(0, 8) + "-" + h.substr(8, 4) + "-" + h.substr(12, 4) + "-" + l.substr(0, 4) +
         "-" + l.substr(4, 12);
}

}  // namespace rctx
