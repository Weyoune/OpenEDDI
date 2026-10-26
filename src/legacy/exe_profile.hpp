// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string_view>

namespace bmh::editor::legacy {
// Detection only. These are physical file bytes in the verified German LE
// routing routine; no translated/relocated LeImage is accepted or patched.
inline bool has_regional_crest_routing(std::string_view bytes) {
  const auto scalar = [&](size_t at, size_t count) -> uint32_t {
    if (at > bytes.size() || count > bytes.size() - at) return 0;
    uint32_t value = 0;
    for (size_t i = 0; i < count; ++i) value |= uint32_t(uint8_t(bytes[at + i])) << (i * 8);
    return value;
  };
  const auto matches = [&](size_t at, std::initializer_list<uint8_t> expected) {
    if (at > bytes.size() || expected.size() > bytes.size() - at) return false;
    for (auto value : expected) if (uint8_t(bytes[at++]) != value) return false;
    return true;
  };
  if (bytes.size() != 602527 || bytes.substr(0, 2) != "MZ") return false;
  const size_t le = scalar(0x3c, 4);
  if (le > bytes.size() || bytes.size() - le < 0x84 || bytes.substr(le, 2) != "LE" ||
      scalar(le + 2, 2) != 0 || scalar(le + 0x28, 4) != 4096 ||
      scalar(le + 0x80, 4) != 0x1ac00 || scalar(le + 0x44, 4) == 0) return false;
  const uint64_t object = uint64_t(le) + scalar(le + 0x40, 4),
                 map = uint64_t(le) + scalar(le + 0x48, 4);
  if (object + 24 > bytes.size() || map + 4 > bytes.size() ||
      scalar(size_t(object), 4) < 0x124 || scalar(size_t(object) + 4, 4) != 0x10000 ||
      scalar(size_t(object) + 12, 4) != 1 || scalar(size_t(object) + 16, 4) == 0 ||
      !matches(size_t(map), {0, 0, 1, 0})) return false;
  // Keep the 0xfff sentinel, both range exits and the far-foreign fallback.
  // The changed immediate is 90 instead of 138: only 90..137 become individual.
  return matches(0x1ac5f, {0x66, 0x81, 0xfb, 0xff, 0x0f}) &&
         matches(0x1ac64, {0x0f, 0x84, 0x9b, 0, 0, 0}) &&
         matches(0x1ac6a, {0x66, 0x83, 0xfb, 0x5a}) &&
         matches(0x1ac6e, {0x7c, 0x19}) &&
         matches(0x1ac70, {0x66, 0x81, 0xfb, 0x5a, 0}) &&
         matches(0x1ac75, {0x7c, 0x07}) &&
         matches(0x1ac77, {0x66, 0x81, 0xfb, 0xa8, 0}) &&
         matches(0x1ac7c, {0x7c, 0x0b}) &&
         matches(0x1ac7e, {0x66, 0x81, 0xfb, 0x65, 0x01}) &&
         matches(0x1ac83, {0x0f, 0x8c, 0x7c, 0, 0, 0}) &&
         matches(0x1ac89, {0x0f, 0xbf, 0xc3});
}
} // namespace bmh::editor::legacy
