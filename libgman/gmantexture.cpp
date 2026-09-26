/* SPDX-License-Identifier: LGPL-2.1-or-later */

/* This is part of GMAN, a RenderMan-compatible renderer.
 *
 * Copyright (c) 2026 John Cairns <john@2ad.com>
 */

/*
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
 */

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

#include "gmanlog.h"
#include "gmantexture.h"
#include "gmantiff.h"

namespace gman {

namespace {

// A single opaque black texel: what a missing, unreadable or unsupported
// file decodes to.
std::vector<GMANColor> blackTexel() {
  return std::vector<GMANColor>(1, GMANColor((RtFloat)0.0, (RtFloat)0.0, (RtFloat)0.0));
}

// Resolves one axis index against wrap, reporting whether the resolved
// texel is real (clamp, periodic) or stands in for the wrap's own "outside
// the image" colour (black). Uniform across both axes and both filter
// taps, so a coordinate anywhere outside [0, 1] -- not just past the
// image's outer edge -- already gets clamp/periodic/black's own answer
// with no separate top-level check.
RtInt wrapIndex(RtInt i, RtInt dim, TextureWrap wrap, bool& valid) {
  valid = true;
  switch (wrap) {
  case TEXTURE_PERIODIC: {
    RtInt m = i % dim;
    return m < 0 ? m + dim : m;
  }
  case TEXTURE_BLACK:
    if (i < 0 || i >= dim) {
      valid = false;
      return 0;
    }
    return i;
  case TEXTURE_CLAMP:
  default:
    if (i < 0)
      return 0;
    if (i >= dim)
      return dim - 1;
    return i;
  }
}

// Matches a wrap name case-insensitively, the way gmanribparse.cpp's
// filterByName matches a pixel filter.
bool wrapByName(const std::string& name, TextureWrap& wrap) {
  std::string lower = name;
  for (std::string::size_type i = 0; i < lower.size(); ++i) {
    lower[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(lower[i])));
  }
  if (lower == "periodic") {
    wrap = TEXTURE_PERIODIC;
    return true;
  }
  if (lower == "clamp") {
    wrap = TEXTURE_CLAMP;
    return true;
  }
  if (lower == "black") {
    wrap = TEXTURE_BLACK;
    return true;
  }
  return false;
}

// Parses libtiff's Pixar wrap-modes tag's "<swrap>,<twrap>" -- the form
// libtiff's own texture tools write. False, leaving swrap/twrap
// untouched, unless both halves name a known wrap mode.
bool parseWrapModes(const char* tag, TextureWrap& swrap, TextureWrap& twrap) {
  const auto joined = std::string(tag);
  const auto comma = joined.find(',');
  if (comma == std::string::npos) {
    return false;
  }
  TextureWrap s, t;
  if (!wrapByName(joined.substr(0, comma), s) || !wrapByName(joined.substr(comma + 1), t)) {
    return false;
  }
  swrap = s;
  twrap = t;
  return true;
}

// RISpec's wrap names, written lower-case -- the inverse of wrapByName.
const char* wrapName(TextureWrap wrap) {
  switch (wrap) {
  case TEXTURE_PERIODIC:
    return "periodic";
  case TEXTURE_BLACK:
    return "black";
  case TEXTURE_CLAMP:
  default:
    return "clamp";
  }
}

// Writes rgb (w*h pixels, three bytes each, top-left oriented) to name as
// a single-level 8-bit RGB TIFF carrying sw/tw in the Pixar wrap-modes tag
// and format in the Pixar texture-format tag. False, removing any partial
// file, if the file cannot be opened or a scanline fails to write.
bool writeTexture(const std::string& name, uint32_t w, uint32_t h, const std::vector<unsigned char>& rgb,
                  TextureWrap sw, TextureWrap tw, const std::string& format) {
  TIFFWriter writer(name, w, h, 3, 8, OutputTIFF::NONE);
  if (!writer.isOpen()) {
    return false;
  }

  writer.setRowsPerStrip(1);
  const std::string wrapModes = std::string(wrapName(sw)) + "," + wrapName(tw);
  writer.setWrapModes(wrapModes);
  writer.setTextureFormat(format);

  std::vector<unsigned char> row(w * 3);
  bool writeOk = true;
  for (uint32_t y = 0; y < h && writeOk; ++y) {
    std::copy(rgb.begin() + (std::size_t)y * w * 3, rgb.begin() + (std::size_t)(y + 1) * w * 3, row.begin());
    writeOk = writer.writeScanline(row.data(), y);
  }

  if (!writeOk) {
    std::remove(name.c_str());
  }
  return writeOk;
}

// Opens pictureName and decodes it into w/h/rgb, the shape makeTexture and
// makeLatLongEnvironment share. who names the caller ("MakeTexture",
// "MakeLatLongEnvironment") in a failure's warning; each keeps its own
// exact message text otherwise. False, w/h/rgb left as decode leaves them,
// on either failure.
bool openAndDecodePicture(const char* who, const std::string& pictureName, const std::string& textureName, uint32_t& w,
                          uint32_t& h, std::vector<unsigned char>& rgb) {
  TIFFReader reader(pictureName);
  if (!reader.isOpen()) {
    warning("{} \"{}\": cannot open picture \"{}\"", who, textureName.c_str(), pictureName.c_str());
    return false;
  }

  if (!reader.decode(w, h, rgb)) {
    warning("{} \"{}\": cannot decode picture \"{}\"", who, textureName.c_str(), pictureName.c_str());
    return false;
  }

  return true;
}

} // namespace

