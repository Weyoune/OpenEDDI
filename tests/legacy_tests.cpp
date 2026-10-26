// SPDX-License-Identifier: GPL-3.0-only
#include "content/digest.hpp"
#include "legacy/transaction.hpp"
#include <iostream>
using namespace bmh::editor;
int main() {
  auto root = fs::temp_directory_path() / new_id("openeddi-test-");
  fs::create_directories(root);
  try {
    write_atomic(root / "M3D.MAN", "original");
    auto expected = legacy::snapshot(root);
    auto r = legacy::save(root, {{"M3D.MAN", "edited"}}, expected);
    if (read_file(root / "M3D.MAN") != "edited" ||
        legacy::backups(root).size() != 1)
      throw std::runtime_error("save/backup");
    legacy::restore(root, str(r, "backup"));
    if (read_file(root / "M3D.MAN") != "original")
      throw std::runtime_error("restore");
    bool rejected = false;
    try {
      legacy::save(root, {{"M3D.MAN", "other"}}, {{"M3D.MAN", "wrong"}});
    } catch (const Error &) {
      rejected = true;
    }
    if (!rejected)
      throw std::runtime_error("stale source");
    auto count = legacy::backups(root).size();
    legacy::save(root, {}, expected);
    if (legacy::backups(root).size() != count)
      throw std::runtime_error("no-op");
    rejected = false;
    try {
      legacy::save(root, {{"../outside", "bad"}}, {});
    } catch (const Error &) {
      rejected = true;
    }
    if (!rejected)
      throw std::runtime_error("unsafe path");
    fs::remove_all(root);
    std::cout << "transaction checks passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    fs::remove_all(root);
    return 1;
  }
}
