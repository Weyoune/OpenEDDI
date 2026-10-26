// SPDX-License-Identifier: GPL-3.0-only
#include "legacy/document.hpp"
#include "editor/world.hpp"
#include "formats/binary.hpp"
#include "formats/executable_layout.hpp"
#include <algorithm>
#include <array>
#include <cstdint>

namespace bmh::editor::legacy {
namespace {
[[noreturn]] void fail(const std::string &detail) {
  throw Error("legacy_data", {{"detail", detail}});
}
uint32_t get(const std::string &s, size_t at, size_t n) {
  if (at > s.size() || n > s.size() - at)
    fail("Truncated legacy data");
  uint32_t v = 0;
  for (size_t i = 0; i < n; ++i)
    v |= uint32_t(uint8_t(s[at + i])) << (i * 8);
  return v;
}
void put(std::string &s, size_t at, uint32_t v, size_t n) {
  if (at > s.size() || n > s.size() - at)
    fail("Legacy field outside source table");
  for (size_t i = 0; i < n; ++i)
    s[at + i] = char(v >> (i * 8));
}
uint32_t le_scalar(const std::string &raw, uint32_t address, size_t count) {
  const auto le = size_t(get(raw, 0x3c, 4));
  if (le > raw.size() || raw.size() - le < 0x84 || raw.substr(le, 2) != "LE" ||
      get(raw, le + 2, 2) != 0)
    fail("Unsupported BMH DOS executable");
  const auto pages = get(raw, le + 0x14, 4), page_size = get(raw, le + 0x28, 4);
  const auto last_page = get(raw, le + 0x2c, 4),
             objects = get(raw, le + 0x44, 4);
  const auto object_table = uint64_t(le) + get(raw, le + 0x40, 4);
  const auto page_map = uint64_t(le) + get(raw, le + 0x48, 4);
  const auto data_pages = get(raw, le + 0x80, 4);
  if (!page_size || page_size > raw.size() || !pages || !objects ||
      objects > 64 || last_page > page_size ||
      object_table + uint64_t(objects) * 24 > raw.size() ||
      page_map + uint64_t(pages) * 4 > raw.size())
    fail("Unsupported LE object/page layout");
  uint32_t result = 0;
  for (size_t byte = 0; byte < count; ++byte) {
    const auto target = uint64_t(address) + byte;
    bool found = false;
    for (size_t i = 0; i < objects; ++i) {
      const auto at = size_t(object_table + i * 24);
      const auto size = get(raw, at, 4), base = get(raw, at + 4, 4);
      if (target < base || target - base >= size)
        continue;
      const auto within = target - base;
      const auto object_page = within / page_size;
      if (object_page >= get(raw, at + 16, 4))
        fail("LE scalar references an unbacked page");
      const auto logical = uint64_t(get(raw, at + 12, 4)) + object_page;
      if (!logical || logical > pages)
        fail("Invalid LE logical page");
      const auto entry = size_t(page_map + (logical - 1) * 4);
      const auto physical = (get(raw, entry, 1) << 16) |
                            (get(raw, entry + 1, 1) << 8) |
                            get(raw, entry + 2, 1);
      if (get(raw, entry + 3, 1) || !physical || physical > pages)
        fail("Unsupported LE data page");
      const auto inside = within % page_size;
      if (inside >= (physical == pages ? last_page : page_size))
        fail("Truncated LE scalar page");
      const auto offset =
          uint64_t(data_pages) + uint64_t(physical - 1) * page_size + inside;
      if (offset >= raw.size())
        fail("LE scalar outside executable");
      if (found)
        fail("Overlapping LE objects");
      result |= get(raw, size_t(offset), 1) << (byte * 8);
      found = true;
    }
    if (!found)
      fail("Missing original executable table");
  }
  return result;
}
struct Image {
  std::string bonus;
  std::array<std::string, 4> pools;
  std::string tail;
  // Offsets within the tail; all other bytes remain opaque.
  static constexpr size_t club_names = 0, player_names = 419 * 2;
  static constexpr size_t referee_names = player_names + 2574 * 2;
  static constexpr size_t stadium_names = referee_names + 100;
  static constexpr size_t badges = stadium_names + 168 * 2;
  static constexpr size_t refs = badges + 419 * 2 + 251 + 189;
  static constexpr size_t kits = refs + 50;
  static constexpr size_t squads = kits + 419 + 168;
  static constexpr size_t words = squads + (3096 - 4) * 2 + 108;
  explicit Image(const std::string &source) {
    get(source, 0, 4);
    size_t at = 8;
    auto take = [&](size_t n) {
      if (at > source.size() || n > source.size() - at)
        fail("Unsupported database counts or truncated file");
      auto s = source.substr(at, n);
      at += n;
      return s;
    };
    bonus = take(3096 + 50);
    for (size_t i = 0; i < 4; ++i)
      pools[i] = take(get(source, i * 2, 2));
    tail = take(words + 2574 * 12);
    if (at != source.size())
      fail("Unsupported database counts or trailing data");
    for (size_t i = 0; i < 4; ++i)
      if (pools[i].empty() || pools[i].back() != '\0')
        fail("Unterminated legacy name pool");
    for (size_t i = 0; i < 419; ++i)
      check_offset(0, get(tail, club_names + i * 2, 2));
    for (size_t i = 0; i < 2574; ++i)
      check_offset(1, get(tail, player_names + i * 2, 2));
    for (size_t i = 0; i < 50; ++i)
      check_offset(2, get(tail, referee_names + i * 2, 2));
    for (size_t i = 0; i < 168; ++i)
      check_offset(3, get(tail, stadium_names + i * 2, 2));
    std::set<uint32_t> seen;
    for (size_t i = 0; i < 2928; ++i) {
      auto p = get(tail, squads + i * 2, 2) & 4095;
      if (p == 4095)
        continue;
      if (p >= 2574 || !seen.insert(p).second)
        fail("Invalid or duplicate squad player");
    }
  }
  static size_t offsets(size_t pool) {
    return std::array<size_t, 4>{club_names, player_names, referee_names,
                                 stadium_names}
        .at(pool);
  }
  std::string name(size_t pool, size_t index) const {
    const auto at = get(tail, offsets(pool) + index * 2, 2);
    return pools[pool].substr(at, pools[pool].find('\0', at) - at);
  }
  void check_offset(size_t pool, size_t offset) const {
    if (offset >= pools[pool].size() ||
        pools[pool].find('\0', offset) == std::string::npos)
      fail("Invalid legacy name offset");
  }
  void rename(size_t pool, size_t index, const std::string &name) {
    auto &p = pools[pool];
    // Reuse complete existing strings; old offsets and old content never move.
    size_t at = 0, found = std::string::npos;
    while (at < p.size()) {
      auto end = p.find('\0', at);
      if (p.compare(at, end - at, name) == 0) {
        found = at;
        break;
      }
      at = end + 1;
    }
    if (found == std::string::npos) {
      if (p.size() + name.size() + 1 > 65535)
        fail("Legacy name pool exceeds 65535 bytes");
      found = p.size();
      p += name;
      p += '\0';
    }
    put(tail, offsets(pool) + index * 2, uint32_t(found), 2);
  }
  std::string serialize() const {
    std::string out(8, '\0');
    for (size_t i = 0; i < 4; ++i) {
      if (pools[i].size() > 65535)
        fail("Legacy name pool exceeds 65535 bytes");
      put(out, i * 2, uint32_t(pools[i].size()), 2);
    }
    out += bonus;
    for (const auto &pool : pools)
      out += pool;
    return out + tail;
  }
};
int integer(const Json &v, int lo, int hi, const std::string &field) {
  if (!v.is_number_integer() ||
      (v.is_number_unsigned()
           ? v.get<uint64_t>() > uint64_t(hi)
           : (v.get<int64_t>() < lo || v.get<int64_t>() > hi)))
    fail(field + " must be an integer from " + std::to_string(lo) + " to " +
         std::to_string(hi));
  return v.get<int>();
}
size_t index(const Json &row, const std::string &key, size_t count) {
  if (!row.contains("x-legacy") || !row["x-legacy"].contains(key))
    fail("Missing original record identity");
  return size_t(integer(row["x-legacy"][key], 0, int(count) - 1, key));
}
size_t capacity(size_t team) { return team < 90 ? 24 : 16; }
size_t first(size_t team) { return team < 90 ? team * 24 : team * 16 + 720; }
std::optional<fs::path> find_file(const fs::path &root,
                                  const std::string &name) {
  std::optional<fs::path> result;
  for (const auto &entry : fs::directory_iterator(root))
    if (entry.is_regular_file() &&
        folded(utf8(entry.path().filename())) == folded(name)) {
      if (result)
        fail("Ambiguous source filenames");
      result = entry.path();
    }
  return result;
}
const std::array<std::string, 7> skills{"teamplay", "speed",    "shooting",
                                        "passing",  "tackling", "free_kick",
                                        "corner"};
const std::array<std::string, 4> positions{"gk", "def", "mid", "fwd"};
const std::array<std::string, 4> writable_tables{"players", "clubs", "stadiums",
                                                 "staff"};
Json field(const Json &row, const std::string &p) {
  Json::json_pointer pointer(p);
  return row.contains(pointer) ? row.at(pointer) : Json();
}
std::string decode_text(const std::string &raw) {
  std::string out;
  for (unsigned char c : raw) {
    if (c >= 128) {
      out += char(0xc0 | (c >> 6));
      out += char(0x80 | (c & 63));
    } else {
      const char one[] = {char(c), 0};
      out += bmh::iso646de_to_utf8(one, 1);
    }
  }
  return out;
}
std::string player_name(const Json &row) {
  const auto &name = row.at("name");
  const auto first_name = str(name, "first"), last_name = str(name, "last");
  return first_name + (first_name.empty() || last_name.empty() ? "" : " ") +
         last_name;
}
} // namespace

std::string Document::encode_text(const std::string &value) {
  std::string result;
  for (size_t i = 0; i < value.size();) {
    uint32_t c = uint8_t(value[i++]);
    if (c >= 128) {
      if (c < 0xc2 || c > 0xc3 || i >= value.size())
        fail("Name contains a character unsupported by the game");
      auto next = uint8_t(value[i++]);
      if ((next & 0xc0) != 0x80)
        fail("Invalid UTF-8 name");
      c = ((c & 31) << 6) | (next & 63);
    }
    switch (c) {
    case 0xc4:
      c = '[';
      break;
    case 0xd6:
      c = '\\';
      break;
    case 0xdc:
      c = ']';
      break;
    case 0xe4:
      c = '{';
      break;
    case 0xf6:
      c = '|';
      break;
    case 0xfc:
      c = '}';
      break;
    case 0xdf:
      c = '~';
      break;
    case '[':
    case '\\':
    case ']':
    case '{':
    case '|':
    case '}':
    case '~':
      fail("Literal brackets, backslash, vertical bar and tilde are "
           "unavailable in the game's name encoding");
    default:
      if (c < 32 || (c >= 127 && c < 160))
        fail("Names cannot contain control characters");
    }
    result += char(c);
  }
  if (result.size() > 65534)
    fail("Legacy name is too long");
  return result;
}

Document Document::from_bytes(std::string bytes, bool english) {
  Image checked(bytes);
  Document d;
  d.source_ = std::move(bytes);
  d.english_ = english;
  d.file_ = english ? "M3E.MAN" : "M3D.MAN";
  return d;
}

int Document::strength(const Json &player) {
  const auto form = integer(field(player, "/attributes/form"), 0, 127, "form");
  const auto technique =
      integer(field(player, "/attributes/technique"), 0, 127, "technique");
  const auto condition =
      integer(field(player, "/attributes/condition"), 0, 127, "condition");
  return std::min((((form >> 3) + 94) * (technique + condition) / 100) / 2, 99);
}

Document::Document(const fs::path &input) {
  auto source = fs::absolute(input);
  root_ = fs::is_directory(source) ? source : source.parent_path();
  auto de = find_file(root_, "BMH.EXE"), en = find_file(root_, "FLTD.EXE");
  if (!de && !en && fs::is_directory(source)) {
    std::vector<fs::path> candidates;
    for (const auto &e : fs::directory_iterator(root_))
      if (e.is_directory() &&
          (find_file(e.path(), "BMH.EXE") || find_file(e.path(), "FLTD.EXE")))
        candidates.push_back(e.path());
    if (candidates.size() != 1)
      fail("Select one original BMH or Football Limited installation");
    root_ = candidates.front();
    de = find_file(root_, "BMH.EXE");
    en = find_file(root_, "FLTD.EXE");
  }
  if (!de && !en)
    fail("Original BMH.EXE or FLTD.EXE is required");
  if (de && en && fs::is_directory(source))
    fail("Select BMH.EXE or FLTD.EXE to choose the edition");
  english_ =
      en && (!de || (!fs::is_directory(source) && fs::equivalent(source, *en)));
  auto exe_path = english_ ? *en : *de;
  if (!fs::is_directory(source) && !fs::equivalent(source, exe_path))
    fail("Select an original game executable");
  const auto raw = bmh::editor::read_file(exe_path);
  if (raw.size() < 64 || raw.substr(0, 2) != "MZ")
    fail("Only original DOS executables are supported");
  std::array<uint32_t, 4> counts;
  if (english_) {
    if (raw.size() != 507925 ||
        raw.find("DOS extender Copyright 1991-1994 by Doug Huffman") ==
            std::string::npos)
      fail("Unsupported Football Limited X-32 executable");
    const size_t offsets[] = {0x6f368, 0x6f37c, 0x6f36c, 0x6f390};
    for (size_t i = 0; i < 4; ++i)
      counts[i] = get(raw, offsets[i] + 0x496d, 4);
    const unsigned char sizes[] = {20, 20, 20, 20, 10, 12, 12, 12, 12};
    for (size_t i = 0; i < 9; ++i)
      if (get(raw, 0x6f2e0 + 0x496d + i, 1) != sizes[i])
        fail("Unsupported English league layout");
  } else {
    if (raw.size() != 602527)
      fail("Unsupported BMH LE executable size");
    const uint32_t addresses[] = {
        bmh::layout::CareerSession_DataDirectory_RuntimeField,
        bmh::layout::CareerSession_DataDirectory_RuntimeField3,
        bmh::layout::CareerSession_DataDirectory_RuntimeField2,
        bmh::layout::CareerSession_DataDirectory_RuntimeField4};
    for (size_t i = 0; i < 4; ++i)
      counts[i] = le_scalar(raw, addresses[i], 4);
    const unsigned char sizes[] = {18, 18, 18, 18, 18, 12, 12, 12, 12};
    for (size_t i = 0; i < 9; ++i)
      if (le_scalar(raw, bmh::layout::LeagueSizes + uint32_t(i), 1) != sizes[i])
        fail("Unsupported German league layout");
  }
  if (counts != std::array<uint32_t, 4>{419, 2574, 168, 3096})
    fail("Unsupported database table counts");
  auto db = find_file(root_, english_ ? "M3E.MAN" : "M3D.MAN");
  if (!db)
    fail("Missing original master database");
  file_ = utf8(db->filename());
  source_ = bmh::editor::read_file(*db);
  Image checked(source_);
}

Json Document::metadata() const {
  return {{"edition", english_ ? "FLTD" : "BMH"},
          {"database", file_},
          {"writable", true},
          {"clubs", 419},
          {"players", 2574},
          {"referees", 50},
          {"stadiums", 168},
          {"squad_teams", 138},
          {"name_pool_byte_limit", 65535},
          {"encoding", "ISO-646-DE plus Latin-1"}};
}

void Document::remember(const World &world) {
  baseline_.clear();
  nations_.clear();
  teams_.clear();
  for (const auto &table : World::tables())
    if (table != "assets" && table != "image-edits")
      baseline_[table] = world.rows(table);
  for (const auto &row : world.rows("nations"))
    if (row.contains("x-legacy") &&
        row["x-legacy"].contains("nationality_index"))
      nations_[str(row, "id")] = index(row, "nationality_index", 62);
  for (const auto &row : world.rows("clubs"))
    teams_[str(row, "id")] = index(row, "team_index", 419);
}
void Document::capture(World &world) {
  Image image(source_);
  for (auto &row : world.rows("clubs")) {
    auto i = index(row, "team_index", 419);
    row["kit"]["selector"] = get(image.tail, Image::kits + i, 1);
    row["crest_selector"] = get(image.tail, Image::badges + i * 2, 2) & 511;
  }
  // The pools can contain shared names and retired strings. Entity identity is
  // the fixed offset-table slot, never the ordinal of a NUL-delimited string.
  world.rows("staff") = Json::array();
  for (size_t i = 0; i < 50; ++i) {
    auto property = get(image.tail, Image::refs + i, 1);
    world.rows("staff").push_back(
        {{"id", "staff:legacy-" + std::to_string(i)},
         {"name", {{"default", decode_text(image.name(2, i))}}},
         {"role", "referee"},
         {"attributes",
          {{"strictness", (property >> 3) & 3}, {"perception", property & 3}}},
         {"x-legacy", {{"referee_index", i}}}});
  }
  world.rows("stadiums") = Json::array();
  for (size_t i = 0; i < 168; ++i)
    world.rows("stadiums")
        .push_back({{"id", "stadium:legacy-" + std::to_string(i)},
                    {"name", {{"default", decode_text(image.name(3, i))}}},
                    {"x-legacy", {{"name_pool_index", i}}}});
  for (auto &club : world.rows("clubs")) {
    const auto i = index(club, "team_index", 419);
    if (i < 168) {
      auto &stadium = world.rows("stadiums")[i];
      stadium["owner"] = str(club, "id");
      club["stadium"] = str(stadium, "id");
    }
  }
  remember(world);
}

bool Document::editable(const std::string &table, const std::string &p) {
  if (table == "players") {
    if (p == "/name/first" || p == "/name/last" || p == "/age_at_start/years" ||
        p == "/positions/0" || p == "/nationalities/0" || p == "/team" ||
        p == "/squad_order" || p == "/x-legacy/squad_row")
      return true;
    if (p == "/attributes/form" || p == "/attributes/technique" ||
        p == "/attributes/condition" || p == "/attributes/bonus")
      return true;
    for (const auto &skill : skills)
      if (p == "/attributes/" + skill)
        return true;
  }
  if (table == "clubs")
    return p == "/name/default" || p == "/kit/selector" ||
           p == "/crest_selector";
  if (table == "stadiums")
    return p == "/name/default";
  if (table == "staff")
    return p == "/name/default" || p == "/attributes/strictness" ||
           p == "/attributes/perception";
  return false;
}

void Document::validate_change(const std::string &table,
                               const std::optional<Json> &before,
                               const std::optional<Json> &after) const {
  // These are transient projections owned and validated by Graphics. They do
  // not describe records in the master database or enter this writer's plan.
  if (table == "assets" || table == "image-edits")
    return;
  if (!before || !after)
    fail("Legacy database records cannot be added or deleted");
  for (const auto &change : Json::diff(*before, *after)) {
    auto p = change.at("path").get<std::string>();
    if (table == "players" && p == "/attributes/strength") {
      const bool inputs_changed = field(*before, "/attributes/form") !=
                                      field(*after, "/attributes/form") ||
                                  field(*before, "/attributes/technique") !=
                                      field(*after, "/attributes/technique") ||
                                  field(*before, "/attributes/condition") !=
                                      field(*after, "/attributes/condition");
      if (!inputs_changed ||
          integer(field(*after, p), 0, 99, p) != strength(*after))
        fail("Strength is read-only and must follow the classic attribute "
             "formula");
      continue;
    }
    if (!editable(table, p))
      fail("This field is read-only in the original database: " + p);
    auto v = field(*after, p);
    if (p == "/team" && v.is_null())
      continue;
    if (p.starts_with("/name/")) {
      if (!v.is_string())
        fail("Names must be text");
      encode_text(v.get<std::string>());
    } else if (p == "/positions/0") {
      if (!v.is_string() || std::find(positions.begin(), positions.end(),
                                      v.get<std::string>()) == positions.end())
        fail("Unsupported player position");
    } else if (p == "/nationalities/0") {
      if (!v.is_string() || !nations_.contains(v.get<std::string>()))
        fail("Unsupported nationality");
    } else if (p == "/team") {
      if (!v.is_string() || !teams_.contains(v.get<std::string>()) ||
          teams_.at(v.get<std::string>()) >= 138)
        fail("This team has no fixed legacy squad");
    } else if (p == "/age_at_start/years")
      integer(v, 16, 47, p);
    else if (p == "/kit/selector")
      integer(v, 0, 255, p);
    else if (p == "/crest_selector")
      integer(v, 0, 511, p);
    else if (p == "/squad_order" || p == "/x-legacy/squad_row") {
      int cap = 24;
      auto team = str(*after, "team");
      if (!team.empty() && teams_.contains(team))
        cap = int(capacity(teams_.at(team)));
      integer(v, -1, cap - 1, p);
    } else if (p == "/attributes/form" || p == "/attributes/technique" ||
               p == "/attributes/condition" || p == "/attributes/bonus")
      integer(v, 0, 127, p);
    else
      integer(v, 0, 3, p);
  }
  if (table == "players") {
    if (!after->contains("positions") || !(*after)["positions"].is_array() ||
        (*after)["positions"].size() != 1 ||
        !after->contains("nationalities") ||
        !(*after)["nationalities"].is_array() ||
        (*after)["nationalities"].size() != 1)
      fail("The original player record has one position and one nationality");
    if (field(*before, "/name/first") != field(*after, "/name/first") ||
        field(*before, "/name/last") != field(*after, "/name/last"))
      encode_text(player_name(*after));
  }
}

std::map<std::string, std::string> Document::prepare(const World &world) const {
  if (baseline_.empty())
    fail("Legacy document has no source projection");
  Image image(source_);
  for (const auto &[table, original] : baseline_) {
    const auto &current = world.rows(table);
    if (current.size() != original.size())
      fail("Legacy database record counts are fixed");
    for (size_t i = 0; i < current.size(); ++i)
      validate_change(table, original[i], current[i]);
  }
  auto patch_bits = [&](size_t at, uint32_t mask, uint32_t value) {
    put(image.tail, at, (get(image.tail, at, 4) & ~mask) | (value & mask), 4);
  };
  for (const auto &table : writable_tables) {
    const auto &rows = world.rows(table), &old = baseline_.at(table);
    for (size_t r = 0; r < rows.size(); ++r) {
      const auto &row = rows[r], &before = old[r];
      if (row == before)
        continue;
      auto changed = [&](const std::string &p) {
        return field(row, p) != field(before, p);
      };
      if (table == "players") {
        auto i = index(row, "player_index", 2574);
        if (changed("/name/first") || changed("/name/last"))
          image.rename(1, i, encode_text(player_name(row)));
        size_t w1 = Image::words + i * 4, w2 = w1 + 2574 * 4,
               w3 = w2 + 2574 * 4;
        for (size_t a = 0; a < 3; ++a) {
          std::string key =
              std::array<std::string, 3>{"form", "technique", "condition"}[a];
          if (changed("/attributes/" + key))
            patch_bits(w1, 127u << (a * 7),
                       uint32_t(row["attributes"][key].get<int>()) << (a * 7));
        }
        for (size_t s = 0; s < skills.size(); ++s)
          if (changed("/attributes/" + skills[s]))
            patch_bits(w2, 3u << (s * 2),
                       uint32_t(row["attributes"][skills[s]].get<int>())
                           << (s * 2));
        if (changed("/age_at_start/years"))
          patch_bits(w3, 31u << 21,
                     uint32_t(row["age_at_start"]["years"].get<int>() - 16)
                         << 21);
        if (changed("/positions/0")) {
          auto p = row["positions"][0].get<std::string>();
          patch_bits(w3, 3,
                     uint32_t(std::find(positions.begin(), positions.end(), p) -
                              positions.begin()));
        }
        if (changed("/attributes/bonus"))
          image.bonus[i] = char((uint8_t(image.bonus[i]) & 128) |
                                row["attributes"]["bonus"].get<int>());
        if (changed("/nationalities/0")) {
          auto n =
              uint32_t(nations_.at(row["nationalities"][0].get<std::string>()));
          patch_bits(w2, 31u << 27, (n & 31) << 27);
          image.bonus[i] =
              char((uint8_t(image.bonus[i]) & 127) | ((n & 32) << 2));
        }
      } else if (table == "clubs") {
        auto i = index(row, "team_index", 419);
        if (changed("/name/default"))
          image.rename(0, i,
                       encode_text(row["name"]["default"].get<std::string>()));
        if (changed("/kit/selector"))
          put(image.tail, Image::kits + i,
              row["kit"]["selector"].get<uint32_t>(), 1);
        if (changed("/crest_selector"))
          put(image.tail, Image::badges + i * 2,
              (get(image.tail, Image::badges + i * 2, 2) & 0xfe00) |
                  row["crest_selector"].get<uint32_t>(),
              2);
      } else {
        auto pool = table == "staff" ? 2 : 3;
        auto i =
            index(row, table == "staff" ? "referee_index" : "name_pool_index",
                  table == "staff" ? 50 : 168);
        if (changed("/name/default"))
          image.rename(size_t(pool), i,
                       encode_text(row["name"]["default"].get<std::string>()));
        if (table == "staff") {
          auto at = Image::refs + i;
          auto property = get(image.tail, at, 1);
          if (changed("/attributes/strictness"))
            property = (property & ~24u) |
                       (row["attributes"]["strictness"].get<uint32_t>() << 3);
          if (changed("/attributes/perception"))
            property = (property & ~3u) |
                       row["attributes"]["perception"].get<uint32_t>();
          put(image.tail, at, property, 1);
        }
      }
    }
  }
  // Clear moving players first so an atomic swap is possible. Slot flags stay
  // at their original physical addresses, including the flags on empty slots.
  struct Move {
    size_t player;
    int team, row;
    bool explicit_row;
  };
  std::vector<Move> moves;
  const auto &players = world.rows("players"),
             &old_players = baseline_.at("players");
  for (size_t r = 0; r < players.size(); ++r) {
    const auto &row = players[r], &old = old_players[r];
    bool team_changed = field(row, "/team") != field(old, "/team");
    bool order_changed =
        field(row, "/squad_order") != field(old, "/squad_order");
    bool legacy_changed =
        field(row, "/x-legacy/squad_row") != field(old, "/x-legacy/squad_row");
    bool row_changed = order_changed || legacy_changed;
    if (!team_changed && !row_changed)
      continue;
    size_t p = index(row, "player_index", 2574);
    auto team = str(row, "team");
    int t = team.empty() ? -1 : int(teams_.at(team));
    if (t >= 138)
      fail("This team has no fixed legacy squad");
    if (order_changed && legacy_changed &&
        field(row, "/squad_order") != field(row, "/x-legacy/squad_row"))
      fail("Conflicting squad row fields");
    const auto slot_value =
        field(row, order_changed ? "/squad_order" : "/x-legacy/squad_row");
    int slot = slot_value.is_number_integer() ? slot_value.get<int>() : -1;
    if (row_changed && slot >= 0 && t >= 0 && slot >= int(capacity(size_t(t))))
      fail("Squad row exceeds this team's fixed capacity");
    moves.push_back({p, t, slot, row_changed && slot >= 0});
    for (size_t s = 0; s < 2928; ++s) {
      auto at = Image::squads + s * 2;
      auto word = get(image.tail, at, 2);
      if ((word & 4095) == p)
        put(image.tail, at, (word & 0xf000) | 4095, 2);
    }
  }
  std::stable_sort(moves.begin(), moves.end(),
                   [](const Move &a, const Move &b) {
                     return a.explicit_row > b.explicit_row;
                   });
  for (const auto &m : moves) {
    if (m.team < 0)
      continue;
    size_t start = first(size_t(m.team)), count = capacity(size_t(m.team)),
           slot = count;
    if (m.explicit_row)
      slot = size_t(m.row);
    else
      for (size_t s = 0; s < count; ++s)
        if ((get(image.tail, Image::squads + (start + s) * 2, 2) & 4095) ==
            4095) {
          slot = s;
          break;
        }
    if (slot >= count)
      fail(
          "Squad capacity exceeded (24 domestic or 16 foreign-league players)");
    auto at = Image::squads + (start + slot) * 2;
    auto word = get(image.tail, at, 2);
    if ((word & 4095) != 4095)
      fail("Two players occupy the same squad row");
    put(image.tail, at, (word & 0xf000) | uint32_t(m.player), 2);
  }
  auto result = image.serialize();
  Image verified(result);
  if (result == source_)
    return {};
  return {{file_, std::move(result)}};
}

Json Document::roster_plan(const World &world) const {
  const auto files = prepare(world);
  Image image(files.contains(file_) ? files.at(file_) : source_);
  Json result = Json::object();
  for (size_t team = 0; team < 138; ++team) {
    const size_t first = team < 90 ? team * 24 : team * 16 + 720;
    const size_t count = team < 90 ? 24 : 16;
    for (size_t row = 0; row < count; ++row) {
      const auto player =
          get(image.tail, Image::squads + (first + row) * 2, 2) & 4095;
      if (player != 4095)
        result[std::to_string(player)] = int(row);
    }
  }
  return result;
}
void Document::mark_saved(const World &world) {
  auto files = prepare(world);
  if (auto it = files.find(file_); it != files.end())
    source_ = it->second;
  remember(world);
}
} // namespace bmh::editor::legacy