Texture::Texture(const std::string& name) : width(1), height(1) {
  TIFFReader reader(name);
  if (!reader.isOpen()) {
    warning("texture \"{}\": cannot open, using opaque black", name.c_str());
    texels = blackTexel();
    return;
  }

  const auto wrapModes = reader.wrapModes();
  if (wrapModes && !parseWrapModes(wrapModes->c_str(), swrap, twrap)) {
    warning("texture \"{}\": unrecognized wrap modes \"{}\", using clamp", name.c_str(), wrapModes->c_str());
  }

  uint32_t w = 0, h = 0;
  std::vector<unsigned char> rgb;
  // One path for every photometric layout libtiff knows -- greyscale,
  // palette, RGB, RGBA -- instead of a switch over the photometric tag.
  // Top-left oriented: row 0 comes back as the image's top row, which is
  // texture space's own convention -- t=0 is the top, with no flip.
  bool ok = reader.decode(w, h, rgb);

  if (!ok) {
    warning("texture \"{}\": cannot decode, using opaque black", name.c_str());
    texels = blackTexel();
    return;
  }

  width = (RtInt)w;
  height = (RtInt)h;
  texels.reserve((std::size_t)width * (std::size_t)height);
  for (std::size_t i = 0; i < rgb.size(); i += 3) {
    texels.push_back(GMANColor((RtFloat)rgb[i + 0] / (RtFloat)255.0, (RtFloat)rgb[i + 1] / (RtFloat)255.0,
                               (RtFloat)rgb[i + 2] / (RtFloat)255.0));
  }
}

GMANColor Texture::sample(RtFloat s, RtFloat t, TextureWrap wrap) const { return sample(s, t, wrap, wrap); }

GMANColor Texture::sample(RtFloat s, RtFloat t, TextureWrap swrap, TextureWrap twrap) const {
  // Texel centres sit at (i + 0.5) / dim; solving that for i turns (s, t)
  // into a coordinate where an integer means "exactly this texel's
  // centre" and a half-integer means "exactly between two centres" --
  // what the direct cache assertions in tests/texture_test.cpp pin.
  RtFloat x = s * (RtFloat)width - (RtFloat)0.5;
  RtFloat y = t * (RtFloat)height - (RtFloat)0.5;
  RtInt x0 = (RtInt)std::floor(x);
  RtInt y0 = (RtInt)std::floor(y);
  RtFloat fx = x - (RtFloat)x0;
  RtFloat fy = y - (RtFloat)y0;

  bool xValid0, xValid1, yValid0, yValid1;
  RtInt ix0 = wrapIndex(x0, width, swrap, xValid0);
  RtInt ix1 = wrapIndex(x0 + 1, width, swrap, xValid1);
  RtInt iy0 = wrapIndex(y0, height, twrap, yValid0);
  RtInt iy1 = wrapIndex(y0 + 1, height, twrap, yValid1);

  const GMANColor black((RtFloat)0.0, (RtFloat)0.0, (RtFloat)0.0);
  GMANColor c00 = (xValid0 && yValid0) ? texel(ix0, iy0) : black;
  GMANColor c10 = (xValid1 && yValid0) ? texel(ix1, iy0) : black;
  GMANColor c01 = (xValid0 && yValid1) ? texel(ix0, iy1) : black;
  GMANColor c11 = (xValid1 && yValid1) ? texel(ix1, iy1) : black;

  RtFloat w00 = (1 - fx) * (1 - fy);
  RtFloat w10 = fx * (1 - fy);
  RtFloat w01 = (1 - fx) * fy;
  RtFloat w11 = fx * fy;

  return GMANColor(w00 * c00.getRed() + w10 * c10.getRed() + w01 * c01.getRed() + w11 * c11.getRed(),
                   w00 * c00.getGreen() + w10 * c10.getGreen() + w01 * c01.getGreen() + w11 * c11.getGreen(),
                   w00 * c00.getBlue() + w10 * c10.getBlue() + w01 * c01.getBlue() + w11 * c11.getBlue());
}

