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

#include "gmancolor.h"
#include "gmanframebuffer.h"
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

  // Whether this driver writes unquantized floating-point samples. False
  // unless a subclass does.
  virtual bool writesFloat() const;

  // A quantize.one of 0 asks for unquantized output. A driver whose
  // writesFloat() is true then receives, once, through writeFloatImage,
  // colour after gain and gamma and alpha, the coverage, all before any
  // rounding, dithering or clamping: min, max and the dither amplitude do
  // not apply, and a NaN reads 0. Any other driver, and any other
  // quantize.one, takes the integer path.
  //
  // The integer path honours an explicit request the driver can store, and
  // stops the file with GMANError at RIE_ERROR, before anything is written,
  // for one it cannot: a negative one or min, a min above max, a dither
  // amplitude that is negative or not finite, a max above what
  // maxBitsPerSample() holds, or a one of 0 from a driver that writes no
  // float. Colour runs through gain and gamma first; alpha, coverage, skips
  // both. Every channel then rounds and dithers by RISpec's rule, one draw
  // per pixel, and clamps into [min, max], before writeImage receives it
  // once, at 16 bits when max exceeds 255 and 8 otherwise.
  RtVoid save(DisplayMode mode, RtFloat gain, RtFloat gamma, GMANQuantize const& quantize);

protected:
  // Writes image, 4 samples per pixel -- R, G, B, alpha -- in row-major
  // order, index 4 * (y * xres + x) + channel, as floats: colour after gain
  // and gamma, alpha the coverage, none of them rounded, dithered or
  // clamped. gamma is the exponent save already applied. Called only when
  // writesFloat() is true; the default writes nothing.
  virtual RtVoid writeFloatImage(DisplayMode mode, std::vector<float> const& samples, RtFloat gamma);

  // Writes image, 4 samples per pixel -- R, G, B, alpha -- in row-major
  // order, index 4 * (y * xres + x) + channel, each already resolved into
  // the request's [min, max] and packed into bitsPerSample bits (8 or 16).
  // gamma is the exponent save already applied; no shipped driver records
  // it.
  virtual RtVoid writeImage(DisplayMode mode, std::vector<std::uint16_t> const& samples, int bitsPerSample,
                            RtFloat gamma) = 0;
};
