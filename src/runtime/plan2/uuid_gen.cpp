#include "runtime/plan2/uuid_gen.h"

#include <array>
#include <cstdint>
#include <random>

namespace agent::plan2 {
namespace {

char HexDigit(uint8_t v) {
  static constexpr char kHex[] = "0123456789abcdef";
  return kHex[v & 0x0F];
}

// Thread-local RNG to avoid global contention.
std::mt19937_64& Rng() {
  static thread_local std::mt19937_64 rng{[] {
    std::random_device rd;
    const uint64_t seed = (static_cast<uint64_t>(rd()) << 32) ^ static_cast<uint64_t>(rd());
    return std::mt19937_64(seed);
  }()};
  return rng;
}

} // namespace

std::string GenerateUuidV4() {
  std::array<uint8_t, 16> b{};

  uint64_t r1 = Rng()();
  uint64_t r2 = Rng()();
  for (int i = 0; i < 8; ++i) b[static_cast<size_t>(i)] = static_cast<uint8_t>((r1 >> (i * 8)) & 0xFF);
  for (int i = 0; i < 8; ++i) b[static_cast<size_t>(8 + i)] = static_cast<uint8_t>((r2 >> (i * 8)) & 0xFF);

  // Set version (4) and variant (10xx).
  b[6] = static_cast<uint8_t>((b[6] & 0x0F) | 0x40);
  b[8] = static_cast<uint8_t>((b[8] & 0x3F) | 0x80);

  std::string out;
  out.resize(36);

  // 8-4-4-4-12
  int o = 0;
  auto write_byte = [&](uint8_t v) {
    out[static_cast<size_t>(o++)] = HexDigit(static_cast<uint8_t>(v >> 4));
    out[static_cast<size_t>(o++)] = HexDigit(v);
  };

  for (int i = 0; i < 4; ++i) write_byte(b[static_cast<size_t>(i)]);
  out[static_cast<size_t>(o++)] = '-';
  for (int i = 4; i < 6; ++i) write_byte(b[static_cast<size_t>(i)]);
  out[static_cast<size_t>(o++)] = '-';
  for (int i = 6; i < 8; ++i) write_byte(b[static_cast<size_t>(i)]);
  out[static_cast<size_t>(o++)] = '-';
  for (int i = 8; i < 10; ++i) write_byte(b[static_cast<size_t>(i)]);
  out[static_cast<size_t>(o++)] = '-';
  for (int i = 10; i < 16; ++i) write_byte(b[static_cast<size_t>(i)]);

  return out;
}

} // namespace agent::plan2