Texture& TextureCache::entry(const std::string& name) {
  std::map<std::string, Texture>::iterator it = textures.find(name);
  if (it == textures.end()) {
    it = textures.emplace(name, Texture(name)).first;
  }
  return it->second;
}

GMANColor TextureCache::sample(const std::string& name, RtFloat s, RtFloat t, TextureWrap wrap) {
  return entry(name).sample(s, t, wrap);
}

GMANColor TextureCache::sample(const std::string& name, RtFloat s, RtFloat t) {
  Texture& tex = entry(name);
  return tex.sample(s, t, tex.swrap, tex.twrap);
}

void TextureCache::forget(const std::string& name) { textures.erase(name); }

TextureCache& textureCache(RtVoid) {
  static TextureCache cache;
  return cache;
}

bool makeTexture(const char* picture, const char* texture, const char* swrap, const char* twrap) {
  const std::string textureName = texture != nullptr ? texture : "";
  if (textureName.empty()) {
    warning("MakeTexture: empty texture name, nothing written");
    return false;
  }

  TextureWrap sw, tw;
  if (!wrapByName(swrap != nullptr ? swrap : "", sw) || !wrapByName(twrap != nullptr ? twrap : "", tw)) {
    warning("MakeTexture \"{}\": unknown wrap mode \"{}\",\"{}\"", textureName.c_str(), swrap != nullptr ? swrap : "",
            twrap != nullptr ? twrap : "");
    return false;
  }

  const std::string pictureName = picture != nullptr ? picture : "";
  uint32_t w = 0, h = 0;
  std::vector<unsigned char> rgb;
  // Same decode call Texture's own constructor uses: a texture made
  // from a picture samples identically to that picture at every texel
  // centre.
  if (!openAndDecodePicture("MakeTexture", pictureName, textureName, w, h, rgb)) {
    return false;
  }

  if (!writeTexture(textureName, w, h, rgb, sw, tw, "Plain Texture")) {
    warning("MakeTexture \"{}\": failed to write", textureName.c_str());
    return false;
  }

  textureCache().forget(textureName);
  return true;
}

bool makeLatLongEnvironment(const char* picture, const char* texture) {
  const std::string textureName = texture != nullptr ? texture : "";
  if (textureName.empty()) {
    warning("MakeLatLongEnvironment: empty texture name, nothing written");
    return false;
  }

  const std::string pictureName = picture != nullptr ? picture : "";
  uint32_t w = 0, h = 0;
  std::vector<unsigned char> rgb;
  if (!openAndDecodePicture("MakeLatLongEnvironment", pictureName, textureName, w, h, rgb)) {
    return false;
  }

  // RISpec 3.2 Sec 7.1.2: periodic in s so longitude 0 and 360 meet
  // without a seam, clamp in t so the poles hold their edge row/texel.
  if (!writeTexture(textureName, w, h, rgb, TEXTURE_PERIODIC, TEXTURE_CLAMP, "LatLong Environment")) {
    warning("MakeLatLongEnvironment \"{}\": failed to write", textureName.c_str());
    return false;
  }

  textureCache().forget(textureName);
  return true;
}

} // namespace gman
