#include "claim_id.hpp"

#include <chrono>
#include <cstdint>
#include <ctime>
#include <iomanip>
#include <random>
#include <sstream>

namespace rctx {

namespace {

uint32_t random_u24() {
  static thread_local std::mt19937 rng{std::random_device{}()};
  static thread_local std::uniform_int_distribution<uint32_t> dist(0, 0xFFFFFF);
  return dist(rng);
}

}  // namespace

std::string new_claim_id() {
  const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  std::tm utc{};
  gmtime_r(&now, &utc);

  char stamp[16];
  std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &utc);

  std::ostringstream out;
  out << stamp << '-' << std::hex << std::setfill('0') << std::setw(6) << random_u24();
  return out.str();
}

}  // namespace rctx
