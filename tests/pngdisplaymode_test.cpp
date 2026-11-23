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
 * all. Every mode but "rgb" writes straight alpha: colour divided by
 * coverage, in linear light before gamma and quantizing, wherever coverage
 * lies strictly between 0 and 1; "rgb" and every pixel at or outside the
 * bounds keep their colour as it was. Reads the produced files back with
 * libpng directly (tests/ is exempt from AGENTS.md's one-includer rule for
 * png.h).
 */

#include "check.h"

#ifdef GMAN_WITH_PNG

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
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
// (0.2 + 0.5 + 0.8) / 3 = 1.5 / 3 = 0.5 exactly; 0.5 * 255 = 127.5, which
// rounds to 128, in float and in double alike. Computed independently of
// the quantizing this test's zero-amplitude requests drive.
GMANAlpha const kPartialAlpha(0.2f, 0.5f, 0.8f);
unsigned char const kExpectedCoverageByte = 128;

struct DecodedPNG {
  bool ok = false;
  int colorType = -1;
  png_uint_32 width = 0, height = 0;
  png_uint_32 rowbytes = 0;
  std::vector<png_byte> pixels;
};

DecodedPNG readPNG(char const* path) {
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
  char const* path = "rgb.png";
  gman::OutputPNG output(path, 2, 2);
  output.save(GMANOutput::RGB, 1.0f, 1.0f, GMANQuantize{255, 0, 255, 0});

  DecodedPNG const img = readPNG(path);
  check(img.ok, "rgb: file opens and decodes");
  if (!img.ok) {
    return;
  }
  check(img.colorType == PNG_COLOR_TYPE_RGB, "rgb: color type carries no alpha channel");
  check(img.rowbytes == img.width * 3, "rgb: exactly 3 samples per pixel");
}

