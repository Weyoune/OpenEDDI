// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <algorithm>
#include <utility>
namespace bmh::editor {
struct DisplayMetrics {
  int width = 0, height = 0, pixels_width = 0, pixels_height = 0;
  bool valid() const {
    return width > 0 && height > 0 && pixels_width > 0 && pixels_height > 0;
  }
  float density() const {
    return valid() ? float(pixels_width) / width *
                         std::clamp(std::min(width / 1440.f, height / 900.f),
                                    .78f, 1.8f)
                   : 1.f;
  }
  std::pair<int, int> point(int x, int y) const {
    return valid() ? std::pair{int(double(x) * pixels_width / width),
                               int(double(y) * pixels_height / height)}
                   : std::pair{x, y};
  }
};
} // namespace bmh::editor
