// SPDX-License-Identifier: GPL-3.0-only
#include "editor/world.hpp"
#include "legacy/document.hpp"
#include <array>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iostream>

using namespace bmh::editor;
using bmh::editor::legacy::Document;

namespace {
void check(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
}
void rejected(const std::function<void()> &fn, const char *message) {
  try {
    fn();
  } catch (const Error &) {
    return;
  }
  throw std::runtime_error(message);
}
uint32_t word(const std::string &bytes, size_t at, size_t size = 2) {
  uint32_t out = 0;
  for (size_t i = 0; i < size; ++i)
    out |= uint32_t(uint8_t(bytes.at(at + i))) << (i * 8);
  return out;
}
void put(std::string &bytes, size_t at, uint32_t value, size_t size = 2) {
  for (size_t i = 0; i < size; ++i)
    bytes.at(at + i) = char(value >> (i * 8));
}
constexpr size_t player_offsets = 419 * 2;
constexpr size_t referee_offsets = player_offsets + 2574 * 2;
constexpr size_t stadium_offsets = referee_offsets + 100;
constexpr size_t crest_words = stadium_offsets + 168 * 2;
constexpr size_t referee_properties = crest_words + 419 * 2 + 251 + 189;
constexpr size_t kit_bytes = referee_properties + 50;
constexpr size_t squad_words = kit_bytes + 419 + 168;
constexpr size_t player_words = squad_words + (3096 - 4) * 2 + 108;
constexpr uint32_t initial_form = 0xeabcc59a, initial_skills = 0x092acdef,
                   initial_identity = 0xd5caed51;
const std::array<std::string, 7> skill_names{
    "teamplay", "speed",     "shooting", "passing",
    "tackling", "free_kick", "corner"};
size_t tail_at(const std::string &bytes) {
  size_t out = 8 + 3096 + 50;
  for (size_t i = 0; i < 4; ++i)
    out += word(bytes, i * 2);
  return out;
}
size_t pool_at(const std::string &bytes, size_t pool) {
  size_t out = 8 + 3096 + 50;
  for (size_t i = 0; i < pool; ++i)
    out += word(bytes, i * 2);
  return out;
}
std::string pool(const std::string &bytes, size_t p) {
  return bytes.substr(pool_at(bytes, p), word(bytes, p * 2));
}
std::string name(const std::string &bytes, size_t p, size_t offsets,
                 size_t index) {
  const auto start =
      pool_at(bytes, p) + word(bytes, tail_at(bytes) + offsets + index * 2);
  return bytes.substr(start, bytes.find('\0', start) - start);
}
std::string source_bytes(size_t extra_club_bytes = 0) {
  std::array<std::string, 4> pools{std::string("Klub\0", 5),
                                   std::string("Alt\0", 4),
                                   std::string("Ref A\0Ref B\0", 12),
                                   std::string("Ground A\0Ground B\0", 18)};
  if (extra_club_bytes)
    pools[0] += std::string(extra_club_bytes - 1, 'Q') + '\0';
  std::string out(8 + 3096 + 50, char(0x9a));
  for (size_t p = 0; p < 4; ++p) {
    put(out, p * 2, uint32_t(pools[p].size()));
    out += pools[p];
  }
  const auto tail = out.size();
  out.resize(tail + player_words + 2574 * 12);
  for (size_t at = tail; at < out.size(); ++at)
    out[at] = char((at * 17 + 83) & 255);
  for (size_t i = 0; i < 419; ++i)
    put(out, tail + i * 2, 0);
  for (size_t i = 0; i < 2574; ++i) {
    put(out, tail + player_offsets + i * 2, 0);
    put(out, tail + player_words + i * 4, initial_form, 4);
    put(out, tail + player_words + (2574 + i) * 4, initial_skills, 4);
    put(out, tail + player_words + (2574 * 2 + i) * 4, initial_identity, 4);
  }
  for (size_t i = 0; i < 50; ++i)
    put(out, tail + referee_offsets + i * 2, (i % 2) * 6);
  for (size_t i = 0; i < 168; ++i)
    put(out, tail + stadium_offsets + i * 2, (i % 2) * 9);
  for (size_t i = 0; i < 2928; ++i)
    put(out, tail + squad_words + i * 2, uint32_t((i % 16) << 12) | 4095);
  put(out, tail + squad_words, 0xa000);
  put(out, tail + squad_words + 2, 0xb001);
  put(out, tail + squad_words + 2160 * 2, 0xc002);
  return out;
}
World projection() {
  auto world = World::blank("Synthetic legacy database");
  world.rows("nations") = Json::array(
      {{{"id", "nation:one"}, {"x-legacy", {{"nationality_index", 1}}}},
       {{"id", "nation:thirtythree"},
        {"x-legacy", {{"nationality_index", 33}}}},
       {{"id", "nation:thirtyfour"},
        {"x-legacy", {{"nationality_index", 34}}}}});
  for (auto i : {0, 90, 138})
    world.rows("clubs").push_back({{"id", "club:" + std::to_string(i)},
                                   {"name", {{"default", "Klub"}}},
                                   {"x-legacy", {{"team_index", i}}}});
  for (int i = 0; i < 44; ++i) {
    Json attributes{{"form", initial_form & 127},
                    {"technique", (initial_form >> 7) & 127},
                    {"condition", (initial_form >> 14) & 127},
                    {"bonus", 26},
                    {"strength", 80}};
    for (size_t s = 0; s < skill_names.size(); ++s)
      attributes[skill_names[s]] = (initial_skills >> (s * 2)) & 3;
    Json row{
        {"id", "player:" + std::to_string(i)},
        {"name", {{"first", ""}, {"last", "Alt"}}},
        {"positions", Json::array({"def"})},
        {"nationalities", Json::array({"nation:thirtythree"})},
        {"age_at_start", {{"years", ((initial_identity >> 21) & 31) + 16}}},
        {"attributes", attributes},
        {"squad_order", i < 2    ? i
                        : i == 2 ? 0
                                 : -1},
        {"x-legacy",
         {{"player_index", i},
          {"squad_row", i < 2    ? i
                        : i == 2 ? 0
                                 : -1}}}};
    if (i < 3)
      row["team"] = i < 2 ? "club:0" : "club:90";
    world.rows("players").push_back(row);
  }
  return world;
}
std::string prepared(const Document &doc, const World &world,
                     bool english = false) {
  const auto files = doc.prepare(world);
  check(files.size() == 1, "a changed database must produce exactly one file");
  return files.at(english ? "M3E.MAN" : "M3D.MAN");
}
void no_op_and_masks() {
  const auto source = source_bytes();
  auto doc = Document::from_bytes(source);
  auto world = projection();
  doc.capture(world);
  check(world.rows("staff").size() == 50 &&
            world.rows("stadiums").size() == 168,
        "fixed referenced-name records");
  check(world.rows("staff")[1]["name"]["default"] == "Ref B",
        "referee offset-table names");
  check(world.rows("stadiums")[1]["name"]["default"] == "Ground B",
        "stadium offset-table names");
  check(doc.prepare(world).empty(),
        "untouched database must be byte identical");
  auto &player = world.rows("players")[0];
  player["attributes"]["form"] = 1;
  player["attributes"]["technique"] = 2;
  player["attributes"]["condition"] = 3;
  player["attributes"]["strength"] = Document::strength(player);
  player["attributes"]["bonus"] = 127;
  player["nationalities"][0] = "nation:thirtyfour";
  player["positions"][0] = "fwd";
  player["age_at_start"]["years"] = 47;
  uint32_t skill_bits = 0;
  for (size_t s = 0; s < skill_names.size(); ++s) {
    player["attributes"][skill_names[s]] = s % 4;
    skill_bits |= uint32_t(s % 4) << (s * 2);
  }
  world.rows("clubs")[0]["kit"]["selector"] = 247;
  world.rows("clubs")[0]["crest_selector"] = 400;
  world.rows("staff")[0]["attributes"]["strictness"] = 2;
  world.rows("staff")[0]["attributes"]["perception"] = 3;
  auto expected = source;
  const auto tail = tail_at(source);
  put(expected, tail + player_words,
      (initial_form & 0xffe00000) | 1 | (2 << 7) | (3 << 14), 4);
  put(expected, tail + player_words + 2574 * 4,
      (initial_skills & 0x07ffc000) | skill_bits | (2u << 27), 4);
  put(expected, tail + player_words + 2574 * 8,
      (initial_identity & ~(3u | (31u << 21))) | 3u | (31u << 21), 4);
  expected[8] = char(255);
  expected[tail + kit_bytes] = char(247);
  put(expected, tail + crest_words,
      (word(source, tail + crest_words) & 0xfe00) | 400);
  expected[tail + referee_properties] = char(
      (word(source, tail + referee_properties, 1) & ~27u) | (2u << 3) | 3u);
  check(prepared(doc, world) == expected,
        "all unedited bits, tables and bytes must survive field edits");
  doc.mark_saved(world);
  check(doc.prepare(world).empty(),
        "mark_saved establishes a byte-identical new baseline");
  player["nationalities"][0] = "nation:one";
  auto nationality = prepared(doc, world);
  check(uint8_t(nationality[8]) == 127,
        "nationality high bit is separate from seven bonus bits");
  auto english = Document::from_bytes(source, true);
  auto english_world = projection();
  english.capture(english_world);
  english_world.rows("clubs")[0]["kit"]["selector"] = 77;
  check(!prepared(english, english_world, true).empty(),
        "English source retains its original database filename");
}
void names_and_pool_limits() {
  check(Document::encode_text("ÄÖÜäöüßé") == std::string("[\\]{|}~\xe9", 8),
        "ISO-646-DE and Latin-1 encoding");
  for (const auto &invalid :
       {std::string("["), std::string("\\"), std::string("~"),
        std::string("A\0B", 3), std::string("\xc2\x80", 2),
        std::string("\xc0\xaf", 2), std::string("\xe2\x82\xac", 3)})
    rejected([&] { Document::encode_text(invalid); },
             "unsupported text must fail");
  auto source = source_bytes();
  auto doc = Document::from_bytes(source);
  auto world = projection();
  doc.capture(world);
  world.rows("players")[0]["name"]["first"] = "Jörg";
  world.rows("players")[0]["name"]["last"] = "Groß";
  world.rows("clubs")[0]["name"]["default"] = "Köln";
  world.rows("staff")[0]["name"]["default"] = "Referee renamed";
  world.rows("stadiums")[0]["name"]["default"] = "A much longer stadium";
  const auto result = prepared(doc, world);
  check(name(result, 1, player_offsets, 0) == "J|rg Gro~",
        "first and last names form one legacy name");
  check(name(result, 0, 0, 0) == "K|ln",
        "club name uses the original encoding");
  check(name(result, 2, referee_offsets, 0) == "Referee renamed",
        "referee offset retargets appended string");
  check(name(result, 3, stadium_offsets, 0) == "A much longer stadium",
        "stadium offset retargets appended string");
  for (size_t p = 0; p < 4; ++p)
    check(pool(result, p).starts_with(pool(source, p)),
          "existing name bytes and offsets never move");
  auto expected_tail = source.substr(tail_at(source));
  const auto new_tail = result.substr(tail_at(result));
  for (const auto offsets :
       {size_t(0), player_offsets, referee_offsets, stadium_offsets})
    put(expected_tail, offsets, word(new_tail, offsets));
  check(expected_tail == new_tail,
        "name edits update only their own source offset words");
  auto reopened = Document::from_bytes(result);
  auto projected = projection();
  reopened.capture(projected);
  check(projected.rows("staff")[0]["name"]["default"] == "Referee renamed",
        "renamed referee reopens through the offset table");
  check(projected.rows("stadiums")[0]["name"]["default"] ==
            "A much longer stadium",
        "renamed stadium reopens through the offset table");
  check(reopened.prepare(projected).empty(),
        "reopened appended pools are unchanged");
  // A 65,535-byte pool is valid; an append crossing that u16 boundary is not.
  auto full = Document::from_bytes(source_bytes(65530));
  auto full_world = projection();
  full.capture(full_world);
  full_world.rows("clubs")[0]["name"]["default"] = "New";
  rejected([&] { full.prepare(full_world); },
           "overflowing a pool must fail before writing");
  full_world.rows("clubs")[0]["name"]["default"] = "";
  rejected([&] { full.prepare(full_world); },
           "an empty name still needs a new NUL byte in a full pool");
  auto edge = Document::from_bytes(source_bytes(65528));
  auto edge_world = projection();
  edge.capture(edge_world);
  edge_world.rows("clubs")[0]["name"]["default"] = "X";
  check(word(prepared(edge, edge_world), 0) == 65535,
        "the exact pool size boundary is accepted");
}
void readonly_and_ranges() {
  auto doc = Document::from_bytes(source_bytes());
  auto world = projection();
  doc.capture(world);
  const auto before = world.rows("players")[0];
  auto rated = before;
  rated["attributes"]["form"] = 0;
  rated["attributes"]["technique"] = 50;
  rated["attributes"]["condition"] = 50;
  check(Document::strength(rated) == 47,
        "classic strength uses the source integer formula");
  rated["attributes"]["form"] = 16;
  check(Document::strength(rated) == 48,
        "form changes the classic strength multiplier");
  rated["attributes"]["form"] = 127;
  rated["attributes"]["technique"] = 127;
  rated["attributes"]["condition"] = 127;
  check(Document::strength(rated) == 99,
        "display strength is capped at ninety-nine");
  auto after = before;
  after["attributes"]["strength"] = 99;
  rejected([&] { doc.validate_change("players", before, after); },
           "derived strength is read-only");
  check(!Document::editable("players", "/attributes/strength"),
        "derived strength is not an editable control");
  after = before;
  after["attributes"]["form"] = 0;
  after["attributes"]["strength"] = Document::strength(after);
  doc.validate_change("players", before, after);
  after["attributes"]["strength"] = (Document::strength(after) + 1) % 100;
  rejected([&] { doc.validate_change("players", before, after); },
           "derived strength cannot be forged during an attribute edit");
  rejected([&] { doc.validate_change("players", before, std::nullopt); },
           "source records cannot be deleted");
  rejected([&] { doc.validate_change("players", std::nullopt, before); },
           "source records cannot be inserted");
  for (const auto &[pointer, value] : std::vector<std::pair<std::string, Json>>{
           {"/age_at_start/years", 48},
           {"/age_at_start/years", 15},
           {"/attributes/form", 128},
           {"/attributes/bonus", -1},
           {"/attributes/speed", 4},
           {"/attributes/corner", 1.5},
           {"/positions/0", "wing"},
           {"/nationalities/0", "nation:missing"},
           {"/squad_order", 24},
           {"/team", "club:138"}}) {
    after = before;
    after[Json::json_pointer(pointer)] = value;
    rejected([&] { doc.validate_change("players", before, after); },
             "invalid player value must fail immediately");
  }
  after = before;
  after["positions"].push_back("mid");
  rejected([&] { doc.validate_change("players", before, after); },
           "a player has one position");
  auto referee = world.rows("staff")[0], changed = referee;
  changed["attributes"]["strictness"] = 4;
  rejected([&] { doc.validate_change("staff", referee, changed); },
           "strictness has only two editable bits");
  changed = referee;
  changed["attributes"]["perception"] = -1;
  rejected([&] { doc.validate_change("staff", referee, changed); },
           "perception has only two editable bits");
  auto club = world.rows("clubs")[0], changed_club = club;
  changed_club["crest_selector"] = 512;
  rejected([&] { doc.validate_change("clubs", club, changed_club); },
           "crest selection has nine bits");
  changed_club = club;
  changed_club["kit"]["selector"] = 256;
  rejected([&] { doc.validate_change("clubs", club, changed_club); },
           "kit selection has eight bits");
}
void graphics_metadata() {
  auto doc = Document::from_bytes(source_bytes());
  auto world = projection();
  doc.capture(world);
  const Json asset{{"key", "asset:synthetic-verified-slot"},
                   {"files", {{"native", "assets/edited/synthetic.png"}}}};
  const Json recipe{{"id", "image-edit:synthetic"},
                    {"transform", {{"fit", "contain"}}}};
  doc.validate_change("assets", std::nullopt, asset);
  doc.validate_change("image-edits", std::nullopt, recipe);
  world.rows("assets").push_back(asset);
  world.rows("image-edits").push_back(recipe);
  check(doc.prepare(world).empty(),
        "graphics-owned metadata does not change the DATA write plan");
  auto deleted = asset;
  doc.validate_change("assets", deleted, std::nullopt);
  rejected([&] { doc.validate_change("review", std::nullopt, recipe); },
           "other metadata has no writable legacy record");
}
void roster() {
  const auto source = source_bytes();
  const auto tail = tail_at(source);
  auto doc = Document::from_bytes(source);
  auto world = projection();
  doc.capture(world);
  world.rows("players")[0]["squad_order"] = 1;
  world.rows("players")[1]["squad_order"] = 0;
  auto result = prepared(doc, world);
  auto expected = source;
  put(expected, tail + squad_words, 0xa001);
  put(expected, tail + squad_words + 2, 0xb000);
  check(
      result == expected,
      "atomic roster order preserves physical slot flags and all other bytes");
  world = projection();
  doc.capture(world);
  world.rows("players")[0]["team"] = "club:90";
  result = prepared(doc, world);
  expected = source;
  put(expected, tail + squad_words, 0xafff);
  put(expected, tail + squad_words + 2161 * 2,
      (word(source, tail + squad_words + 2161 * 2) & 0xf000) | 0);
  check(result == expected,
        "a team-only transfer takes its first free fixed slot");
  world = projection();
  doc.capture(world);
  world.rows("players")[0]["team"] = nullptr;
  expected = source;
  put(expected, tail + squad_words, 0xafff);
  check(prepared(doc, world) == expected,
        "unassigned players use the empty-slot sentinel");
  world = projection();
  doc.capture(world);
  world.rows("players")[0]["x-legacy"]["squad_row"] = 1;
  rejected([&] { doc.prepare(world); },
           "two players cannot occupy the same physical slot");
  world.rows("players")[1]["x-legacy"]["squad_row"] = 0;
  check(!doc.prepare(world).empty(),
        "the legacy row alias supports an atomic swap");
  world.rows("players")[0]["squad_order"] = 2;
  rejected([&] { doc.prepare(world); }, "conflicting row aliases must fail");
  world = projection();
  doc.capture(world);
  for (int i = 3; i <= 18; ++i)
    world.rows("players")[i]["team"] = "club:90";
  rejected([&] { doc.prepare(world); },
           "foreign squads cannot exceed sixteen players");
  world = projection();
  doc.capture(world);
  for (int i = 3; i <= 25; ++i)
    world.rows("players")[i]["team"] = "club:0";
  rejected([&] { doc.prepare(world); },
           "domestic squads cannot exceed twenty-four players");
  world = projection();
  doc.capture(world);
  auto moved = world.rows("players")[2];
  moved["squad_order"] = 16;
  rejected(
      [&] { doc.validate_change("players", world.rows("players")[2], moved); },
      "foreign explicit row has sixteen slots");
}
void malformed_sources() {
  auto source = source_bytes();
  rejected([&] { Document::from_bytes(source.substr(0, source.size() - 1)); },
           "truncated source must fail");
  rejected([&] { Document::from_bytes(source + 'x'); },
           "trailing source bytes must fail");
  auto broken = source;
  put(broken, tail_at(broken) + referee_offsets, 65535);
  rejected([&] { Document::from_bytes(broken); },
           "invalid referee offsets must fail");
  broken = source;
  put(broken, tail_at(broken) + stadium_offsets, 65535);
  rejected([&] { Document::from_bytes(broken); },
           "invalid stadium offsets must fail");
  broken = source;
  put(broken, tail_at(broken) + squad_words + 2, 0xb000);
  rejected([&] { Document::from_bytes(broken); },
           "duplicate original squad identities must fail");
  broken = source;
  put(broken, tail_at(broken) + squad_words + 2, 3000);
  rejected([&] { Document::from_bytes(broken); },
           "invalid original squad identities must fail");
}
void source_profiles() {
  struct Temporary {
    fs::path root =
        fs::temp_directory_path() /
        ("openeddi-synthetic-profiles-" +
         std::to_string(
             std::chrono::steady_clock::now().time_since_epoch().count()));
    Temporary() { fs::create_directory(root); }
    ~Temporary() {
      std::error_code ignored;
      fs::remove_all(root, ignored);
    }
  } temp;
  const auto write = [](const fs::path &path, const std::string &bytes) {
    std::ofstream output(path, std::ios::binary);
    output.write(bytes.data(), std::streamsize(bytes.size()));
    if (!output)
      throw std::runtime_error("synthetic fixture write failed");
  };
  auto raw = std::string(602527, '\0');
  raw.replace(0, 2, "MZ");
  put(raw, 0x3c, 0x80, 4);
  raw.replace(0x80, 2, "LE");
  put(raw, 0x80 + 0x14, 1, 4);
  put(raw, 0x80 + 0x28, 4096, 4);
  put(raw, 0x80 + 0x2c, 4096, 4);
  put(raw, 0x80 + 0x40, 0x90, 4);
  put(raw, 0x80 + 0x44, 1, 4);
  put(raw, 0x80 + 0x48, 0xa8, 4);
  put(raw, 0x80 + 0x80, 0x1000, 4);
  put(raw, 0x110, 4096, 4);
  put(raw, 0x114, 0xa2000, 4);
  put(raw, 0x11c, 1, 4);
  put(raw, 0x120, 1, 4);
  raw[0x12a] = 1; // one big-endian 24-bit physical page index
  const std::array<size_t, 4> de_addresses{0xa2c3e, 0xa2c52, 0xa2c42, 0xa2c66};
  const std::array<uint32_t, 4> counts{419, 2574, 168, 3096};
  for (size_t i = 0; i < counts.size(); ++i)
    put(raw, 0x1000 + de_addresses[i] - 0xa2000, counts[i], 4);
  const std::array<uint8_t, 9> de_sizes{18, 18, 18, 18, 18, 12, 12, 12, 12};
  for (size_t i = 0; i < de_sizes.size(); ++i)
    raw[0x1000 + 0xa2bcd - 0xa2000 + i] = char(de_sizes[i]);
  const auto source = source_bytes();
  write(temp.root / "bMh.ExE", raw);
  write(temp.root / "m3d.man", source);
  Document german(temp.root);
  auto world = projection();
  german.capture(world);
  world.rows("clubs")[0]["kit"]["selector"] = 111;
  check(german.prepare(world).contains("m3d.man"),
        "case-insensitive lookup retains source spelling");
  check(read_file(temp.root / "m3d.man") == source &&
            read_file(temp.root / "bMh.ExE") == raw,
        "prepare does not mutate installation files");
  auto broken = raw;
  put(broken, 0x1000 + de_addresses[0] - 0xa2000, 420, 4);
  write(temp.root / "bMh.ExE", broken);
  rejected([&] { Document unsupported(temp.root); },
           "unknown source table counts must be blocked");
  broken = raw;
  put(broken, 0x80 + 0x44, 0xffffffff, 4);
  write(temp.root / "bMh.ExE", broken);
  rejected([&] { Document unsupported(temp.root); },
           "malformed LE objects are bounded before allocation");
  write(temp.root / "bMh.ExE", "OPENBMH-IMAGE\0translated tables");
  rejected([&] { Document unsupported(temp.root); },
           "translated executable images must be blocked");
  fs::create_directory(temp.root / "english");
  auto english_raw = std::string(507925, '\0');
  english_raw.replace(0, 2, "MZ");
  const std::string x32_signature =
      "DOS extender Copyright 1991-1994 by Doug Huffman";
  english_raw.replace(256, x32_signature.size(), x32_signature);
  const std::array<size_t, 4> en_addresses{0x6f368, 0x6f37c, 0x6f36c, 0x6f390};
  for (size_t i = 0; i < counts.size(); ++i)
    put(english_raw, en_addresses[i] + 0x496d, counts[i], 4);
  const std::array<uint8_t, 9> en_sizes{20, 20, 20, 20, 10, 12, 12, 12, 12};
  for (size_t i = 0; i < en_sizes.size(); ++i)
    english_raw[0x6f2e0 + 0x496d + i] = char(en_sizes[i]);
  write(temp.root / "english" / "FLTD.EXE", english_raw);
  write(temp.root / "english" / "M3E.MAN", source);
  Document english(temp.root / "english");
  check(english.metadata()["edition"] == "FLTD",
        "original English raw layout is identified");
  english_raw[0x6f2e0 + 0x496d] = 18;
  write(temp.root / "english" / "FLTD.EXE", english_raw);
  rejected([&] { Document unsupported(temp.root / "english"); },
           "unknown English league layouts must be blocked");
}
} // namespace
int main() {
  try {
    no_op_and_masks();
    names_and_pool_limits();
    readonly_and_ranges();
    graphics_metadata();
    roster();
    malformed_sources();
    source_profiles();
    std::cout << "legacy data tests passed\n";
    return EXIT_SUCCESS;
  } catch (const std::exception &error) {
    std::cerr << "legacy data test failure: " << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
