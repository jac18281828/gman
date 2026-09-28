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

/*
 * gman::OutputPNG writes no gAMA chunk, at the default gamma or any other:
 * PNG's chunk records the file gamma, sample = light^gAMA, while gman
 * encodes the inverse, so a value here would tell a gamma-aware viewer the
 * wrong exponent. Reads the produced files back with libpng directly
 * (tests/ is exempt from AGENTS.md's one-includer rule for png.h).
 */

#include "check.h"

#ifdef GMAN_WITH_PNG

#include <cstdio>
#include <string>

extern "C" {
#include <png.h>
}

#include "gmanoutputpng.h"
#include "ri.h"

namespace {

void checkNoGAMA(const char* path, RtFloat gamma) {
  gman::OutputPNG output(path, 2, 2);
  output.save(GMANOutput::RGB, 1.0f, gamma, GMANQuantize{255, 0, 255, 0});

  std::FILE* fp = std::fopen(path, "rb");
  std::string const label = "gamma " + std::to_string(gamma) + ": ";
  check(fp != nullptr, label + "file opens");
  if (fp == nullptr) {
    return;
  }

  png_structp png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
  png_infop info_ptr = png_create_info_struct(png_ptr);
  if (setjmp(png_jmpbuf(png_ptr))) {
    png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
    std::fclose(fp);
    check(false, label + "file decodes");
    return;
  }

  png_init_io(png_ptr, fp);
  png_read_info(png_ptr, info_ptr);

  png_uint_32 width = 0, height = 0;
  int bitDepth = 0, colorType = 0;
  png_get_IHDR(png_ptr, info_ptr, &width, &height, &bitDepth, &colorType, nullptr, nullptr, nullptr);
  check(width == 2 && height == 2, label + "IHDR reports the saved dimensions");

  check(!png_get_valid(png_ptr, info_ptr, PNG_INFO_gAMA), label + "no gAMA chunk present");

  png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
  std::fclose(fp);
}

} // namespace

#endif // GMAN_WITH_PNG

int main() {
#ifdef GMAN_WITH_PNG
  checkNoGAMA("gamma-1.0.png", 1.0f);
  checkNoGAMA("gamma-2.2.png", 2.2f);
#else
  std::printf("pnggamma: skipped (GMAN_WITH_PNG is off)\n");
#endif

  return checkSummary("png gamma chunk absent");
}