void checkRGBAZ() {
  char const* path = "rgbaz.png";
  gman::OutputPNG output(path, 2, 2);
  output.setAlpha(0, 0, kPartialAlpha);
  output.save(GMANOutput::RGBAZ, 1.0f, 1.0f, GMANQuantize{255, 0, 255, 0});

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
  char const* path = "a.png";
  gman::OutputPNG output(path, 2, 2);
  output.setAlpha(0, 0, kPartialAlpha);
  output.save(GMANOutput::A, 1.0f, 1.0f, GMANQuantize{255, 0, 255, 0});

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

unsigned char predictedByte(double v) {
  return static_cast<unsigned char>(std::clamp(std::floor(255.0 * v + 0.5), 0.0, 255.0));
}

struct PixelCase {
  std::string name;
  GMANOutput::DisplayMode mode;
  RtFloat gamma;
  GMANColor color;
  GMANAlpha alpha;
  double coverage;
};

GMANColor const kPixelColor(0.2f, 0.3f, 0.4f);
double const kPixelRed = kPixelColor.getRed(), kPixelGreen = kPixelColor.getGreen(), kPixelBlue = kPixelColor.getBlue();

void checkPixel(PixelCase const& c, double red, double green, double blue) {
  std::string const path = "straight_" + c.name + ".png";
  gman::OutputPNG output(path.c_str(), 2, 2);
  output.setPixel(0, 0, c.color);
  output.setAlpha(0, 0, c.alpha);
  output.save(c.mode, 1.0f, c.gamma, GMANQuantize{255, 0, 255, 0});

  DecodedPNG const img = readPNG(path.c_str());
  check(img.ok, c.name + ": file opens and decodes");
  if (!img.ok) {
    return;
  }
  bool const hasAlpha = c.mode != GMANOutput::RGB;
  check(img.rowbytes == img.width * (hasAlpha ? 4u : 3u),
        c.name + ": " + (hasAlpha ? "4" : "3") + " samples per pixel");

  double const linear[3] = {red, green, blue};
  char const* const channelNames[3] = {"red", "green", "blue"};
  for (int channel = 0; channel < 3; ++channel) {
    int const expected = predictedByte(std::pow(linear[channel], 1.0 / c.gamma));
    int const actual = img.pixels[static_cast<std::size_t>(channel)];
    check(std::abs(actual - expected) <= 1, c.name + ": " + channelNames[channel] + " byte (expected " +
                                                std::to_string(expected) + ", got " + std::to_string(actual) + ")");
  }
  if (hasAlpha) {
    int const expected = predictedByte(c.coverage);
    int const actual = img.pixels[3];
    check(std::abs(actual - expected) <= 1,
          c.name + ": alpha byte (expected " + std::to_string(expected) + ", got " + std::to_string(actual) + ")");
  }
}

void checkStraightPartial() {
  double const coverage = 0.5;
  checkPixel({"straight-partial", GMANOutput::RGBA, 1.0f, kPixelColor, kPartialAlpha, coverage}, kPixelRed / coverage,
             kPixelGreen / coverage, kPixelBlue / coverage);
}

void checkStraightGamma() {
  double const coverage = 0.5;
  checkPixel({"straight-gamma", GMANOutput::RGBA, 2.2f, kPixelColor, kPartialAlpha, coverage}, kPixelRed / coverage,
             kPixelGreen / coverage, kPixelBlue / coverage);
}

void checkRGBStaysPremultiplied() {
  checkPixel({"rgb-premultiplied", GMANOutput::RGB, 1.0f, kPixelColor, kPartialAlpha, 0.5}, kPixelRed, kPixelGreen,
             kPixelBlue);
}

void checkOpaqueKeepsColor() {
  GMANAlpha const opaque(1.0f, 1.0f, 1.0f);
  checkPixel({"opaque", GMANOutput::RGBA, 1.0f, kPixelColor, opaque, 1.0}, kPixelRed, kPixelGreen, kPixelBlue);
}

void checkZeroKeepsColor() {
  GMANAlpha const none(0.0f, 0.0f, 0.0f);
  checkPixel({"zero", GMANOutput::RGBA, 1.0f, kPixelColor, none, 0.0}, kPixelRed, kPixelGreen, kPixelBlue);
}

void checkOvershootKeepsColor() {
  GMANColor const color(0.6f, 0.6f, 0.6f);
  GMANAlpha const over(1.2f, 1.2f, 1.2f);
  checkPixel({"overshoot", GMANOutput::RGBA, 1.0f, color, over, 1.0}, 0.6, 0.6, 0.6);
}

void checkStraightClamps() {
  GMANColor const color(0.4f, 0.4f, 0.4f);
  GMANAlpha const quarter(0.25f, 0.25f, 0.25f);
  double const coverage = 0.25;
  checkPixel({"clamp", GMANOutput::RGBA, 1.0f, color, quarter, coverage}, 0.4 / coverage, 0.4 / coverage,
             0.4 / coverage);
}

void checkNegativeKeepsColor() {
  GMANAlpha const negative(-0.5f, -0.5f, -0.5f);
  checkPixel({"negative", GMANOutput::RGBA, 1.0f, kPixelColor, negative, 0.0}, kPixelRed, kPixelGreen, kPixelBlue);
}

void checkNaNKeepsColor() {
  RtFloat const nan = std::numeric_limits<RtFloat>::quiet_NaN();
  GMANAlpha const notANumber(nan, nan, nan);
  checkPixel({"nan", GMANOutput::RGBA, 1.0f, kPixelColor, notANumber, 0.0}, kPixelRed, kPixelGreen, kPixelBlue);
}

} // namespace

#endif // GMAN_WITH_PNG

int main() {
#ifdef GMAN_WITH_PNG
  checkRGB();
  checkRGBAZ();
  checkA();
  checkStraightPartial();
  checkStraightGamma();
  checkRGBStaysPremultiplied();
  checkOpaqueKeepsColor();
  checkZeroKeepsColor();
  checkOvershootKeepsColor();
  checkStraightClamps();
  checkNegativeKeepsColor();
  checkNaNKeepsColor();
#else
  std::printf("pngdisplaymode: skipped (GMAN_WITH_PNG is off)\n");
#endif

  return checkSummary("png displaymode holds");
}
