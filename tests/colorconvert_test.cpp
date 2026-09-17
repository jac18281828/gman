/* SPDX-License-Identifier: LGPL-2.1-or-later
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

#include "check.h"
#include "gmancolor.h"

namespace {

void checkHSV(const char* what, RtFloat r, RtFloat g, RtFloat b, RtFloat expectH, RtFloat expectS, RtFloat expectV) {
  RtFloat c[3] = {r, g, b};
  GMANConvertRGBtoHSV(c);
  check(c[0] == expectH && c[1] == expectS && c[2] == expectV, what);
}

void checkHSL(const char* what, RtFloat r, RtFloat g, RtFloat b, RtFloat expectH, RtFloat expectL, RtFloat expectS) {
  RtFloat c[3] = {r, g, b};
  GMANConvertRGBtoHSL(c);
  check(c[0] == expectH && c[1] == expectL && c[2] == expectS, what);
}

} // namespace

int main() {
  checkHSV("hsv: r>g>b", 0.75f, 0.5f, 0.25f, 30.0f, 0.666666687f, 0.75f);
  checkHSL("hsl: r>g>b", 0.75f, 0.5f, 0.25f, 30.0f, 0.5f, 0.5f);

  checkHSV("hsv: r>b>g", 0.75f, 0.25f, 0.5f, 330.0f, 0.666666687f, 0.75f);
  checkHSL("hsl: r>b>g", 0.75f, 0.25f, 0.5f, 330.0f, 0.5f, 0.5f);

  checkHSV("hsv: g>r>b", 0.5f, 0.75f, 0.25f, 90.0f, 0.666666687f, 0.75f);
  checkHSL("hsl: g>r>b", 0.5f, 0.75f, 0.25f, 90.0f, 0.5f, 0.5f);

  checkHSV("hsv: g>b>r", 0.25f, 0.75f, 0.5f, 150.0f, 0.666666687f, 0.75f);
  checkHSL("hsl: g>b>r", 0.25f, 0.75f, 0.5f, 150.0f, 0.5f, 0.5f);

  checkHSV("hsv: b>r>g", 0.5f, 0.25f, 0.75f, 270.0f, 0.666666687f, 0.75f);
  checkHSL("hsl: b>r>g", 0.5f, 0.25f, 0.75f, 270.0f, 0.5f, 0.5f);

  checkHSV("hsv: b>g>r", 0.25f, 0.5f, 0.75f, 210.0f, 0.666666687f, 0.75f);
  checkHSL("hsl: b>g>r", 0.25f, 0.5f, 0.75f, 210.0f, 0.5f, 0.5f);

  checkHSV("hsv: tie at the top", 0.75f, 0.75f, 0.25f, 60.0f, 0.666666687f, 0.75f);
  checkHSL("hsl: tie at the top", 0.75f, 0.75f, 0.25f, 60.0f, 0.5f, 0.5f);

  checkHSV("hsv: tie at the bottom", 0.75f, 0.25f, 0.25f, 0.0f, 0.666666687f, 0.75f);
  checkHSL("hsl: tie at the bottom", 0.75f, 0.25f, 0.25f, 0.0f, 0.5f, 0.5f);

  checkHSV("hsv: black", 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
  checkHSV("hsv: mid gray", 0.5f, 0.5f, 0.5f, 0.0f, 0.0f, 0.5f);

  return checkSummary("colorconvert holds");
}
