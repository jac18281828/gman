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

#ifdef HAVE_LIBTIFF
extern "C" {
#include <tiffio.h>
}
#endif

#include <cctype>
#include <cmath>
#include <cstdio>

#include "gmantexture.h"
#include "gmanlog.h"

namespace {

// A single opaque black texel: what a missing, unreadable or unsupported
// file, or a GMAN_WITH_TIFF=OFF build, decodes to.
std::vector<GMANColor> blackTexel() {
  return std::vector<GMANColor>(1, GMANColor((RtFloat) 0.0, (RtFloat) 0.0,
                                              (RtFloat) 0.0));
}

// Resolves one axis index against wrap, reporting whether the resolved
// texel is real (clamp, periodic) or stands in for the wrap's own "outside
// the image" colour (black). Uniform across both axes and both filter
// taps, so a coordinate anywhere outside [0, 1] -- not just past the
// image's outer edge -- already gets clamp/periodic/black's own answer
// with no separate top-level check.
RtInt wrapIndex(RtInt i, RtInt dim, GMANTextureWrap wrap, bool &valid) {
  valid = true;
  switch (wrap) {
    case GMAN_TEXTURE_PERIODIC: {
      RtInt m = i % dim;
      return m < 0 ? m + dim : m;
    }
    case GMAN_TEXTURE_BLACK:
      if (i < 0 || i >= dim) {
        valid = false;
        return 0;
      }
      return i;
    case GMAN_TEXTURE_CLAMP:
    default:
      if (i < 0) return 0;
      if (i >= dim) return dim - 1;
      return i;
  }
}

#ifdef HAVE_LIBTIFF
// Matches a wrap name case-insensitively, the way gmanribparse.cpp's
// filterByName matches a pixel filter.
bool wrapByName(const std::string &name, GMANTextureWrap &wrap) {
  std::string lower = name;
  for (std::string::size_type i = 0; i < lower.size(); ++i) {
    lower[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(lower[i])));
  }
  if (lower == "periodic") {
    wrap = GMAN_TEXTURE_PERIODIC;
    return true;
  }
  if (lower == "clamp") {
    wrap = GMAN_TEXTURE_CLAMP;
    return true;
  }
  if (lower == "black") {
    wrap = GMAN_TEXTURE_BLACK;
    return true;
  }
  return false;
}

// Parses TIFFTAG_PIXAR_WRAPMODES's "<swrap>,<twrap>" -- the form libtiff's
// own texture tools write. False, leaving swrap/twrap untouched, unless
// both halves name a known wrap mode.
bool parseWrapModes(const char *tag, GMANTextureWrap &swrap,
                     GMANTextureWrap &twrap) {
  const auto joined = std::string(tag);
  const auto comma = joined.find(',');
  if (comma == std::string::npos) {
    return false;
  }
  GMANTextureWrap s, t;
  if (!wrapByName(joined.substr(0, comma), s) ||
      !wrapByName(joined.substr(comma + 1), t)) {
    return false;
  }
  swrap = s;
  twrap = t;
  return true;
}

// RISpec's wrap names, written lower-case -- the inverse of wrapByName.
const char *wrapName(GMANTextureWrap wrap) {
  switch (wrap) {
    case GMAN_TEXTURE_PERIODIC: return "periodic";
    case GMAN_TEXTURE_BLACK:    return "black";
    case GMAN_TEXTURE_CLAMP:
    default:                    return "clamp";
  }
}

// Decodes an already-open TIFF's pixels the one way this file reads a
// picture: width/height, then TIFFReadRGBAImageOriented into raster. False
// on a failed read or a zero dimension. Shared by GMANTexture's
// constructor and gmanMakeTexture; each keeps its own TIFFOpen/TIFFClose
// and error text around the call.
bool decodeRaster(TIFF *tif, uint32_t &w, uint32_t &h,
                   std::vector<uint32_t> &raster) {
  TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &w);
  TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &h);
  raster.assign((std::size_t) w * (std::size_t) h, 0);
  int ok = TIFFReadRGBAImageOriented(tif, w, h, raster.data(),
                                      ORIENTATION_TOPLEFT, 0);
  return ok != 0 && w > 0 && h > 0;
}

