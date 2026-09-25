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

#include <list>
#include <map>
#include <stack>
#include <string>
#include <type_traits>

#include "gmanlog.h"
#include "gmanmath.h"
#include "gmantypes.h"
#include "ri.h"

/*
 * RenderMan API GMANColor
 *
 * A simple color object...The atomic color object
 * used in this rendering system.
 *
 */

template <class SampleType> class GMANColorBase {
public:
  typedef SampleType ColorSampleType;

  // A transfer curve -- gamma, and the clamping the drivers narrow through --
  // is defined over normalized floating-point samples. A byte instantiation
  // answers false, and the code that needs the guarantee asserts on this.
  static constexpr bool hasFloatingPointSamples = std::is_floating_point_v<SampleType>;

protected:
  /* Value-initialized: the default constructor below left these indeterminate,
   * so a default-constructed colour carried garbage and gcc flagged every use
   * of one under -Werror=maybe-uninitialized. Black is the sane default. */
  SampleType r{}; // red
  SampleType g{}; // green
  SampleType b{}; // blue

public:
  GMANColorBase() {}; // default constructor

  // set each of the color values on construction
  GMANColorBase(SampleType rval, SampleType gval, SampleType bval) {

    r = rval;
    g = gval;
    b = bval;
  };

  GMANColorBase(SampleType sval) { r = g = b = sval; };

  // Copy, move and destruction are the implicit ones. Spelling them out cost
  // the class its triviality, and a class non-trivial for the purposes of
  // calls returns through a hidden pointer instead of in registers.

  // less comparison for use by MSVC templates
  bool operator<(const GMANColorBase& c) const { return ((r < c.r) && (g < c.g) && (b < c.b)); }

  // Red values
  inline SampleType getRed(RtVoid) const { return r; };
  inline RtVoid setRed(SampleType rval) { r = rval; };

  // Green values
  inline SampleType getGreen(RtVoid) const { return g; };
  inline RtVoid setGreen(SampleType gval) { g = gval; };

  // Blue values
  inline SampleType getBlue(RtVoid) const { return b; };
  inline RtVoid setBlue(SampleType bval) { b = bval; };

  // add a color value
  GMANColorBase& operator+=(const GMANColorBase& c) {
    r += c.r;
    g += c.g;
    b += c.b;
    return *this;
  };

  // subtract a color value
  GMANColorBase& operator-=(const GMANColorBase& c) {
    r -= c.r;
    g -= c.g;
    b -= c.b;
    return *this;
  };

  // divide the color by value
  GMANColorBase& operator/=(SampleType s) {
    r /= s;
    g /= s;
    b /= s;

    return *this;
  };

  // get max color sample
  SampleType maxSampleValue(RtVoid) {
    SampleType maxval;

    maxval = GMANMAX(r, g);

    maxval = GMANMAX(maxval, b);

    return maxval;
  };

  // scale the color by a value
  RtVoid scale(SampleType s) {
    r *= s;
    g *= s;
    b *= s;
  }
};

// combine two colors
template <class ColorObj, class AlphaObj> struct GMANCombineBase {
  ColorObj operator()(const ColorObj& c1, const ColorObj& c2, const AlphaObj& a) {
    ColorObj res(c1.getRed() + (c2.getRed() - c1.getRed()) * a.getRed(),
                 c1.getGreen() + (c2.getGreen() - c1.getGreen()) * a.getGreen(),
                 c1.getBlue() + (c2.getBlue() - c1.getBlue()) * a.getBlue());

    return res;
  };
};

// default color type
class GMANColor : public GMANColorBase<GMANColorSample> {
public:
  GMANColor() : GMANColorBase<GMANColorSample>() {}; // default constructor

  // set each of the color values on construction
  GMANColor(GMANColorSample rval, GMANColorSample gval, GMANColorSample bval)
      : GMANColorBase<GMANColorSample>(rval, gval, bval) {

        };

  // set each color value with one value
  GMANColor(GMANColorSample sval) : GMANColorBase<GMANColorSample>(sval) {};

  GMANColor(GMANByte rval, GMANByte gval, GMANByte bval)
      : GMANColorBase<GMANColorSample>((GMANColorSample)rval / GMAN_BYTEMAX, (GMANColorSample)gval / GMAN_BYTEMAX,
                                       (GMANColorSample)bval / GMAN_BYTEMAX) {}
};

// default alpha type
class GMANAlpha : public GMANColorBase<GMANColorSample> {
public:
  GMANAlpha() : GMANColorBase<GMANColorSample>() {}; // default constructor

  // set each of the color values on construction
  GMANAlpha(GMANColorSample rval, GMANColorSample gval, GMANColorSample bval)
      : GMANColorBase<GMANColorSample>(rval, gval, bval) {

        };

  // set each color value with one value
  GMANAlpha(GMANColorSample sval) : GMANColorBase<GMANColorSample>(sval) {};

  GMANAlpha(GMANByte rval, GMANByte gval, GMANByte bval)
      : GMANColorBase<GMANColorSample>((GMANColorSample)rval / GMAN_BYTEMAX, (GMANColorSample)gval / GMAN_BYTEMAX,
                                       (GMANColorSample)bval / GMAN_BYTEMAX) {}
};

typedef GMANCombineBase<GMANColor, GMANAlpha> GMANCombine;
typedef GMANCombineBase<GMANAlpha, GMANAlpha> GMANAlphaCombine;
