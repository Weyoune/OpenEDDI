// SPDX-License-Identifier: GPL-3.0-only
#include "editor/display.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
using bmh::editor::DisplayMetrics;
int main() {
  try {
    for (double ratio : {1., 1.25, 1.5, 2., 3.}) {
      DisplayMetrics m{1440, 900, int(1440 * ratio), int(900 * ratio)};
      if (std::abs(m.density() - ratio) > 1e-5)
        throw std::runtime_error("density");
      if (m.point(720, 450) != std::pair{int(720 * ratio), int(450 * ratio)})
        throw std::runtime_error("pointer scaled incorrectly");
      if (m.point(0, 0) != std::pair{0, 0} ||
          m.point(1440, 900) != std::pair{m.pixels_width, m.pixels_height})
        throw std::runtime_error("edges");
    }
    DisplayMetrics invalid{0, 0, 0, 0};
    if (invalid.valid() || invalid.point(1, 2) != std::pair{1, 2})
      throw std::runtime_error("minimized");
    std::cout << "Display density and pointer mapping passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