// Writes raster (w*h pixels, packed per TIFFGetR/G/B) to name as a
// single-level 8-bit RGB TIFF carrying sw/tw in TIFFTAG_PIXAR_WRAPMODES.
// False, removing any partial file, if the file cannot be opened or a
// scanline fails to write.
bool writeTexture(const std::string &name, uint32_t w, uint32_t h,
                   const std::vector<uint32_t> &raster, GMANTextureWrap sw,
                   GMANTextureWrap tw) {
  TIFF *dst = TIFFOpen(name.c_str(), "w");
  if (dst == nullptr) {
    return false;
  }

  TIFFSetField(dst, TIFFTAG_IMAGEWIDTH, w);
  TIFFSetField(dst, TIFFTAG_IMAGELENGTH, h);
  TIFFSetField(dst, TIFFTAG_BITSPERSAMPLE, 8);
  TIFFSetField(dst, TIFFTAG_SAMPLESPERPIXEL, 3);
  TIFFSetField(dst, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT);
  TIFFSetField(dst, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB);
  TIFFSetField(dst, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
  TIFFSetField(dst, TIFFTAG_COMPRESSION, COMPRESSION_NONE);
  TIFFSetField(dst, TIFFTAG_ROWSPERSTRIP, 1);
  const std::string wrapModes =
      std::string(wrapName(sw)) + "," + wrapName(tw);
  TIFFSetField(dst, TIFFTAG_PIXAR_WRAPMODES, wrapModes.c_str());
  TIFFSetField(dst, TIFFTAG_PIXAR_TEXTUREFORMAT, "Plain Texture");

  std::vector<unsigned char> row(w * 3);
  bool writeOk = true;
  for (uint32_t y = 0; y < h && writeOk; ++y) {
    for (uint32_t x = 0; x < w; ++x) {
      uint32_t p = raster[y * w + x];
      row[x * 3 + 0] = TIFFGetR(p);
      row[x * 3 + 1] = TIFFGetG(p);
      row[x * 3 + 2] = TIFFGetB(p);
    }
    writeOk = TIFFWriteScanline(dst, row.data(), y, 0) >= 0;
  }
  TIFFClose(dst);

  if (!writeOk) {
    std::remove(name.c_str());
  }
  return writeOk;
}
#endif

}  // namespace

GMANTexture::GMANTexture(const std::string &name) : width(1), height(1) {
#ifdef HAVE_LIBTIFF
  TIFF *tif = TIFFOpen(name.c_str(), "r");
  if (tif == nullptr) {
    warning("texture \"{}\": cannot open, using opaque black", name.c_str());
    texels = blackTexel();
    return;
  }

  char *wrapModes = nullptr;
  if (TIFFGetField(tif, TIFFTAG_PIXAR_WRAPMODES, &wrapModes) &&
      wrapModes != nullptr && !parseWrapModes(wrapModes, swrap, twrap)) {
    warning("texture \"{}\": unrecognized wrap modes \"{}\", using clamp",
            name.c_str(), wrapModes);
  }

  uint32_t w = 0, h = 0;
  std::vector<uint32_t> raster;
  // One path for every photometric layout libtiff knows -- greyscale,
  // palette, RGB, RGBA -- instead of a switch over TIFFTAG_PHOTOMETRIC.
  // ORIENTATION_TOPLEFT: row 0 comes back as the image's top row, which is
  // texture space's own convention -- t=0 is the top, with no flip.
  bool ok = decodeRaster(tif, w, h, raster);
  TIFFClose(tif);

  if (!ok) {
    warning("texture \"{}\": cannot decode, using opaque black",
            name.c_str());
    texels = blackTexel();
    return;
  }

  width = (RtInt) w;
  height = (RtInt) h;
  texels.reserve((std::size_t) width * (std::size_t) height);
  for (uint32_t p : raster) {
    texels.push_back(GMANColor((RtFloat) TIFFGetR(p) / (RtFloat) 255.0,
                                (RtFloat) TIFFGetG(p) / (RtFloat) 255.0,
                                (RtFloat) TIFFGetB(p) / (RtFloat) 255.0));
  }
#else
  warning("texture \"{}\": built without libtiff, using opaque black",
          name.c_str());
  texels = blackTexel();
#endif
}

GMANColor GMANTexture::sample(RtFloat s, RtFloat t, GMANTextureWrap wrap)
    const {
  return sample(s, t, wrap, wrap);
}

