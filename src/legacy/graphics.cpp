// SPDX-License-Identifier: GPL-3.0-only
#include "legacy/graphics.hpp"
#include "editor/world.hpp"
#include "formats/executable_layout.hpp"
#include "formats/le.hpp"
#include "launch/fltd.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <numeric>

namespace bmh::editor::legacy {
namespace {
[[noreturn]] void invalid(const std::string &why) {
  throw Error("legacy_graphics", {{"reason", why}});
}
uint8_t byte(std::string_view s, size_t p) {
  if (p >= s.size())
    invalid("truncated image");
  return uint8_t(s[p]);
}
uint32_t number(std::string_view s, size_t p, int count) {
  uint32_t n = 0;
  for (int i = 0; i < count; ++i)
    n |= uint32_t(byte(s, p + i)) << (i * 8);
  return n;
}
void little(std::string &out, uint32_t n, int count) {
  for (int i = 0; i < count; ++i)
    out += char(n >> (i * 8));
}
void dimensions(int w, int h) {
  if (w <= 0 || h <= 0 || w > 65535 || h > 65535 ||
      uint64_t(w) * h > 16 * 1024 * 1024)
    invalid("invalid slot dimensions");
}
GraphicPalette read_palette(std::string_view bytes) {
  if (bytes.size() < 768)
    invalid("truncated palette");
  GraphicPalette p{};
  for (size_t i = 0; i < p.size(); ++i) {
    const auto c = [&](size_t ch) {
      return uint32_t(((byte(bytes, i * 3 + ch) & 63) * 255 + 31) / 63);
    };
    p[i] = 0xff000000u | c(0) << 16 | c(1) << 8 | c(2);
  }
  return p;
}
fs::path find(const fs::path &root, const std::string &name) {
  if (!fs::is_directory(root))
    return {};
  fs::path result;
  for (const auto &entry : fs::directory_iterator(root))
    if (folded(utf8(entry.path().filename())) == folded(name)) {
      if (!result.empty())
        invalid("ambiguous case-insensitive source path");
      if (fs::is_symlink(entry.symlink_status()))
        invalid("symlink source graphics");
      result = entry.path();
    }
  return result;
}
std::vector<uint8_t> extract(const IndexedVga &atlas, int start, int w, int h) {
  dimensions(w, h);
  if (start < 0 ||
      size_t(start) + size_t(h - 1) * atlas.width + w > atlas.pixels.size())
    invalid("slot exceeds packed atlas");
  std::vector<uint8_t> result(size_t(w) * h);
  for (int y = 0; y < h; ++y)
    std::copy_n(atlas.pixels.begin() + start + size_t(y) * atlas.width, w,
                result.begin() + size_t(y) * w);
  return result;
}
std::vector<uint8_t> indices(const images::Raster &r, const GraphicPalette &p,
                             bool transparent) {
  dimensions(r.width, r.height);
  if (r.pixels.size() != size_t(r.width) * r.height * 4)
    invalid("invalid raster bytes");
  std::vector<uint8_t> out(size_t(r.width) * r.height);
  for (size_t i = 0; i < out.size(); ++i) {
    if (transparent && r.pixels[i * 4 + 3] < 128) {
      out[i] = 0;
      continue;
    }
    int best = transparent ? 1 : 0, distance = std::numeric_limits<int>::max();
    for (int j = transparent ? 1 : 0; j < 256; ++j) {
      int d = 0;
      for (int c = 0; c < 3; ++c) {
        int delta =
            int(r.pixels[i * 4 + c]) - int((p[j] >> (16 - c * 8)) & 255);
        d += delta * delta;
      }
      if (d < distance) {
        distance = d;
        best = j;
      }
    }
    out[i] = uint8_t(best);
  }
  return out;
}
images::Raster raster(const std::vector<uint8_t> &pixels, int w, int h,
                      const GraphicPalette &p, bool transparent) {
  images::Raster r{w, h, std::vector<uint8_t>(pixels.size() * 4)};
  for (size_t i = 0; i < pixels.size(); ++i) {
    for (int c = 0; c < 3; ++c)
      r.pixels[i * 4 + c] = uint8_t(p[pixels[i]] >> (16 - c * 8));
    r.pixels[i * 4 + 3] = transparent && pixels[i] == 0 ? 0 : 255;
  }
  return r;
}
bool overlaps(int a, int aw, int ah, int b, int bw, int bh, int stride) {
  for (int ay = 0, by = 0; ay < ah && by < bh;) {
    const int left = a + ay * stride, right = b + by * stride;
    if (left < right + bw && right < left + aw)
      return true;
    if (left + aw <= right)
      ++ay;
    else
      ++by;
  }
  return false;
}
} // namespace
IndexedVga decode_vga(std::string_view bytes) {
  if (bytes.size() < 8)
    invalid("VGA header truncated");
  IndexedVga result;
  result.width = int(number(bytes, 0, 2));
  result.height = int(number(bytes, 2, 2));
  dimensions(result.width, result.height);
  const size_t end = 8 + size_t(number(bytes, 4, 4));
  if (end > bytes.size() || end <= 8)
    invalid("VGA stream length invalid");
  const size_t count = size_t(result.width) * result.height;
  result.pixels.reserve(count);
  size_t at = 8;
  while (result.pixels.size() < count) {
    if (at >= end)
      invalid("VGA stream ends early");
    const auto code = byte(bytes, at++);
    const size_t n = code >= 128 ? code - 127 : code + 1;
    const auto remaining = count - result.pixels.size();
    // Original decoder clips its last command at the pixel boundary.
    if (code >= 128) {
      if (at >= end)
        invalid("VGA truncated run");
      const auto v = byte(bytes, at++);
      result.pixels.insert(result.pixels.end(), std::min(n, remaining), v);
    } else {
      const size_t take = std::min(n, remaining);
      if (take > end - at)
        invalid("VGA truncated literals");
      for (size_t i = 0; i < take; ++i)
        result.pixels.push_back(byte(bytes, at++));
    }
  }
  if (at + 1 != end)
    invalid("VGA unexpected trailing stream data");
  result.trailing = byte(bytes, at);
  result.suffix = std::string(bytes.substr(end));
  return result;
}
std::string encode_vga(const IndexedVga &image) {
  dimensions(image.width, image.height);
  if (image.pixels.size() != size_t(image.width) * image.height)
    invalid("VGA pixel count invalid");
  std::string stream;
  const auto &p = image.pixels;
  size_t at = 0;
  while (at < p.size()) {
    size_t run = 1;
    while (run < 128 && at + run < p.size() && p[at + run] == p[at])
      ++run;
    if (run >= 2) {
      stream += char(127 + run);
      stream += char(p[at]);
      at += run;
      continue;
    }
    const size_t start = at++;
    while (at - start < 128 && at < p.size()) {
      if (at + 1 < p.size() && p[at] == p[at + 1])
        break;
      ++at;
    }
    stream += char(at - start - 1);
    stream.append(reinterpret_cast<const char *>(p.data() + start), at - start);
  }
  stream += char(image.trailing);
  std::string out;
  little(out, uint32_t(image.width), 2);
  little(out, uint32_t(image.height), 2);
  little(out, uint32_t(stream.size()), 4);
  out += stream;
  out += image.suffix;
  if (decode_vga(out).pixels != image.pixels)
    invalid("VGA round-trip verification failed");
  return out;
}
std::string patch_embedded_indices(std::string_view file,
                                   const VerifiedEmbeddedSlot &slot,
                                   const std::vector<uint8_t> &pixels) {
  dimensions(slot.width, slot.height);
  if (slot.stride < slot.width ||
      slot.expected.size() != size_t(slot.stride) * slot.height ||
      pixels.size() != size_t(slot.width) * slot.height ||
      slot.file_offset > file.size() ||
      slot.expected.size() > file.size() - slot.file_offset ||
      file.substr(slot.file_offset, slot.expected.size()) != slot.expected)
    invalid("embedded physical slot not verified");
  std::string result(file);
  for (int y = 0; y < slot.height; ++y)
    std::copy_n(pixels.data() + size_t(y) * slot.width, slot.width,
                result.data() + slot.file_offset + size_t(y) * slot.stride);
  return result;
}
images::Raster convert_indexed(const images::Raster &source, int width,
                               int height, const GraphicPalette &palette,
                               bool transparent, const Json &recipe) {
  dimensions(source.width, source.height);
  dimensions(width, height);
  if (source.pixels.size() != size_t(source.width) * source.height * 4 ||
      !recipe.is_object())
    invalid("invalid conversion input");
  double cx = 0, cy = 0, cw = 1, ch = 1;
  if (recipe.contains("crop")) {
    const auto &crop = recipe.at("crop");
    cx = crop.value("x", 0.0);
    cy = crop.value("y", 0.0);
    cw = crop.value("width", 1.0);
    ch = crop.value("height", 1.0);
  }
  if (!std::isfinite(cx) || !std::isfinite(cy) || !std::isfinite(cw) ||
      !std::isfinite(ch) || cx < 0 || cy < 0 || cw <= 0 || ch <= 0 ||
      cx + cw > 1.000001 || cy + ch > 1.000001)
    invalid("invalid crop");
  const std::string fit = str(recipe, "fit", "contain");
  if (fit != "contain" && fit != "cover" && fit != "crop" && fit != "aspectfit")
    invalid("unknown image fit");
  const bool cover = fit == "cover" || fit == "crop";
  const double sw = source.width * cw, sh = source.height * ch;
  const double scale = cover ? std::max(width / sw, height / sh)
                             : std::min(width / sw, height / sh);
  images::Raster fitted{width, height,
                        std::vector<uint8_t>(size_t(width) * height * 4)};
  for (int y = 0; y < height; ++y)
    for (int x = 0; x < width; ++x) {
      auto *dst = fitted.pixels.data() + (size_t(y) * width + x) * 4;
      const double sx = (x + .5 - width / 2.0) / scale + sw / 2,
                   sy = (y + .5 - height / 2.0) / scale + sh / 2;
      if (sx < 0 || sy < 0 || sx >= sw || sy >= sh) {
        for (int c = 0; c < 3; ++c)
          dst[c] = uint8_t(palette[0] >> (16 - c * 8));
        dst[3] = transparent ? 0 : 255;
        continue;
      }
      const int ix = std::clamp(int(sx + cx * source.width), 0,
                                source.width - 1),
                iy = std::clamp(int(sy + cy * source.height), 0,
                                source.height - 1);
      const auto *src =
          source.pixels.data() + (size_t(iy) * source.width + ix) * 4;
      const double alpha = src[3] / 255.0;
      for (int c = 0; c < 3; ++c)
        dst[c] = transparent
                     ? src[c]
                     : uint8_t(std::round(src[c] * alpha +
                                          ((palette[0] >> (16 - c * 8)) & 255) *
                                              (1 - alpha)));
      dst[3] = transparent ? src[3] : 255;
    }
  const auto dithering = recipe.value("dither", Json(false));
  if (dithering != false && dithering != true && dithering != "none" &&
      dithering != "floyd-steinberg")
    invalid("unknown image dithering");
  const bool dither = dithering == true || dithering == "floyd-steinberg";
  if (!dither)
    return raster(indices(fitted, palette, transparent), width, height, palette,
                  transparent);
  std::vector<std::array<double, 3>> errors(size_t(width) * height);
  std::vector<uint8_t> output(size_t(width) * height);
  for (int y = 0; y < height; ++y)
    for (int x = 0; x < width; ++x) {
      const size_t i = size_t(y) * width + x;
      if (transparent && fitted.pixels[i * 4 + 3] < 128) {
        output[i] = 0;
        continue;
      }
      images::Raster one{1, 1, std::vector<uint8_t>(4, 255)};
      for (int c = 0; c < 3; ++c)
        one.pixels[c] = uint8_t(std::clamp(
            std::round(fitted.pixels[i * 4 + c] + errors[i][c]), 0.0, 255.0));
      const auto q = indices(one, palette, transparent)[0];
      output[i] = q;
      for (int c = 0; c < 3; ++c) {
        const double e =
            double(one.pixels[c]) - double((palette[q] >> (16 - c * 8)) & 255);
        const auto add = [&](int dx, int dy, double weight) {
          if (x + dx >= 0 && x + dx < width && y + dy < height)
            errors[size_t(y + dy) * width + x + dx][c] += e * weight;
        };
        add(1, 0, 7.0 / 16);
        add(-1, 1, 3.0 / 16);
        add(0, 1, 5.0 / 16);
        add(1, 1, 1.0 / 16);
      }
    }
  return raster(output, width, height, palette, transparent);
}
Graphics::Graphics(const fs::path &root)
    : root_(fs::absolute(fs::is_regular_file(root) ? root.parent_path()
                                                   : root)) {}
void Graphics::capture(World &world) {
  slots_.clear();
  files_.clear();
  const auto palette_file = find(root_, "M3PC.COL");
  if (palette_file.empty())
    invalid("M3PC.COL missing");
  palette_ = read_palette(read_file(palette_file));
  std::map<std::string, std::string> names;
  for (const auto &group : {"KB", "W"}) {
    const auto dir = find(root_, group);
    if (dir.empty())
      continue;
    for (const auto &entry : fs::directory_iterator(dir)) {
      if (!entry.is_regular_file() ||
          folded(utf8(entry.path().extension())) != ".vga")
        continue;
      const auto stem = utf8(entry.path().stem());
      if (stem.empty() ||
          !std::all_of(stem.begin(), stem.end(),
                       [](unsigned char c) { return std::isdigit(c); }))
        continue;
      if (entry.is_symlink())
        invalid("symlink VGA");
      const auto name = folded(group) + '/' + stem;
      const auto relative = utf8(entry.path().lexically_relative(root_));
      File f;
      f.original = read_file(entry.path(), 32 * 1024 * 1024);
      f.image = decode_vga(f.original);
      if (!f.image.suffix.empty() && f.image.suffix.size() < 768)
        invalid("truncated VGA palette");
      f.palette = f.image.suffix.size() >= 768 ? read_palette(f.image.suffix)
                                               : palette_;
      if (!names.emplace(name, relative).second)
        invalid("duplicate VGA atlas");
      files_.emplace(relative, std::move(f));
    }
  }
  std::map<std::string, Json> resources;
  if (world.store->contains("assets/classic/resources.json")) {
    const auto index =
        Json::parse(world.store->read("assets/classic/resources.json"));
    for (const auto &row : index.value("images", Json::array()))
      resources[str(row, "name")] = row;
  }
  auto bind = [&](const std::string &key, const std::string &name,
                  const std::string &atlas, int sx, int sy, int w, int h,
                  bool transparent, bool whole) {
    const auto resource = resources.find(name);
    const auto f = names.find(atlas);
    if (!world.find("assets", key) || resource == resources.end() ||
        f == names.end())
      return;
    const auto &image = files_.at(f->second).image;
    Slot slot;
    slot.file = f->second;
    slot.start = sy * image.width + sx;
    slot.width = w;
    slot.height = h;
    slot.transparent = transparent;
    slot.whole = whole;
    slot.palette = whole ? files_.at(f->second).palette : palette_;
    slot.baseline = extract(image, slot.start, w, h);
    slot.pixels = str(resource->second, "pixels");
    const auto imported = world.store->read(slot.pixels);
    if (imported.size() != slot.baseline.size() ||
        !std::equal(slot.baseline.begin(), slot.baseline.end(),
                    imported.begin(),
                    [](uint8_t a, char b) { return a == uint8_t(b); }))
      invalid("imported image does not match physical slot");
    slot.png = world.asset_file(key);
    slot.baseline_png = world.store->read(slot.png);
    slots_[key] = std::move(slot);
  };
  for (const auto &[name, file] : names) {
    const auto &im = files_.at(file).image;
    bind("asset:classic/" + name, name, name, 0, 0, im.width, im.height, false,
         true);
  }
  for (const auto &club : world.rows("clubs")) {
    const auto legacy = club.value("x-legacy", Json::object());
    if (!legacy.contains("team_index") || !legacy.contains("badge_word"))
      continue;
    const int team = legacy.at("team_index").get<int>(),
              selector = legacy.at("badge_word").get<int>() & 511;
    const bool neutral =
        (team >= 90 && team < 138) || (team >= 168 && team < 357);
    bind(str(club, "crest"), "crests/" + std::to_string(team),
         neutral ? "kb/0" : "w/" + std::to_string(selector >> 3),
         neutral ? 8 : (selector & 7) * 38, 0, 38, 38, true, false);
    if (neutral && slots_.contains(str(club, "crest")))
      slots_.at(str(club, "crest")).shared_fallback = true;
  }
  for (int i = 0; i < 4; ++i)
    bind("asset:portrait/bmh-classic/background/" + std::to_string(i),
         "portraits/background-" + std::to_string(i), "kb/29", 108 + i * 38,
         168, 38, 38, false, false);
  // Runtime geometry is read only. It does not establish executable file slots.
  const auto german = find(root_, "BMH.EXE"), english = find(root_, "FLTD.EXE");
  if (!german.empty() || !english.empty()) {
    const bool en = str(world.manifest, "provenance") == "import:fltd";
    const auto exe = en ? bmh::launch::translate_fltd(english, false)
                        : bmh::load_le(german, false);
    for (int layer = 0; layer < 5; ++layer) {
      const int startx = exe.u8(
          bmh::layout::ClubPlayerDetail_DrawPortrait_RuntimeField + layer);
      int sx = startx,
          sy = exe.u8(bmh::layout::ClubPlayerDetail_DrawPortrait_RuntimeField2 +
                      layer);
      const int count = exe.u8(
                    bmh::layout::
                        ClubPlayerDetail_ClubPlayerDetailRedraw_RuntimeField2 +
                    layer),
                w = exe.u8(
                    bmh::layout::ClubPlayerDetail_DrawPortrait_RuntimeField3 +
                    layer),
                h = exe.u8(
                    bmh::layout::ClubPlayerDetail_DrawPortrait_RuntimeField4 +
                    layer);
      for (int part = 0; part < count; ++part) {
        const auto suffix = std::to_string(layer) + '/' + std::to_string(part);
        bind("asset:portrait/bmh-classic/layer/" + suffix,
             "portraits/layer-" + std::to_string(layer) + '-' +
                 std::to_string(part),
             "kb/29", sx, sy, w, h, true, false);
        sx += w;
        if (sx >
            exe.u16(bmh::layout::ClubPlayerDetail_DrawPortrait_RuntimeField7 +
                    layer * 2)) {
          sx = startx;
          sy += h;
        }
      }
    }
  }
  for (auto &[key, slot] : slots_) {
    if (auto *asset = world.find("assets", key)) {
      (*asset)["width"] = slot.width;
      (*asset)["height"] = slot.height;
      std::string caption = slot.file;
      if (!slot.whole)
        caption += " · " + key.substr(key.find(':') + 1);
      (*asset)["name"] = {{"default", caption}};
    }
    for (const auto &club : world.rows("clubs"))
      if (str(club, "crest") == key)
        slot.usages.push_back(
            Json{{"table", "clubs"}, {"id", str(club, "id")}});
    for (const auto &[other, alias] : slots_)
      if (other != key && alias.file == slot.file && !alias.whole &&
          overlaps(slot.start, slot.width, slot.height, alias.start,
                   alias.width, alias.height,
                   files_.at(slot.file).image.width)) {
        slot.usages.push_back(Json{{"asset", other}});
      }
  }
}
bool Graphics::supports(const std::string &key) const {
  return slots_.contains(key);
}
Json Graphics::info(const std::string &key) const {
  const auto it = slots_.find(key);
  if (it == slots_.end())
    return {{"writable", false}, {"reason", "No verified original image slot"}};
  const auto &s = it->second;
  bool shared = s.shared_fallback;
  for (const auto &u : s.usages)
    if (u.contains("asset"))
      shared = true;
  return {{"writable", true},
          {"file", s.file},
          {"width", s.width},
          {"height", s.height},
          {"linear_start", s.start},
          {"stride", files_.at(s.file).image.width},
          {"transparent_index", s.transparent ? Json(0) : Json()},
          {"usages", s.usages},
          {"shared_fallback", s.shared_fallback},
          {"requires_confirmation", shared},
          {"embedded_verified_slots", 0}};
}
std::string Graphics::target_key(const World &world, const Json &args) const {
  const auto target = args.value("target", Json::object());
  std::string key;
  if (!target.empty()) {
    if (str(target, "table") != "clubs" ||
        str(target, "field", "crest") != "crest")
      throw Error("legacy_image_target_read_only",
                  {{"reason",
                    "Individual portrait photos have no original file slot"}});
    const auto club = world.find("clubs", str(target, "id"));
    if (!club)
      throw Error("record_missing");
    key = str(*club, "crest");
  } else
    key = str(args, "key", str(args, "source_key"));
  if (!supports(key))
    throw Error("legacy_image_target_read_only",
                {{"key", key}, {"reason", "No verified original image slot"}});
  const auto details = info(key);
  if (details.value("requires_confirmation", false) &&
      !args.value("confirm_shared", false))
    throw Error("legacy_shared_graphic_confirmation", details);
  return key;
}
images::Raster Graphics::convert(const World &, const std::string &key,
                                 const images::Raster &source,
                                 const Json &recipe) const {
  const auto it = slots_.find(key);
  if (it == slots_.end())
    throw Error("legacy_image_target_read_only");
  const auto &s = it->second;
  return convert_indexed(source, s.width, s.height, s.palette, s.transparent,
                         recipe);
}
std::map<std::string, std::string> Graphics::prepare(const World &world) const {
  std::map<std::string, IndexedVga> changed;
  std::map<std::pair<std::string, size_t>, uint8_t> writes;
  for (const auto &[key, s] : slots_) {
    const auto png = world.asset_file(key);
    if (png.empty())
      invalid("bound image removed");
    const auto data = world.store->read(png);
    if (data == s.baseline_png)
      continue;
    const auto image =
        images::decode(data, folded(utf8(path(png).extension())));
    if (image.width != s.width || image.height != s.height)
      invalid("edited image dimensions differ from legacy slot");
    const auto quantized =
        convert_indexed(image, s.width, s.height, s.palette, s.transparent);
    if (quantized.pixels != image.pixels)
      invalid("image must be converted to its original palette before saving");
    auto pixel = indices(quantized, s.palette, s.transparent);
    // Duplicate palette colors must not rewrite unchanged physical indices.
    const auto base_rgba =
        raster(s.baseline, s.width, s.height, s.palette, s.transparent);
    for (size_t i = 0; i < pixel.size(); ++i)
      if (std::equal(image.pixels.begin() + i * 4,
                     image.pixels.begin() + i * 4 + 4,
                     base_rgba.pixels.begin() + i * 4))
        pixel[i] = s.baseline[i];
    const auto &original = files_.at(s.file).image;
    for (int y = 0; y < s.height; ++y)
      for (int x = 0; x < s.width; ++x) {
        const size_t local = size_t(y) * s.width + x;
        if (pixel[local] == s.baseline[local])
          continue;
        const size_t at = s.start + size_t(y) * original.width + x;
        const auto [it, inserted] =
            writes.emplace(std::make_pair(s.file, at), pixel[local]);
        if (!inserted && it->second != pixel[local])
          invalid("conflicting edits to shared graphic pixels");
        if (!changed.contains(s.file))
          changed[s.file] = original;
        changed.at(s.file).pixels.at(at) = pixel[local];
      }
  }
  std::map<std::string, std::string> result;
  for (const auto &[name, image] : changed)
    result[name] = encode_vga(image);
  return result;
}
void Graphics::mark_saved(World &world) {
  const auto changes = prepare(world);
  for (const auto &[name, bytes] : changes) {
    auto &file = files_.at(name);
    file.original = bytes;
    file.image = decode_vga(bytes);
  }
  for (auto &[key, s] : slots_) {
    s.baseline = extract(files_.at(s.file).image, s.start, s.width, s.height);
    // All views of a physical sheet must follow its merged pixels. Otherwise
    // editing a formerly stale alias after saving can revert a neighbour's
    // edit.
    s.png = world.asset_file(key);
    const auto current = world.store->read(s.png);
    const auto merged =
        raster(s.baseline, s.width, s.height, s.palette, s.transparent);
    const auto projected =
        images::decode(current, folded(utf8(path(s.png).extension())));
    s.baseline_png = projected.width == merged.width &&
                             projected.height == merged.height &&
                             projected.pixels == merged.pixels
                         ? current
                         : images::encode_png(merged);
    world.store->set(s.png,
                     std::make_shared<const std::string>(s.baseline_png));
    world.store->set(s.pixels,
                     std::make_shared<const std::string>(
                         reinterpret_cast<const char *>(s.baseline.data()),
                         s.baseline.size()));
  }
}
} // namespace bmh::editor::legacy
