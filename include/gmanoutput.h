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
  typedef enum {
    RGB = GMANQuantize::RGB,
    RGBA = GMANQuantize::RGBA,
    RGBAZ = GMANQuantize::RGBAZ,
    A = GMANQuantize::A,
    AZ = GMANQuantize::AZ,
    Z = GMANQuantize::Z
  } DisplayMode;

protected:
  std::string outputName;

  GMANQuantize* quantizer;

public:
  GMANOutput();
  ; // default constructor

  // name, width, height
  GMANOutput(const char* name, int width, int height);

  // path, width, height, background

  GMANOutput(const char* name, int width, int height, const GMANColor& background);

  virtual ~GMANOutput(); // default destructor

  // set the output name
  virtual RtVoid setName(const char* name) { outputName = name; };

  // call this to set up the quantization
  virtual RtVoid setQuantization(DisplayMode mode, RtInt one, RtInt min, RtInt max, RtFloat ditheramplitude);

  //
  // Copy or swap the frame buffer bank so the
  // buffer data becomes visible.
  // or save the image data to the display device
  //
  // Runs gamma, then quantize, then a NaN-safe [0, 1] clamp on every pixel
  // in float, then hands the result to writeImage. A driver never narrows a
  // value save has not already put through all three steps.
  RtVoid save(DisplayMode mode, RtFloat gain, RtFloat gamma);

protected:
  // Writes image, xres * yres colours in row-major order (index y * xres +
  // x), already gamma-corrected, quantized and clamped into [0, 1] with no
  // NaN. gamma is the exponent save already applied, passed through for a
  // format that records it rather than reapplies it.
  virtual RtVoid writeImage(DisplayMode mode, std::vector<GMANColor> const& image, RtFloat gamma) = 0;
};