GMANColor GMANTexture::sample(RtFloat s, RtFloat t, GMANTextureWrap swrap,
                               GMANTextureWrap twrap) const {
  // Texel centres sit at (i + 0.5) / dim; solving that for i turns (s, t)
  // into a coordinate where an integer means "exactly this texel's
  // centre" and a half-integer means "exactly between two centres" --
  // what the direct cache assertions in tests/texture_test.cpp pin.
  RtFloat x = s * (RtFloat) width - (RtFloat) 0.5;
  RtFloat y = t * (RtFloat) height - (RtFloat) 0.5;
  RtInt x0 = (RtInt) std::floor(x);
  RtInt y0 = (RtInt) std::floor(y);
  RtFloat fx = x - (RtFloat) x0;
  RtFloat fy = y - (RtFloat) y0;

  bool xValid0, xValid1, yValid0, yValid1;
  RtInt ix0 = wrapIndex(x0, width, swrap, xValid0);
  RtInt ix1 = wrapIndex(x0 + 1, width, swrap, xValid1);
  RtInt iy0 = wrapIndex(y0, height, twrap, yValid0);
  RtInt iy1 = wrapIndex(y0 + 1, height, twrap, yValid1);

  const GMANColor black((RtFloat) 0.0, (RtFloat) 0.0, (RtFloat) 0.0);
  GMANColor c00 = (xValid0 && yValid0) ? texel(ix0, iy0) : black;
  GMANColor c10 = (xValid1 && yValid0) ? texel(ix1, iy0) : black;
  GMANColor c01 = (xValid0 && yValid1) ? texel(ix0, iy1) : black;
  GMANColor c11 = (xValid1 && yValid1) ? texel(ix1, iy1) : black;

  RtFloat w00 = (1 - fx) * (1 - fy);
  RtFloat w10 = fx * (1 - fy);
  RtFloat w01 = (1 - fx) * fy;
  RtFloat w11 = fx * fy;

  return GMANColor(
      w00 * c00.getRed() + w10 * c10.getRed() + w01 * c01.getRed() +
          w11 * c11.getRed(),
      w00 * c00.getGreen() + w10 * c10.getGreen() + w01 * c01.getGreen() +
          w11 * c11.getGreen(),
      w00 * c00.getBlue() + w10 * c10.getBlue() + w01 * c01.getBlue() +
          w11 * c11.getBlue());
}

GMANTexture &GMANTextureCache::entry(const std::string &name) {
  std::map<std::string, GMANTexture>::iterator it = textures.find(name);
  if (it == textures.end()) {
    it = textures.emplace(name, GMANTexture(name)).first;
  }
  return it->second;
}

GMANColor GMANTextureCache::sample(const std::string &name, RtFloat s,
                                    RtFloat t, GMANTextureWrap wrap) {
  return entry(name).sample(s, t, wrap);
}

GMANColor GMANTextureCache::sample(const std::string &name, RtFloat s,
                                    RtFloat t) {
  GMANTexture &tex = entry(name);
  return tex.sample(s, t, tex.swrap, tex.twrap);
}

void GMANTextureCache::forget(const std::string &name) {
  textures.erase(name);
}

GMANTextureCache &gmanTextureCache(RtVoid) {
  static GMANTextureCache cache;
  return cache;
}

bool gmanMakeTexture(const char *picture, const char *texture,
                      const char *swrap, const char *twrap) {
#ifdef HAVE_LIBTIFF
  const std::string textureName = texture != nullptr ? texture : "";
  if (textureName.empty()) {
    warning("MakeTexture: empty texture name, nothing written");
    return false;
  }

  GMANTextureWrap sw, tw;
  if (!wrapByName(swrap != nullptr ? swrap : "", sw) ||
      !wrapByName(twrap != nullptr ? twrap : "", tw)) {
    warning("MakeTexture \"{}\": unknown wrap mode \"{}\",\"{}\"",
            textureName.c_str(), swrap != nullptr ? swrap : "",
            twrap != nullptr ? twrap : "");
    return false;
  }

  const std::string pictureName = picture != nullptr ? picture : "";
  TIFF *src = TIFFOpen(pictureName.c_str(), "r");
  if (src == nullptr) {
    warning("MakeTexture \"{}\": cannot open picture \"{}\"",
            textureName.c_str(), pictureName.c_str());
    return false;
  }

  uint32_t w = 0, h = 0;
  std::vector<uint32_t> raster;
  // Same decode call GMANTexture's own constructor uses: a texture made
  // from a picture samples identically to that picture at every texel
  // centre.
  bool decoded = decodeRaster(src, w, h, raster);
  TIFFClose(src);

  if (!decoded) {
    warning("MakeTexture \"{}\": cannot decode picture \"{}\"",
            textureName.c_str(), pictureName.c_str());
    return false;
  }

  if (!writeTexture(textureName, w, h, raster, sw, tw)) {
    warning("MakeTexture \"{}\": failed to write", textureName.c_str());
    return false;
  }

  gmanTextureCache().forget(textureName);
  return true;
#else
  (void) picture;
  (void) swrap;
  (void) twrap;
  warning("MakeTexture \"{}\": built without libtiff, nothing written",
          texture != nullptr ? texture : "");
  return false;
#endif
}
