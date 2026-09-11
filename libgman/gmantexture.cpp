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

#include <cmath>

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

}  // namespace

GMANTexture::GMANTexture(const std::string &name) : width(1), height(1) {
#ifdef HAVE_LIBTIFF
  TIFF *tif = TIFFOpen(name.c_str(), "r");
  if (tif == nullptr) {
    warning("texture \"%s\": cannot open, using opaque black", name.c_str());
    texels = blackTexel();
    return;
  }

  uint32_t w = 0, h = 0;
  TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &w);
  TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &h);

  std::vector<uint32_t> raster(w * h);
  // One path for every photometric layout libtiff knows -- greyscale,
  // palette, RGB, RGBA -- instead of a switch over TIFFTAG_PHOTOMETRIC.
  // ORIENTATION_TOPLEFT: row 0 comes back as the image's top row, which is
  // texture space's own convention -- t=0 is the top, with no flip.
  int ok = TIFFReadRGBAImageOriented(tif, w, h, raster.data(),
                                      ORIENTATION_TOPLEFT, 0);
  TIFFClose(tif);

  if (!ok || w == 0 || h == 0) {
    warning("texture \"%s\": cannot decode, using opaque black",
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
  warning("texture \"%s\": built without libtiff, using opaque black",
          name.c_str());
  texels = blackTexel();
#endif
}

GMANColor GMANTexture::sample(RtFloat s, RtFloat t, GMANTextureWrap wrap)
    const {
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
  RtInt ix0 = wrapIndex(x0, width, wrap, xValid0);
  RtInt ix1 = wrapIndex(x0 + 1, width, wrap, xValid1);
  RtInt iy0 = wrapIndex(y0, height, wrap, yValid0);
  RtInt iy1 = wrapIndex(y0 + 1, height, wrap, yValid1);

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

GMANColor GMANTextureCache::sample(const std::string &name, RtFloat s,
                                    RtFloat t, GMANTextureWrap wrap) {
  std::map<std::string, GMANTexture>::iterator it = textures.find(name);
  if (it == textures.end()) {
    it = textures.emplace(name, GMANTexture(name)).first;
  }
  return it->second.sample(s, t, wrap);
}

GMANTextureCache &gmanTextureCache(RtVoid) {
  static GMANTextureCache cache;
  return cache;
}
