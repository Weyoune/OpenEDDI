// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "editor/images.hpp"
#include <array>

namespace bmh::editor {
class World;
}
namespace bmh::editor::legacy {
using GraphicPalette = std::array<uint32_t, 256>;
struct IndexedVga {
  int width = 0, height = 0;
  std::vector<uint8_t> pixels;
  uint8_t trailing = 0;
  std::string suffix;
};
IndexedVga decode_vga(std::string_view);
std::string encode_vga(const IndexedVga &);
// Physical slots require exact source bytes and explicit geometry. No
// normalized executable address can be passed here; callers must prove a file
// offset first.
struct VerifiedEmbeddedSlot {
  size_t file_offset = 0;
  int width = 0, height = 0, stride = 0;
  std::string expected;
};
std::string patch_embedded_indices(std::string_view file,
                                   const VerifiedEmbeddedSlot &,
                                   const std::vector<uint8_t> &indices);
images::Raster convert_indexed(const images::Raster &, int width, int height,
                               const GraphicPalette &, bool transparent,
                               const Json &recipe = Json::object());
class Graphics {
public:
  explicit Graphics(const fs::path &root);
  void capture(World &);
  bool supports(const std::string &key) const;
  std::string target_key(const World &, const Json &import_args) const;
  images::Raster convert(const World &, const std::string &key,
                         const images::Raster &, const Json &recipe) const;
  std::map<std::string, std::string> prepare(const World &) const;
  void mark_saved(World &);
  Json info(const std::string &key) const;

private:
  struct File {
    std::string original;
    IndexedVga image;
    GraphicPalette palette{};
  };
  struct Slot {
    std::string file, png, pixels, baseline_png;
    int start = 0, width = 0, height = 0;
    bool transparent = false, whole = false, shared_fallback = false;
    GraphicPalette palette{};
    std::vector<uint8_t> baseline;
    Json usages = Json::array();
  };
  fs::path root_;
  GraphicPalette palette_{};
  std::map<std::string, File> files_;
  std::map<std::string, Slot> slots_;
};
} // namespace bmh::editor::legacy
