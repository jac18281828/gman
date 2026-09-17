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

#ifndef __GMAN_GMANTEXTURE_H
#define __GMAN_GMANTEXTURE_H 1

#include <map>
#include <string>
#include <vector>

#include "gmancolor.h"
#include "ri.h"

// How a sample outside [0, 1] is resolved, per axis, independently for s
// and t. RiMakeTexture records this per file in libtiff's Pixar
// wrap-modes tag; GMANTexture reads it back at construction, and
// GMANTextureCache::sample applies it per axis.
enum GMANTextureWrap {
  GMAN_TEXTURE_CLAMP,
  GMAN_TEXTURE_PERIODIC,
  GMAN_TEXTURE_BLACK
};

/*
 * One decoded texture: the RGBA pixels of a single named file, and the
 * bilinear sampler over them. A missing, unreadable or unsupported file
 * decodes to a single opaque black texel and warns once, at construction
 * -- GMANTextureCache builds exactly one of these per filename, so the
 * warning fires exactly once no matter how many lookups the name gets.
 *
 * Owns its texels in a std::vector, never a raw pointer: AGENTS.md's Rule
 * of Five is then satisfied by owning nothing raw, and the compiler's
 * implicit copy/move members are correct as written.
 */
class GMAN_EXPORT GMANTexture {
public:
  explicit GMANTexture(const std::string &name);

  // Bilinear sample at (s, t); t=0 is the image's top row. wrap resolves
  // both s and t identically outside [0, 1].
  GMANColor sample(RtFloat s, RtFloat t, GMANTextureWrap wrap) const;

  // Per-axis form: wrap resolves s and t independently. The single-wrap
  // overload above forwards here with the same mode on both axes.
  GMANColor sample(RtFloat s, RtFloat t, GMANTextureWrap swrap,
                    GMANTextureWrap twrap) const;

  // Read from libtiff's Pixar wrap-modes tag at construction; an absent
  // or unparseable tag leaves both clamp. GMANTextureCache reads these
  // for its own sample().
  GMANTextureWrap swrap = GMAN_TEXTURE_CLAMP;
  GMANTextureWrap twrap = GMAN_TEXTURE_CLAMP;

private:
  RtInt width;
  RtInt height;

  std::vector<GMANColor> texels; // row-major, texels[0] is the top-left

  GMANColor texel(RtInt x, RtInt y) const { return texels[y * width + x]; }
};

/*
 * Every texture RiSurface's "texturename" has named, keyed by filename and
 * decoded once -- the gmanLightSourceMgr() idiom. Keyed on the name rather
 * than the shader instance: GMANLoadShader returns one static shader per
 * plugin, so every "paintedplastic" surface in a scene shares one
 * parameter list, and the filename is the only thing that varies.
 */
class GMAN_EXPORT GMANTextureCache {
public:
  // Loads and decodes name on first request; every later request for the
  // same name, hit or miss, reads no file.
  GMANColor sample(const std::string &name, RtFloat s, RtFloat t,
                    GMANTextureWrap wrap);

  // Samples with the texture's own recorded wrap modes.
  GMANColor sample(const std::string &name, RtFloat s, RtFloat t);

  // Drops name from the cache, so the next lookup reads the file again.
  void forget(const std::string &name);

private:
  // Loads and decodes name on first request; every later request for the
  // same name, hit or miss, reads no file. Both sample overloads route
  // through this.
  GMANTexture &entry(const std::string &name);

  std::map<std::string, GMANTexture> textures;
};

// One cache per process, like gmanLightSourceMgr(). A free function rather
// than a member threaded through the shading path, for the same reason:
// shading runs per vertex, far from anything that would otherwise own it.
GMAN_EXPORT GMANTextureCache &gmanTextureCache(RtVoid);

// RiMakeTexture's implementation: decodes picture the way GMANTexture does
// and writes texture as a single-level 8-bit RGB TIFF carrying swrap and
// twrap in libtiff's Pixar wrap-modes tag. Never throws -- warns once,
// naming texture and the cause, and leaves no file at texture, on a null or
// empty name, an unknown wrap name, a picture that cannot be opened or
// decoded, or an output that cannot be opened or fully written. A
// successful write forgets texture from gmanTextureCache(), so the next
// lookup reads what was just written.
GMAN_EXPORT bool gmanMakeTexture(const char *picture, const char *texture,
                                  const char *swrap, const char *twrap);

// RiMakeLatLongEnvironment's implementation: decodes picture and writes
// texture as a latitude-longitude environment map, RISpec 3.2 Sec 7.1.2 --
// "periodic,clamp" in the Pixar wrap-modes tag (longitude 0 and 360 meet
// without a seam; latitude clamps at the poles) and "LatLong Environment"
// in the Pixar texture-format tag, RenderMan tools' value for this format.
// Same failure shape as gmanMakeTexture: never throws, warns once naming
// texture and the cause, and leaves no file at texture on a null or empty
// name, a picture that cannot be opened or decoded, or an output that
// cannot be opened or fully written. A successful write forgets texture
// from gmanTextureCache(), so the next lookup reads what was just written.
GMAN_EXPORT bool gmanMakeLatLongEnvironment(const char *picture,
                                             const char *texture);

#endif
