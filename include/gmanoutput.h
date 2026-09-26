/* SPDX-License-Identifier: LGPL-2.1-or-later */

/* This is part of GMAN, a RenderMan-compatible renderer.
 *
 * Copyright (c) 2001, 2000, 1999 John Cairns
 *
 * Author: John Cairns <john@2ad.com>
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

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "gmanframebuffer.h"
#include "gmangamma.h"
#include "gmanlog.h"
#include "gmanquantize.h"
#include "ri.h"

/*
 * RenderMan API GMANOutput
 *
 * An interface for output of a frame buffer.
 * ... the frame buffer is detailed in the Renderer,
 * and its contents can be stored with an output
 *
 */

class GMANOutput : public GMANFrameBuffer {
public:
  // public types
  typedef enum { RGB, RGBA, RGBAZ, A, AZ, Z } DisplayMode;

protected:
  std::string outputName;

public:
  GMANOutput(); // default constructor

  // name, width, height
  GMANOutput(const char* name, int width, int height);

  // path, width, height, background

  GMANOutput(const char* name, int width, int height, const GMANColor& background);

  virtual ~GMANOutput(); // default destructor

  // set the output name
  virtual RtVoid setName(const char* name) { outputName = name; };

  // The widest bit depth a sample this driver writes can hold. 8 unless a
  // subclass writes more.
  virtual int maxBitsPerSample() const;

  // Resolves quantize against maxBitsPerSample(): honoured, it bounds every
  // channel; otherwise save logs one warning naming this output and
  // quantizes with 255 0 255 and the requested amplitude instead. Colour
  // runs through gain and gamma first; alpha, coverage, skips both. Every
  // channel then rounds and dithers by RISpec's rule, one draw per pixel,
  // and clamps into the resolved range, before writeImage receives it
  // once, at the resolved bit depth.
  RtVoid save(DisplayMode mode, RtFloat gain, RtFloat gamma, GMANQuantize const& quantize);

protected:
  // Writes image, 4 samples per pixel -- R, G, B, alpha -- in row-major
  // order, index 4 * (y * xres + x) + channel, each already resolved into
  // the request's [min, max] and packed into bitsPerSample bits (8 or 16).
  // gamma is the exponent save already applied, passed through for a
  // format that records it rather than reapplies it.
  virtual RtVoid writeImage(DisplayMode mode, std::vector<std::uint16_t> const& samples, int bitsPerSample,
                            RtFloat gamma) = 0;
};
