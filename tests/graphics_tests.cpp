// SPDX-License-Identifier: GPL-3.0-only
#include "editor/world.hpp"
#include "formats/executable_layout.hpp"
#include "formats/le.hpp"
#include "legacy/graphics.hpp"
#include "legacy/exe_profile.hpp"
#include <algorithm>
#include <cctype>
#include <iostream>
#include <random>

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
  } catch (const Error &) {
    return;
  }
  throw std::runtime_error(why);
}
std::string bytes(std::initializer_list<int> values) {
  std::string out;
  for (auto v : values)
    out += char(v);
  return out;
}
std::string regional_executable() {
  // Entirely synthetic LE: one loaded code page, zero-filled data object, no
  // fixups. Only the small routing guards are meaningful machine instructions.
  std::string raw(602527, '\0');
  const auto put = [&](size_t at, uint32_t value, size_t count = 4) {
    for (size_t i = 0; i < count; ++i) raw.at(at + i) = char(value >> (i * 8));
  };
  raw.replace(0, 2, "MZ"); put(0x3c, 0x80); raw.replace(0x80, 2, "LE");
  put(0x80 + 0x14, 1); put(0x80 + 0x28, 4096); put(0x80 + 0x2c, 4096);
  put(0x80 + 0x40, 0xb0); put(0x80 + 0x44, 2); put(0x80 + 0x48, 0xe0);
  put(0x80 + 0x68, 0x100); put(0x80 + 0x6c, 0x110); put(0x80 + 0x80, 0x1ac00);
  put(0x130, 4096); put(0x134, 0x10000); put(0x138, 0x2045); put(0x13c, 1); put(0x140, 1);
  put(0x148, 0x21000); put(0x14c, 0xa0000); put(0x150, 0x2043); put(0x154, 2);
  raw.at(0x162) = 1;
  const auto instruction = [&](size_t at, std::initializer_list<int> values) {
    for (auto value : values) raw.at(at++) = char(value);
  };
  instruction(0x1ac5f, {0x66, 0x81, 0xfb, 0xff, 0x0f}); // Sentinel comparison.
  instruction(0x1ac64, {0x0f, 0x84, 0x9b, 0, 0, 0}); // Sentinel fallback.
  instruction(0x1ac6a, {0x66, 0x83, 0xfb, 0x5a});
  instruction(0x1ac6e, {0x7c, 0x19});
  instruction(0x1ac70, {0x66, 0x81, 0xfb, 0x5a, 0}); // Regional threshold.
  instruction(0x1ac75, {0x7c, 0x07});
  instruction(0x1ac77, {0x66, 0x81, 0xfb, 0xa8, 0});
  instruction(0x1ac7c, {0x7c, 0x0b});
  instruction(0x1ac7e, {0x66, 0x81, 0xfb, 0x65, 0x01});
  instruction(0x1ac83, {0x0f, 0x8c, 0x7c, 0, 0, 0});
  instruction(0x1ac89, {0x0f, 0xbf, 0xc3});
  return raw;
}
void executable_profile_tests() {
  const auto regional = regional_executable();
  check(has_regional_crest_routing(regional), "Verified regional routing not recognized");
  auto original = regional; original.at(0x1ac73) = char(138);
  check(!has_regional_crest_routing(original), "Original fallback classified as regional routing");
  for (auto at : {size_t(0), size_t(0x3c), size_t(0x80), size_t(0xa8), size_t(0x100),
                  size_t(0x134), size_t(0x13c), size_t(0x162), size_t(0x1ac63),
                  size_t(0x1ac65), size_t(0x1ac6d), size_t(0x1ac6f), size_t(0x1ac74),
                  size_t(0x1ac76), size_t(0x1ac7a), size_t(0x1ac7d), size_t(0x1ac81),
                  size_t(0x1ac84), size_t(0x1ac8a)}) {
    auto wrong = regional; wrong.at(at) ^= 1;
    check(!has_regional_crest_routing(wrong), "Unverified header/routing variant recognized");
  }
  check(!has_regional_crest_routing(regional.substr(0, 507925)) &&
            !has_regional_crest_routing("") && !has_regional_crest_routing(std::string(507925, '\0')),
        "English, truncated or empty executable recognized as German regional patch");
}
std::string stream(int w, int h, const std::string &rle,
                   const std::string &suffix = {}) {
  std::string out;
  for (auto [value, count] : {std::pair{w, 2}, {h, 2}, {int(rle.size()), 4}})
    for (int i = 0; i < count; ++i)
      out += char(uint32_t(value) >> (i * 8));
  return out + rle + suffix;
}
GraphicPalette gray() {
  GraphicPalette p{};
  for (int i = 0; i < 256; ++i)
    p[i] = 0xff000000u | uint32_t(i) * 0x010101;
  return p;
}
images::Raster solid(int w, int h, uint32_t color) {
  images::Raster r{w, h, std::vector<uint8_t>(size_t(w) * h * 4)};
  for (size_t i = 0; i < r.pixels.size(); i += 4) {
    r.pixels[i] = uint8_t(color >> 16);
    r.pixels[i + 1] = uint8_t(color >> 8);
    r.pixels[i + 2] = uint8_t(color);
    r.pixels[i + 3] = uint8_t(color >> 24);
  }
  return r;
}
void rle_tests() {
  check(decode_vga(stream(3, 2, bytes({133, 7, 0xa5}), "suffix")).pixels ==
            std::vector<uint8_t>(6, 7),
        "continuous run crosses rows");
  auto clipped = decode_vga(stream(2, 1, bytes({255, 19, 0x33})));
  check(clipped.pixels == std::vector<uint8_t>({19, 19}) &&
            clipped.trailing == 0x33,
        "final run clips at pixel boundary");
  check(decode_vga(stream(2, 1, bytes({127, 6, 7, 0x55}))).pixels ==
            std::vector<uint8_t>({6, 7}),
        "final literal clips like Original");
  rejects([] { decode_vga(""); }, "short header accepted");
  rejects([] { decode_vga(stream(0, 2, bytes({0}))); },
          "zero dimensions accepted");
  rejects([] { decode_vga(stream(65535, 65535, bytes({0}))); },
          "oversize dimensions accepted");
  rejects([] { decode_vga(stream(2, 1, bytes({128}))); },
          "truncated run accepted");
  rejects([] { decode_vga(stream(3, 1, bytes({2, 1, 2}))); },
          "truncated literals accepted");
  rejects([] { decode_vga(stream(1, 1, bytes({0, 1}))); },
          "missing trailing byte accepted");
  rejects([] { decode_vga(stream(1, 1, bytes({0, 1, 0, 0}))); },
          "extra stream bytes accepted");
  auto bad_length = stream(1, 1, bytes({0, 1, 0}));
  bad_length[4] = 127;
  rejects([&] { decode_vga(bad_length); }, "bad declared length accepted");
  std::mt19937 random(0x45444449);
  for (int pattern = 0; pattern < 5; ++pattern)
    for (int n : {1, 2, 127, 128, 129, 255, 256, 513}) {
      IndexedVga image{n, 3, std::vector<uint8_t>(size_t(n) * 3), 0xe7,
                       std::string("\0private-tail\xff", 14)};
      for (size_t i = 0; i < image.pixels.size(); ++i)
        image.pixels[i] = pattern == 0   ? 9
                          : pattern == 1 ? uint8_t(i)
                          : pattern == 2 ? uint8_t(i / 128)
                          : pattern == 3 ? uint8_t(i / 2)
                                         : uint8_t(random());
      const auto encoded = encode_vga(image);
      const auto decoded = decode_vga(encoded);
      check(decoded.width == image.width && decoded.height == image.height &&
                decoded.pixels == image.pixels &&
                decoded.suffix == image.suffix &&
                decoded.trailing == image.trailing,
            "RLE round trip changed bytes");
      check(encoded == encode_vga(decoded), "encoder not deterministic");
    }
  rejects([] { encode_vga({2, 2, {1}, 0, {}}); },
          "incorrect pixel count accepted");
}
void conversion_tests() {
  auto p = gray();
  auto wide = solid(4, 2, 0xffff0000);
  p[250] = 0xffff0000;
  const auto contain = convert_indexed(wide, 4, 4, p, true);
  check(contain.width == 4 && contain.height == 4 && contain.pixels[3] == 0 &&
            contain.pixels[4 * 4 + 3] == 255,
        "default contain distorted aspect or lost transparent padding");
  const auto cover = convert_indexed(wide, 4, 4, p, true, {{"fit", "cover"}});
  check(cover.pixels[3] == 255 && cover.pixels[2] == 0 &&
            cover.pixels[0] == 255,
        "cover left padding");
  auto stripe = solid(4, 1, 0xff000000);
  for (int x = 2; x < 4; ++x)
    for (int c = 0; c < 3; ++c)
      stripe.pixels[x * 4 + c] = 255;
  const auto cropped = convert_indexed(stripe, 2, 1, gray(), false,
                                       {{"crop", {{"x", .5}, {"width", .5}}}});
  check(cropped.pixels == solid(2, 1, 0xffffffff).pixels,
        "crop geometry wrong");
  rejects(
      [&] {
        convert_indexed(wide, 1, 1, p, false,
                        {{"crop", {{"x", .9}, {"width", .2}}}});
      },
      "invalid crop accepted");
  rejects([&] { convert_indexed(wide, 1, 1, p, false, {{"fit", "stretch"}}); },
          "unknown fit accepted");
  rejects([&] { convert_indexed(wide, 1, 1, p, false, {{"dither", "magic"}}); },
          "unknown dither accepted");
  GraphicPalette bw{};
  bw.fill(0xff000000);
  bw[255] = 0xffffffff;
  auto middle = solid(16, 16, 0xff808080);
  const auto plain = convert_indexed(middle, 16, 16, bw, false),
             dithered =
                 convert_indexed(middle, 16, 16, bw, false, {{"dither", true}});
  check(plain.pixels != dithered.pixels, "dither produced no effect");
  check(dithered.pixels == convert_indexed(middle, 16, 16, bw, false,
                                           {{"dither", "floyd-steinberg"}})
                               .pixels,
        "dither is not deterministic");
  int white = 0;
  for (size_t i = 0; i < dithered.pixels.size(); i += 4) {
    check(dithered.pixels[i] == 0 || dithered.pixels[i] == 255,
          "dither emitted nonpalette color");
    white += dithered.pixels[i] == 255;
  }
  check(white > 100 && white < 156, "dither mean brightness drifted");
  auto alpha = solid(1, 1, 0x00ffffff);
  check(convert_indexed(alpha, 1, 1, gray(), true).pixels[3] == 0,
        "transparent slot lost index zero");
  check(convert_indexed(alpha, 1, 1, gray(), false).pixels ==
            solid(1, 1, 0xff000000).pixels,
        "opaque slot did not flatten alpha");
  auto duplicates = gray();
  duplicates[0] = 0xffff0000;
  duplicates[1] = 0xff000000;
  check(convert_indexed(solid(1, 1, 0xffff0000), 1, 1, duplicates, true)
                .pixels[3] == 255,
        "opaque source became transparent palette zero");
}
void embedded_tests() {
  const std::string original = "prefixABCDEFGHIJKLsuffix";
  VerifiedEmbeddedSlot slot{6, 2, 3, 4, "ABCDEFGHIJKL"};
  const auto result =
      patch_embedded_indices(original, slot, {1, 2, 3, 4, 5, 6});
  for (size_t i = 0; i < original.size(); ++i) {
    const bool inside = i >= 6 && i < 18 && (i - 6) % 4 < 2;
    if (!inside)
      check(result[i] == original[i],
            "embedded patch changed byte outside slot");
  }
  check(uint8_t(result[6]) == 1 && uint8_t(result[15]) == 6,
        "embedded stride wrong");
  auto wrong = slot;
  wrong.expected[0] = '!';
  rejects([&] { patch_embedded_indices(original, wrong, {1, 2, 3, 4, 5, 6}); },
          "unverified embedded bytes accepted");
  wrong = slot;
  wrong.file_offset = original.size();
  rejects([&] { patch_embedded_indices(original, wrong, {1, 2, 3, 4, 5, 6}); },
          "embedded offset out of bounds accepted");
  wrong = slot;
  wrong.stride = 1;
  rejects([&] { patch_embedded_indices(original, wrong, {1, 2, 3, 4, 5, 6}); },
          "embedded invalid stride accepted");
}
struct Fixture {
  fs::path root = fs::temp_directory_path() / path(new_id("graphics-tests-"));
  World world = World::blank("Synthetic graphics fixture");
  GraphicPalette palette{};
  Json resources = {{"images", Json::array()}};
  std::map<std::string, IndexedVga> sheets;
  Fixture() {
    fs::create_directories(root / "KB");
    fs::create_directories(root / "W");
    std::string dac;
    for (int i = 0; i < 256; ++i) {
      const int r = i & 63, g = i >> 6;
      dac += char(r);
      dac += char(g);
      dac += char(0);
      palette[i] = 0xff000000u | uint32_t((r * 255 + 31) / 63) << 16 |
                   uint32_t((g * 255 + 31) / 63) << 8;
    }
    write_atomic(root / "M3PC.COL", dac);
    sheet("kb/0", 64, 40, 1);
    sheet("w/0", 304, 38, 2);
    sheet("kb/29", 320, 240, 3);
    // Full-sheet overrides differ from the palette used for crest/portrait
    // extraction.
    sheets.at("w/0").suffix = dac + std::string("\0unknown-tail", 13);
    sheets.at("w/0").suffix[32 * 3] = char(63);
    write_sheet("w/0");
    auto override = palette;
    override[32] = 0xffff0000;
    add("asset:classic/kb/0", "kb/0", "kb/0", 0, 0, 64, 40, false, palette);
    add("asset:classic/w/0", "w/0", "w/0", 0, 0, 304, 38, false, override);
    add("asset:classic/kb/29", "kb/29", "kb/29", 0, 0, 320, 240, false,
        palette);
    club(90, 0);
    club(91, 0);
    club(0, 3);
    club(1, 4);
    for (int i = 0; i < 4; ++i)
      add("asset:portrait/bmh-classic/background/" + std::to_string(i),
          "portraits/background-" + std::to_string(i), "kb/29", 108 + i * 38,
          168, 38, 38, false, palette);
    bmh::Bytes memory(bmh::layout::GermanImageEnd - 0x10000);
    const auto put = [&](uint32_t at, uint8_t v) {
      memory.at(at - 0x10000) = v;
    };
    put(bmh::layout::ClubPlayerDetail_DrawPortrait_RuntimeField, 254);
    put(bmh::layout::ClubPlayerDetail_DrawPortrait_RuntimeField2, 2);
    put(bmh::layout::ClubPlayerDetail_ClubPlayerDetailRedraw_RuntimeField2, 2);
    put(bmh::layout::ClubPlayerDetail_DrawPortrait_RuntimeField3, 80);
    put(bmh::layout::ClubPlayerDetail_DrawPortrait_RuntimeField4, 2);
    put(bmh::layout::ClubPlayerDetail_DrawPortrait_RuntimeField7, 64);
    put(bmh::layout::ClubPlayerDetail_DrawPortrait_RuntimeField7 + 1, 1);
    bmh::save_image(bmh::LeImage::from_bytes(0x10000, std::move(memory)),
                    root / "BMH.EXE");
    for (int i = 0; i < 2; ++i)
      add("asset:portrait/bmh-classic/layer/0/" + std::to_string(i),
          "portraits/layer-0-" + std::to_string(i), "kb/29", 254, 2 + i * 2, 80,
          2, true, palette);
    world.store->set("assets/classic/resources.json",
                     std::make_shared<const std::string>(resources.dump()));
  }
  ~Fixture() {
    std::error_code ec;
    fs::remove_all(root, ec);
  }
  void write_sheet(const std::string &name) {
    auto relative = name;
    std::transform(relative.begin(), relative.end(), relative.begin(),
                   [](unsigned char c) { return char(std::toupper(c)); });
    write_atomic(root / path(relative + ".VGA"), encode_vga(sheets.at(name)));
  }
  void sheet(const std::string &name, int w, int h, int seed) {
    IndexedVga image{
        w, h, std::vector<uint8_t>(size_t(w) * h), uint8_t(0xa0 + seed), {}};
    for (size_t i = 0; i < image.pixels.size(); ++i)
      image.pixels[i] = uint8_t((i + seed) % 256);
    sheets[name] = std::move(image);
    write_sheet(name);
  }
  void add(const std::string &key, const std::string &name,
           const std::string &source, int x, int y, int w, int h,
           bool transparent, const GraphicPalette &p) {
    std::string indices;
    images::Raster r{w, h, std::vector<uint8_t>(size_t(w) * h * 4)};
    const auto &s = sheets.at(source);
    for (int dy = 0; dy < h; ++dy)
      for (int dx = 0; dx < w; ++dx) {
        const auto index = s.pixels.at(size_t(y + dy) * s.width + x + dx);
        indices += char(index);
        const size_t at = (size_t(dy) * w + dx) * 4;
        for (int c = 0; c < 3; ++c)
          r.pixels[at + c] = uint8_t(p[index] >> (16 - c * 8));
        r.pixels[at + 3] = transparent && !index ? 0 : 255;
      }
    const auto base = "assets/classic/images/" + name;
    world.store->set(base + ".png", std::make_shared<const std::string>(
                                        images::encode_png(r)));
    world.store->set(base + ".pixels",
                     std::make_shared<const std::string>(indices));
    world.rows("assets").push_back(
        {{"key", key}, {"files", {{"native", base + ".png"}}}});
    resources["images"].push_back(
        {{"name", name}, {"pixels", base + ".pixels"}, {"png", base + ".png"}});
  }
  void club(int team, int selector) {
    const auto id = "club:" + std::to_string(team),
               key = "asset:club/" + std::to_string(team) + "/crest";
    const bool neutral = team >= 90;
    add(key, "crests/" + std::to_string(team), neutral ? "kb/0" : "w/0",
        neutral ? 8 : (selector & 7) * 38, 0, 38, 38, true, palette);
    world.rows("clubs").push_back(
        {{"id", id},
         {"crest", key},
         {"x-legacy", {{"team_index", team}, {"badge_word", selector}}}});
  }
  void change_pixel(const std::string &key, size_t pixel, uint8_t index,
                    const GraphicPalette *p = nullptr) {
    const auto file = world.asset_file(key);
    auto r = images::decode(world.store->read(file), ".png");
    const auto color = (p ? *p : palette)[index];
    for (int c = 0; c < 3; ++c)
      r.pixels.at(pixel * 4 + c) = uint8_t(color >> (16 - c * 8));
    r.pixels.at(pixel * 4 + 3) = 255;
    world.store->set(
        file, std::make_shared<const std::string>(images::encode_png(r)));
  }
};
void integration_tests() {
  Fixture f;
  Graphics g(f.root);
  g.capture(f.world);
  check(g.prepare(f.world).empty(),
        "opening installation changes original files");
  check(g.supports("asset:classic/w/0") &&
            g.supports("asset:portrait/bmh-classic/layer/0/0"),
        "verified sheets/layers not bound");
  check(!g.supports("asset:player/1/photo"),
        "unverified individual photo writable");
  check(g.info("asset:club/90/crest").value("shared_fallback", false),
        "fallback metadata absent");
  rejects(
      [&] {
        g.target_key(f.world,
                     {{"target", {{"table", "clubs"}, {"id", "club:90"}}}});
      },
      "shared fallback did not require confirmation");
  check(g.target_key(f.world,
                     {{"target", {{"table", "clubs"}, {"id", "club:90"}}},
                      {"confirm_shared", true}}) == "asset:club/90/crest",
        "confirmed fallback target wrong");
  rejects(
      [&] {
        g.target_key(
            f.world,
            {{"target",
              {{"table", "people"}, {"id", "person:1"}, {"field", "photo"}}}});
      },
      "individual portrait target accepted");
  const auto full = g.convert(f.world, "asset:classic/w/0",
                              solid(1, 1, 0xffff0000), {{"fit", "cover"}});
  check(full.pixels[0] == 255 && full.pixels[1] == 0,
        "whole sheet embedded palette ignored");
  const auto global = g.convert(f.world, "asset:club/0/crest",
                                solid(1, 1, f.palette[32]), {{"fit", "cover"}});
  check(global.pixels[0] == 130, "global DAC palette conversion wrong");
  f.change_pixel("asset:club/90/crest", 0, 11);
  f.change_pixel("asset:club/91/crest", 1, 12);
  auto changed = g.prepare(f.world);
  check(changed.size() == 1 && changed.contains("KB/0.VGA"),
        "shared aliases did not merge to one file");
  auto merged = decode_vga(changed.at("KB/0.VGA"));
  auto expected = f.sheets.at("kb/0").pixels;
  expected[8] = 11;
  expected[9] = 12;
  check(merged.pixels == expected &&
            merged.trailing == f.sheets.at("kb/0").trailing,
        "shared edits changed neighbours");
  g.mark_saved(f.world);
  check(g.prepare(f.world).empty(), "saved aliases retain dirty differences");
  check(uint8_t(f.world.store->read(
            "assets/classic/images/crests/90.pixels")[1]) == 12,
        "alias index projection stale after save");
  const auto alias = images::decode(
      f.world.store->read(f.world.asset_file("asset:club/91/crest")), ".png");
  check(alias.pixels[0] == uint8_t(f.palette[11] >> 16),
        "alias PNG projection stale after save");
  f.change_pixel("asset:club/91/crest", 2, 13);
  merged = decode_vga(g.prepare(f.world).at("KB/0.VGA"));
  expected[10] = 13;
  check(merged.pixels == expected,
        "subsequent alias edit reverted a saved neighbour");
  f.change_pixel("asset:club/90/crest", 2, 14);
  rejects([&] { g.prepare(f.world); }, "conflicting alias edits accepted");
  // Independent fixture isolates full-sheet overrides and linear packed
  // geometry.
  Fixture f2;
  Graphics g2(f2.root);
  g2.capture(f2.world);
  f2.change_pixel("asset:club/0/crest", 37, 44);
  f2.change_pixel("asset:club/1/crest", 0, 45);
  const auto w = decode_vga(g2.prepare(f2.world).at("W/0.VGA"));
  auto w_expected = f2.sheets.at("w/0").pixels;
  w_expected[3 * 38 + 37] = 44;
  w_expected[4 * 38] = 45;
  check(w.pixels == w_expected && w.suffix == f2.sheets.at("w/0").suffix &&
            w.trailing == f2.sheets.at("w/0").trailing,
        "crest cell boundaries/palette suffix changed");
  f2.change_pixel("asset:portrait/bmh-classic/layer/0/0", 70, 46);
  auto portrait_expected = f2.sheets.at("kb/29").pixels;
  portrait_expected[2 * 320 + 254 + 70] = 46;
  const auto portraits = decode_vga(g2.prepare(f2.world).at("KB/29.VGA"));
  check(portraits.pixels == portrait_expected,
        "packed sprite row wrap changed neighbours");
  g2.mark_saved(f2.world);
  check(g2.prepare(f2.world).empty(),
        "merged sheet projection dirty after save");
  const auto unsupported_file = f2.world.asset_file("asset:club/0/crest");
  f2.world.store->set(unsupported_file,
                      std::make_shared<const std::string>(
                          images::encode_png(solid(38, 38, 0xff010203))));
  rejects([&] { g2.prepare(f2.world); },
          "unquantized edit reached original save");
  f2.world.store->set(unsupported_file,
                      std::make_shared<const std::string>(
                          images::encode_png(solid(1, 1, f2.palette[2]))));
  rejects([&] { g2.prepare(f2.world); }, "wrong-sized original edit accepted");
  // A single imported fallback still changes the shared original generic icon.
  Fixture f3;
  auto &clubs = f3.world.rows("clubs");
  clubs.erase(clubs.begin() + 1);
  auto &assets = f3.world.rows("assets");
  assets.erase(std::remove_if(assets.begin(), assets.end(),
                              [](const Json &a) {
                                return str(a, "key") == "asset:club/91/crest";
                              }),
               assets.end());
  Graphics g3(f3.root);
  g3.capture(f3.world);
  check(g3.info("asset:club/90/crest").value("requires_confirmation", false),
        "single fallback alias lacks shared confirmation");
}
void regional_projection_tests() {
  Fixture f;
  // Upstream deliberately imports these as the original shared fallback.
  f.club(137, 3);
  f.club(168, 4);
  f.world.store->set("assets/classic/resources.json",
                    std::make_shared<const std::string>(f.resources.dump()));
  const auto key = "asset:club/90/crest";
  const auto fallback_png = f.world.store->read(f.world.asset_file(key));
  const auto fallback_pixels = f.world.store->read("assets/classic/images/crests/90.pixels");
  const auto far_png = f.world.store->read(f.world.asset_file("asset:club/168/crest"));
  const auto raw = regional_executable();
  write_atomic(f.root / "BMH.EXE", raw);
  Graphics g(f.root);
  g.capture(f.world);
  check(!g.info(key).value("shared_fallback", true) &&
            !g.info("asset:club/137/crest").value("shared_fallback", true),
        "Recognized regional crest still uses computer fallback");
  check(g.info("asset:club/168/crest").value("shared_fallback", false) &&
            f.world.store->read(f.world.asset_file("asset:club/168/crest")) == far_png,
        "Far-foreign fallback was changed by regional recognition");
  check(f.world.store->read(f.world.asset_file(key)) != fallback_png &&
            f.world.store->read("assets/classic/images/crests/90.pixels") != fallback_pixels,
        "Regional cached PNG or indexed pixels were not refreshed");
  const auto rgba = images::decode(f.world.store->read(f.world.asset_file(key)), ".png");
  const auto &sheet = f.sheets.at("w/0");
  const auto pixels = f.world.store->read("assets/classic/images/crests/90.pixels");
  for (int y = 0; y < 38; ++y)
    for (int x = 0; x < 38; ++x)
      check(uint8_t(pixels[size_t(y) * 38 + x]) == sheet.pixels[size_t(y) * sheet.width + x],
            "Regional slot was not extracted with the original packed stride");
  check(rgba.pixels == convert_indexed(rgba, 38, 38, f.palette, true).pixels,
        "Regional projection used the whole-sheet override palette");
  check(g.prepare(f.world).empty() && read_file(f.root / "BMH.EXE") == raw,
        "Read-only regional import schedules changes or patches its executable");
  g.mark_saved(f.world);
  check(g.prepare(f.world).empty(), "Regional baseline was not stable after save bookkeeping");
}
} // namespace
int main() {
  try {
    rle_tests();
    executable_profile_tests();
    conversion_tests();
    embedded_tests();
    integration_tests();
    regional_projection_tests();
    std::cout << "graphics tests passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "graphics tests: " << e.what() << '\n';
    return 1;
  }
}
