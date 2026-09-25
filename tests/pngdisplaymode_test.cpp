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
 * gman::OutputPNG's DisplayMode contract: "rgb" writes three samples with
 * no alpha; "rgbaz" (bucketed with "rgba" and every other non-rgb,
 * non-a mode) keeps four, its fourth sample carrying alpha as coverage;
 * "a" writes one sample, the same coverage byte, with no RGB channel at
 * all. Reads the produced files back with libpng directly (tests/ is
 * exempt from AGENTS.md's one-includer rule for png.h).
 */

#include "check.h"

#ifdef GMAN_WITH_PNG

#include <cstdio>
#include <string>
#include <utility>
#include <vector>

extern "C" {
#include <png.h>
}

#include "gmanoutputpng.h"
#include "ri.h"

namespace {

// A non-uniform alpha, so a coverage computation that reads the wrong
// channel or drops the divide cannot land on the right byte by accident.
// (0.3 + 0.6 + 0.9) / 3 is 0.6 in real arithmetic; 0.6 has no exact binary
// float representation, and the nearest float is fractionally below it, so
// narrowing (multiplying by 255 and truncating) lands on 152, not 153.
// Computed independently of gman::coverageByte, the function under test.
GMANAlpha const kPartialAlpha(0.3f, 0.6f, 0.9f);
unsigned char const kExpectedCoverageByte = 152;

struct DecodedPNG {
  bool ok = false;
  int colorType = -1;
  png_uint_32 width = 0, height = 0;
  png_uint_32 rowbytes = 0;
  std::vector<png_byte> pixels;
};

DecodedPNG readPNG(const char* path) {
  DecodedPNG img;
  std::FILE* fp = std::fopen(path, "rb");
  if (fp == nullptr) {
    return img;
  }

  png_structp png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
  png_infop info_ptr = png_create_info_struct(png_ptr);
  if (setjmp(png_jmpbuf(png_ptr))) {
    png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
    std::fclose(fp);
    return DecodedPNG();
  }

  png_init_io(png_ptr, fp);
  png_read_info(png_ptr, info_ptr);

  int bitDepth = 0;
  png_get_IHDR(png_ptr, info_ptr, &img.width, &img.height, &bitDepth, &img.colorType, nullptr, nullptr, nullptr);
  png_read_update_info(png_ptr, info_ptr);
  img.rowbytes = png_get_rowbytes(png_ptr, info_ptr);

  std::vector<png_byte> buffer((std::size_t)img.rowbytes * img.height);
  std::vector<png_bytep> rows(img.height);
  for (png_uint_32 y = 0; y < img.height; ++y) {
    rows[y] = buffer.data() + y * img.rowbytes;
  }
  png_read_image(png_ptr, rows.data());
  png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
  std::fclose(fp);

  img.pixels = std::move(buffer);
  img.ok = true;
  return img;
}

void checkRGB() {
  const char* path = "rgb.png";
  gman::OutputPNG output(path, 2, 2);
  output.save(GMANOutput::RGB, 1.0f, 1.0f);

  DecodedPNG const img = readPNG(path);
  check(img.ok, "rgb: file opens and decodes");
  if (!img.ok) {
    return;
  }
  check(img.colorType == PNG_COLOR_TYPE_RGB, "rgb: color type carries no alpha channel");
  check(img.rowbytes == img.width * 3, "rgb: exactly 3 samples per pixel");
}

void checkRGBAZ() {
  const char* path = "rgbaz.png";
  gman::OutputPNG output(path, 2, 2);
  output.setAlpha(0, 0, kPartialAlpha);
  output.save(GMANOutput::RGBAZ, 1.0f, 1.0f);

  DecodedPNG const img = readPNG(path);
  check(img.ok, "rgbaz: file opens and decodes");
  if (!img.ok) {
    return;
  }
  check(img.colorType == PNG_COLOR_TYPE_RGB_ALPHA, "rgbaz: color type carries RGB plus alpha");
  check(img.rowbytes == img.width * 4, "rgbaz: exactly 4 samples per pixel");

  unsigned char const actual = img.pixels[3];
  check(actual == kExpectedCoverageByte, "rgbaz: alpha byte matches the hand-computed coverage (expected " +
                                             std::to_string((int)kExpectedCoverageByte) + ", got " +
                                             std::to_string((int)actual) + ")");
}

void checkA() {
  const char* path = "a.png";
  gman::OutputPNG output(path, 2, 2);
  output.setAlpha(0, 0, kPartialAlpha);
  output.save(GMANOutput::A, 1.0f, 1.0f);

  DecodedPNG const img = readPNG(path);
  check(img.ok, "a: file opens and decodes");
  if (!img.ok) {
    return;
  }
  check(img.colorType == PNG_COLOR_TYPE_GRAY, "a: no RGB channel present in the decoded file");
  check(img.rowbytes == img.width * 1, "a: exactly 1 sample per pixel");

  unsigned char const actual = img.pixels[0];
  check(actual == kExpectedCoverageByte, "a: sole sample matches the hand-computed coverage (expected " +
                                             std::to_string((int)kExpectedCoverageByte) + ", got " +
                                             std::to_string((int)actual) + ")");
}

} // namespace

#endif // GMAN_WITH_PNG

int main() {
#ifdef GMAN_WITH_PNG
  checkRGB();
  checkRGBAZ();
  checkA();
#else
  std::printf("pngdisplaymode: skipped (GMAN_WITH_PNG is off)\n");
#endif

  return checkSummary("png displaymode holds");
}
