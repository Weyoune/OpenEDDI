// SPDX-License-Identifier: GPL-3.0-only
#include "content/digest.hpp"
#include "legacy/transaction.hpp"
#include <functional>
#include <iostream>
#include <iterator>

using namespace bmh::editor;
using namespace bmh::editor::legacy;
namespace {
void check(bool ok, const char *why) {
  if (!ok)
    throw std::runtime_error(why);
}
template <class F> void rejects(F action, const char *why) {
  try {
    action();
  } catch (const std::exception &) {
    return;
  }
  throw std::runtime_error(why);
}
struct Fixture {
  fs::path base =
      fs::temp_directory_path() / path(new_id("transaction-failure-"));
  fs::path root = base / "installation", outside = base / "outside";
  const Files original{{"KB/1.VGA", "original image"},
                       {"M3D.MAN", "original database"}};
  const Files edited{{"KB/1.VGA", "edited image"},
                     {"M3D.MAN", "edited database"}};
  Fingerprints expected;
  Fixture() {
    fs::create_directories(root / "KB");
    fs::create_directories(outside);
    for (const auto &[name, data] : original)
      write_atomic(root / path(name), data);
    write_atomic(outside / "sentinel", "outside must survive");
    expected = snapshot(root);
  }
  ~Fixture() {
    std::error_code ec;
    fs::remove_all(base, ec);
  }
  void unchanged(const Files &want) const {
    for (const auto &[name, data] : want)
      check(read_file(root / path(name)) == data,
            "original file changed unexpectedly");
    check(read_file(outside / "sentinel") == "outside must survive",
          "outside sentinel changed");
  }
  fs::path metadata() const { return root / ".openeddi"; }
  Json interrupted(const std::string &id = "synthetic-interrupted") {
    Json j{{"id", id}, {"state", "writing"}, {"files", Json::array()}};
    const auto backup = metadata() / "backups" / path(id);
    for (const auto &[name, data] : original) {
      fs::create_directories((backup / "original" / path(name)).parent_path());
      write_atomic(backup / "original" / path(name), data);
      j["files"].push_back({{"path", name},
                            {"before", bmh::content::sha256(data)},
                            {"after", bmh::content::sha256(edited.at(name))}});
    }
    write_atomic(backup / "manifest.json", j.dump());
    write_atomic(metadata() / "transaction.json", j.dump());
    return j;
  }
};
void no_op_and_external_changes() {
  Fixture f;
  check(!save(f.root, {}, f.expected).value("changed", true),
        "empty save reports changed");
  check(backups(f.root).empty(), "empty save created backup");
  check(!save(f.root, f.original, f.expected).value("changed", true),
        "identical byte save reports changed");
  check(backups(f.root).empty(), "identical byte save created backup");
  write_atomic(f.root / "M3D.MAN", "external editor bytes");
  rejects([&] { save(f.root, f.edited, f.expected); },
          "external changes were overwritten");
  check(read_file(f.root / "M3D.MAN") == "external editor bytes",
        "failed save changed external edit");
  check(read_file(f.root / "KB/1.VGA") == f.original.at("KB/1.VGA"),
        "stale multi-file save partially wrote");
  check(backups(f.root).empty(), "stale input created backup");
  rejects(
      [&] {
        save(f.root, {{"missing.MAN", "new bytes"}},
             {{"missing.MAN", bmh::content::sha256("old")}});
      },
      "missing original accepted");
  check(!fs::exists(f.root / "missing.MAN"), "missing original was created");
}
void backup_and_stage_failures() {
  for (const auto *blocked : {"backups", "staging"}) {
    Fixture f;
    fs::create_directories(f.metadata());
    write_atomic(f.metadata() / blocked,
                 "regular file blocks destination directory");
    rejects([&] { save(f.root, f.edited, f.expected); },
            "blocked backup/staging destination accepted");
    f.unchanged(f.original);
    check(!fs::exists(f.metadata() / "transaction.json"),
          "backup failure left writing journal");
  }
}
void corrupted_restore() {
  Fixture f;
  const auto result = save(f.root, f.edited, f.expected);
  const auto id = str(result, "backup");
  f.unchanged(f.edited);
  const auto before_count = backups(f.root).size();
  write_atomic(f.metadata() / "backups" / path(id) / "original/M3D.MAN",
               "corrupt backup");
  rejects([&] { restore(f.root, id); }, "corrupt backup restored");
  f.unchanged(f.edited);
  check(backups(f.root).size() == before_count,
        "corrupt restore created another backup");
  for (const auto *bad : {"../escape", "x/y", "x\\y", ""})
    rejects([&] { restore(f.root, bad); }, "unsafe backup identifier accepted");
  f.unchanged(f.edited);
}
void interrupted_recovery() {
  {
    Fixture f;
    f.interrupted();
    write_atomic(f.root / "KB/1.VGA", f.edited.at("KB/1.VGA"));
    recover(f.root);
    f.unchanged(f.original);
    check(!fs::exists(f.metadata() / "transaction.json"),
          "successful recovery left journal");
    recover(f.root);
    f.unchanged(f.original);
  }
  {
    Fixture f;
    f.interrupted();
    for (const auto &[name, bytes] : f.edited)
      write_atomic(f.root / path(name), bytes);
    write_atomic(f.metadata() /
                     "backups/synthetic-interrupted/original/M3D.MAN",
                 "bad backup");
    rejects([&] { recover(f.root); }, "corrupt interrupted backup accepted");
    f.unchanged(f.edited);
    check(fs::exists(f.metadata() / "transaction.json"),
          "failed recovery discarded journal");
  }
  {
    Fixture f;
    f.interrupted();
    write_atomic(f.root / "KB/1.VGA", f.edited.at("KB/1.VGA"));
    write_atomic(f.root / "M3D.MAN", "new external edit after crash");
    rejects([&] { recover(f.root); },
            "recovery overwrote bytes outside journal before/after states");
    check(read_file(f.root / "M3D.MAN") == "new external edit after crash",
          "recovery lost external edit");
    check(read_file(f.root / "KB/1.VGA") == f.edited.at("KB/1.VGA"),
          "recovery wrote before validating all targets");
    check(fs::exists(f.metadata() / "transaction.json"),
          "conflicting recovery discarded journal");
  }
  for (const auto *state : {"ready", "complete"}) {
    Fixture f;
    auto j = f.interrupted();
    j["state"] = state;
    if (std::string(state) == "complete")
      for (const auto &[name, data] : f.edited)
        write_atomic(f.root / path(name), data);
    write_atomic(f.metadata() / "transaction.json", j.dump());
    recover(f.root);
    f.unchanged(std::string(state) == "complete" ? f.edited : f.original);
    check(!fs::exists(f.metadata() / "transaction.json"),
          "completed/ready journal not cleared");
  }
  {
    Fixture f;
    auto j = f.interrupted();
    j["state"] = "unrecognized";
    write_atomic(f.metadata() / "transaction.json", j.dump());
    rejects([&] { recover(f.root); }, "unknown journal state accepted");
    f.unchanged(f.original);
    check(fs::exists(f.metadata() / "transaction.json"),
          "unknown journal discarded");
  }
  {
    Fixture f;
    f.interrupted("nested/identifier");
    write_atomic(f.root / "KB/1.VGA", f.edited.at("KB/1.VGA"));
    rejects([&] { recover(f.root); }, "nested journal identifier accepted");
    check(read_file(f.root / "KB/1.VGA") == f.edited.at("KB/1.VGA"),
          "invalid journal caused restoration");
    check(fs::exists(f.metadata() / "transaction.json"),
          "invalid journal discarded");
  }
}
void traversal_guards() {
  Fixture f;
  for (const auto &name :
       {std::string("../outside/sentinel"), std::string("KB/../M3D.MAN"),
        std::string(".openeddi/transaction.json"),
        utf8(f.outside / "sentinel")}) {
    rejects(
        [&] {
          save(f.root, {{name, "must not write"}},
               {{name, bmh::content::sha256("outside must survive")}});
        },
        "unsafe save path accepted");
    f.unchanged(f.original);
  }
  const std::string nul_name("M3D.MAN\0alias", 13);
  rejects(
      [&] {
        save(f.root, {{nul_name, "must not write"}},
             {{nul_name, bmh::content::sha256(f.original.at("M3D.MAN"))}});
      },
      "NUL path alias accepted");
  f.unchanged(f.original);
}
bool symlink_available(const Fixture &f) {
  std::error_code ec;
  fs::create_directory_symlink(f.outside, f.root / "symlink-probe", ec);
  if (ec)
    return false;
  fs::remove(f.root / "symlink-probe");
  return true;
}
void symlink_guards() {
  Fixture probe;
  if (!symlink_available(probe)) {
    std::cout << "symlink checks unavailable on this host\n";
    return;
  }
  for (const auto *name :
       {".openeddi", ".openeddi/backups", ".openeddi/staging"}) {
    Fixture f;
    fs::create_directories((f.root / name).parent_path());
    fs::create_directory_symlink(f.outside, f.root / name);
    rejects([&] { save(f.root, f.edited, f.expected); },
            "metadata directory symlink accepted");
    f.unchanged(f.original);
    check(std::distance(fs::directory_iterator(f.outside),
                        fs::directory_iterator()) == 1,
          "save escaped through metadata symlink");
  }
  {
    Fixture f;
    write_atomic(f.outside / "M3D.MAN", f.original.at("M3D.MAN"));
    fs::remove(f.root / "M3D.MAN");
    fs::create_symlink(f.outside / "M3D.MAN", f.root / "M3D.MAN");
    rejects([&] { save(f.root, f.edited, f.expected); },
            "source leaf symlink accepted");
    check(read_file(f.outside / "M3D.MAN") == f.original.at("M3D.MAN"),
          "source symlink wrote outside");
  }
  {
    Fixture f;
    write_atomic(f.outside / "1.VGA", f.original.at("KB/1.VGA"));
    fs::remove_all(f.root / "KB");
    fs::create_directory_symlink(f.outside, f.root / "KB");
    rejects([&] { save(f.root, f.edited, f.expected); },
            "source parent symlink accepted");
    check(read_file(f.outside / "1.VGA") == f.original.at("KB/1.VGA"),
          "source parent symlink wrote outside");
  }
  {
    Fixture f;
    fs::create_directories(f.metadata());
    write_atomic(f.outside / "journal.json", Json{{"state", "ready"}}.dump());
    fs::create_symlink(f.outside / "journal.json",
                       f.metadata() / "transaction.json");
    rejects([&] { recover(f.root); }, "journal symlink accepted");
    f.unchanged(f.original);
    check(read_file(f.outside / "journal.json") ==
              Json{{"state", "ready"}}.dump(),
          "outside journal modified");
  }
  {
    Fixture f;
    const auto result = save(f.root, f.edited, f.expected);
    const auto backup_file = f.metadata() / "backups" /
                             path(str(result, "backup")) / "original/M3D.MAN";
    write_atomic(f.outside / "original.MAN", f.original.at("M3D.MAN"));
    fs::remove(backup_file);
    fs::create_symlink(f.outside / "original.MAN", backup_file);
    rejects([&] { restore(f.root, str(result, "backup")); },
            "backup file symlink accepted despite matching hash");
    f.unchanged(f.edited);
  }
}
} // namespace
int main() {
  try {
    no_op_and_external_changes();
    backup_and_stage_failures();
    corrupted_restore();
    interrupted_recovery();
    traversal_guards();
    symlink_guards();
    std::cout << "transaction failure checks passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "transaction failure checks: " << e.what() << '\n';
    return 1;
  }
}
