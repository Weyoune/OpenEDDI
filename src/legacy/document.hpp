// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "editor/types.hpp"

namespace bmh::editor {
class World;
}
namespace bmh::editor::legacy {
// The writer owns the original disk bytes, never the engine's translated image.
class Document {
public:
  explicit Document(const fs::path &installation_or_exe);
  const fs::path &root() const { return root_; }
  Json metadata() const;
  void capture(World &);
  void validate_change(const std::string &table,
                       const std::optional<Json> &before,
                       const std::optional<Json> &after) const;
  static bool editable(const std::string &table,
                       const std::string &json_pointer);
  std::map<std::string, std::string> prepare(const World &) const;
  Json roster_plan(const World &) const;
  void mark_saved(const World &);
  // Synthetic fixtures use the same strict parser and writer without a game
  // EXE.
  static Document from_bytes(std::string bytes, bool english = false);
  static std::string encode_text(const std::string &utf8);
  // Display-only rating derived from the three stored classic attributes.
  static int strength(const Json &player);

private:
  Document() = default;
  fs::path root_;
  std::string file_, source_;
  bool english_ = false;
  std::map<std::string, Json> baseline_;
  std::map<std::string, size_t> nations_, teams_;
  void remember(const World &);
};
} // namespace bmh::editor::legacy
