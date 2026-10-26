// SPDX-License-Identifier: GPL-3.0-only
#include "legacy/transaction.hpp"
#include "content/digest.hpp"
#include <algorithm>
#include <chrono>
#include <fstream>
#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif
namespace bmh::editor::legacy {
namespace {
std::string hash(const fs::path &p) { return content::sha256(read_file(p)); }
fs::path safe(const fs::path &root, const std::string &name) {
  auto p = path(name);
  if (!safe_name(name) || p.empty() || p.is_absolute())
    throw Error("unsafe_path");
  fs::path cur = root;
  for (const auto &part : p) {
    if (part == ".." || part == "." || part == ".openeddi")
      throw Error("unsafe_path");
    cur /= part;
    if (fs::is_symlink(fs::symlink_status(cur)))
      throw Error("unsafe_path");
  }
  return cur;
}
void durable(const fs::path &p) {
#ifdef _WIN32
  HANDLE h =
      CreateFileW(p.c_str(), GENERIC_WRITE,
                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                  nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE)
    throw Error("save_failed");
  const bool ok = FlushFileBuffers(h);
  CloseHandle(h);
  if (!ok)
    throw Error("save_failed");
#else
  int h = ::open(p.c_str(), O_RDONLY);
  if (h < 0)
    throw Error("save_failed");
  const int ok = fsync(h);
  close(h);
  if (ok)
    throw Error("save_failed");
  int directory = ::open(p.parent_path().c_str(), O_RDONLY | O_DIRECTORY);
  if (directory < 0)
    throw Error("save_failed");
  const int synced = fsync(directory);
  close(directory);
  if (synced)
    throw Error("save_failed");
#endif
}
void write(const fs::path &p, const std::string &data) {
  fs::create_directories(p.parent_path());
  write_atomic(p, data);
  durable(p);
}
fs::path control(const fs::path &root) {
  auto p = root / ".openeddi";
  for (const auto &d :
       {p, p / "backups", p / "staging", p / "transaction.json", p / "lock"})
    if (fs::is_symlink(fs::symlink_status(d)))
      throw Error("unsafe_path");
  return p;
}
struct Lock {
#ifdef _WIN32
  HANDLE handle = INVALID_HANDLE_VALUE;
#else
  int handle = -1;
#endif
  explicit Lock(const fs::path &root) {
    auto dir = control(root);
    fs::create_directories(dir);
#ifdef _WIN32
    handle =
        CreateFileW((dir / "lock").c_str(), GENERIC_READ | GENERIC_WRITE, 0,
                    nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
      throw Error("installation_busy");
#else
    handle =
        ::open((dir / "lock").c_str(), O_CREAT | O_RDWR | O_NOFOLLOW, 0600);
    if (handle < 0)
      throw Error("save_failed");
    if (flock(handle, LOCK_EX | LOCK_NB)) {
      close(handle);
      handle = -1;
      throw Error("installation_busy");
    }
#endif
  }
  ~Lock() {
#ifdef _WIN32
    if (handle != INVALID_HANDLE_VALUE)
      CloseHandle(handle);
#else
    if (handle >= 0) {
      flock(handle, LOCK_UN);
      close(handle);
    }
#endif
  }
};
Json manifest(const fs::path &root, const std::string &id) {
  if (!safe_name(id) || id.find('/') != id.npos || id.find('\\') != id.npos)
    throw Error("unsafe_path");
  auto base = control(root) / "backups" / path(id);
  if (fs::is_symlink(fs::symlink_status(base)) ||
      fs::is_symlink(fs::symlink_status(base / "manifest.json")))
    throw Error("unsafe_path");
  return Json::parse(read_file(base / "manifest.json"));
}
void rollback(const fs::path &root, const Json &journal) {
  const auto id = str(journal, "id");
  if (!safe_name(id) || id.find('/') != id.npos || id.find('\\') != id.npos)
    throw Error("unsafe_path");
  const auto base = control(root) / "backups" / path(id) / "original";
  if (fs::is_symlink(fs::symlink_status(base.parent_path())) ||
      fs::is_symlink(fs::symlink_status(base)))
    throw Error("unsafe_path");
  // Validate every backup before restoring any original.
  for (const auto &f : journal.at("files")) {
    const auto name = str(f, "path");
    const auto current = hash(safe(root, name));
    if (current != str(f, "before") && current != str(f, "after"))
      throw Error("recovery_conflict");
    if (hash(safe(base, name)) != str(f, "before"))
      throw Error("backup_corrupt");
  }
  for (const auto &f : journal.at("files"))
    write(safe(root, str(f, "path")), read_file(safe(base, str(f, "path"))));
}
} // namespace
Fingerprints snapshot(const fs::path &root) {
  Fingerprints out;
  for (auto it = fs::recursive_directory_iterator(root);
       it != fs::recursive_directory_iterator(); ++it) {
    if (it->path().filename() == ".openeddi") {
      it.disable_recursion_pending();
      continue;
    }
    if (fs::is_symlink(it->symlink_status())) {
      if (it->is_directory())
        it.disable_recursion_pending();
      continue;
    }
    const auto ext = folded(utf8(it->path().extension()));
    if (it->is_regular_file() &&
        (ext == ".exe" || ext == ".man" || ext == ".vga" || ext == ".col" ||
         ext == ".men" || ext == ".dat" || ext == ".blk" || ext == ".fnt"))
      out[utf8(fs::relative(it->path(), root))] = hash(it->path());
  }
  return out;
}
void recover_unlocked(const fs::path &root) {
  const auto file = control(root) / "transaction.json";
  if (!fs::exists(file))
    return;
  auto j = Json::parse(read_file(file));
  const auto state = str(j, "state");
  if (state == "writing") {
    for (const auto &f : j.at("files")) {
      const auto actual = hash(safe(root, str(f, "path")));
      if (actual != str(f, "before") && actual != str(f, "after"))
        throw Error("recovery_conflict");
    }
    rollback(root, j);
  }
  if (state != "writing" && state != "complete" && state != "ready")
    throw Error("recovery_required");
  fs::remove(file);
}
void recover(const fs::path &root) {
  if (!fs::exists(control(root) / "transaction.json"))
    return;
  Lock lock(root);
  recover_unlocked(root);
}
Json save_unlocked(const fs::path &root, const Files &incoming,
                   const Fingerprints &expected) {
  recover_unlocked(root);
  Files changed;
  for (const auto &[name, digest] : expected) {
    const auto file = safe(root, name);
    if (!fs::is_regular_file(file) || hash(file) != digest)
      throw Error("source_changed", {{"path", name}});
  }
  for (const auto &[name, data] : incoming) {
    auto it = expected.find(name);
    const auto file = safe(root, name);
    if (it == expected.end() || !fs::is_regular_file(file) ||
        hash(file) != it->second)
      throw Error("source_changed", {{"path", name}});
    if (content::sha256(data) != it->second)
      changed[name] = data;
  }
  if (changed.empty())
    return {{"changed", false}};
  for (const auto &[name, data] : changed) {
    auto file = safe(root, name);
    auto it = expected.find(name);
    if (it == expected.end() || !fs::is_regular_file(file) ||
        hash(file) != it->second)
      throw Error("source_changed", {{"path", name}});
    if (data.empty())
      throw Error("validation_failed");
  }
  const auto id = new_id(
      std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count()) +
      "-");
  const auto meta = control(root);
  const auto backup = meta / "backups" / path(id);
  const auto stage = meta / "staging" / path(id);
  Json j{{"id", id}, {"state", "ready"}, {"files", Json::array()}};
  for (const auto &[name, data] : changed) {
    const auto original = read_file(safe(root, name));
    write(backup / "original" / path(name), original);
    write(stage / path(name), data);
    const auto before = content::sha256(original),
               after = content::sha256(data);
    if (hash(backup / "original" / path(name)) != before ||
        hash(stage / path(name)) != after)
      throw Error("backup_corrupt");
    j["files"].push_back(
        {{"path", name}, {"before", before}, {"after", after}});
  }
  write(backup / "manifest.json", j.dump(2));
  // Recheck after staging, before the first replacement.
  for (const auto &[name, _] : changed)
    if (hash(safe(root, name)) != expected.at(name))
      throw Error("source_changed");
  j["state"] = "writing";
  write(meta / "transaction.json", j.dump(2));
  try {
    for (const auto &[name, data] : changed) {
      if (hash(safe(root, name)) != expected.at(name))
        throw Error("source_changed");
      write(safe(root, name), read_file(stage / path(name)));
    }
    for (const auto &f : j["files"])
      if (hash(safe(root, str(f, "path"))) != str(f, "after"))
        throw Error("source_changed");
    j["state"] = "complete";
    write(meta / "transaction.json", j.dump(2));
  } catch (...) {
    try {
      rollback(root, j);
      fs::remove(meta / "transaction.json");
    } catch (...) {
      throw Error("recovery_required");
    }
    throw;
  }
  fs::remove(meta / "transaction.json");
  fs::remove_all(stage);
  return {{"changed", true}, {"backup", id}, {"files", j["files"]}};
}
Json save(const fs::path &root, const Files &changed,
          const Fingerprints &expected) {
  if (changed.empty())
    return {{"changed", false}};
  Lock lock(root);
  return save_unlocked(root, changed, expected);
}
Json backups(const fs::path &root) {
  Json out = Json::array();
  const auto base = control(root) / "backups";
  if (!fs::exists(base))
    return out;
  for (const auto &d : fs::directory_iterator(base))
    if (d.is_directory() && !fs::is_symlink(d.symlink_status()) &&
        fs::exists(d.path() / "manifest.json"))
      out.push_back(Json::parse(read_file(d.path() / "manifest.json")));
  std::sort(out.begin(), out.end(), [](const Json &a, const Json &b) {
    return str(a, "id") > str(b, "id");
  });
  return out;
}
Json restore(const fs::path &root, const std::string &id) {
  Lock lock(root);
  recover_unlocked(root);
  auto j = manifest(root, id);
  Files changes;
  Fingerprints expected;
  const auto base = control(root) / "backups" / path(id) / "original";
  if (fs::is_symlink(fs::symlink_status(base)))
    throw Error("unsafe_path");
  for (const auto &f : j.at("files")) {
    auto name = str(f, "path");
    auto original = read_file(safe(base, name));
    if (content::sha256(original) != str(f, "before"))
      throw Error("backup_corrupt");
    auto target = safe(root, name);
    changes[name] = original;
    expected[name] = hash(target);
  }
  auto result = save_unlocked(root, changes, expected);
  result["restored"] = id;
  return result;
}
} // namespace bmh::editor::legacy
