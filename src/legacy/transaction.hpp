// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "editor/types.hpp"
namespace bmh::editor::legacy {
using Files = std::map<std::string, std::string>;
using Fingerprints = std::map<std::string, std::string>;
Fingerprints snapshot(const fs::path &root);
Json save(const fs::path &root, const Files &changed,
          const Fingerprints &expected);
Json backups(const fs::path &root);
Json restore(const fs::path &root, const std::string &id);
void recover(const fs::path &root);
} // namespace bmh::editor::legacy
