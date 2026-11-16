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
 * A 16-bit TIFF texture keeps its depth. Fixtures are written with libtiff
 * directly -- a 2x1 image whose samples {128, 32767, 65407} and
 * {1000, 40000, 65535} libtiff's own 8-bit reader rounds to
 * {0, 127, 255} and {4, 156, 255} -- and each case reads them back through
 * a caller-owned gman::TextureCache at texel centres, where bilinear
 * sampling returns the texel itself:
 *
 *   a  16-bit RGB                 texel = v / 65535 to 1e-6
 *   b  16-bit greyscale           R, G and B each = v / 65535 to 1e-6
 *   c  16-bit RGB + assoc. alpha  as a; the alpha sample is dropped
 *   d  8-bit RGB checker          texel = v / 255 to 1e-6, no warning
 *   e  16-bit RGB, bottom-left    decodes at 8 bits (within 1/255 of
 *                                 v / 65535) and warns once, naming the
 *                                 file; makeTexture on it warns once too,
 *                                 naming MakeTexture and the file
 *   f  makeTexture on a           the written file is 16-bit, samples exact
 *   g  makeTexture on d           the written file is 8-bit, samples exact
 *   h  makeLatLongEnvironment on a  the written file is 16-bit, samples exact
 *   i  16-bit RGB + one extra     libtiff cannot convert it: the texture is
 *      sample (alpha read as      opaque black with a cannot-decode warning,
 *      blue)                      and makeTexture fails and writes no file
 *
 * The 8-bit rounding of the sample set is off from v / 65535 by up to
 * 1.95e-3, three orders of magnitude over the 1e-6 tolerance, so a reader
 * that narrows to 8 bits fails a, b, c, f and h. Verified by making the
 * full-depth branch of TIFFReader::decode unreachable, which turns exactly
 * those rows red and leaves tests/texture_test.cpp and
 * tests/texturecache_test.cpp green.
 */

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <tiffio.h>

#include "check.h"
#include "checkertexture.h"
#include "gmanlog.h"
#include "gmantexture.h"

namespace {

const double kExactTol = 1.0e-6;
const double kEightBitTol = 1.0 / 255.0;

const std::string kWarningPrefix = "GMAN WARNING: ";

const std::vector<std::uint16_t> kRgbRow = {128, 32767, 65407, 1000, 40000, 65535};
const std::vector<std::uint16_t> kRgbaRow = {128, 32767, 65407, 65535, 1000, 40000, 65535, 65535};
const std::vector<std::uint16_t> kGreyRow = {128, 40000};
const std::vector<std::uint16_t> kGreyAsRgb = {128, 128, 128, 40000, 40000, 40000};
const std::vector<std::uint16_t> kChecker = {255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255};

struct Layout16 {
  std::uint16_t samplesPerPixel;
  std::uint16_t photometric;
  std::uint16_t orientation;
  bool associatedAlpha;
};

const Layout16 kRgb = {3, PHOTOMETRIC_RGB, ORIENTATION_TOPLEFT, false};
const Layout16 kGrey = {1, PHOTOMETRIC_MINISBLACK, ORIENTATION_TOPLEFT, false};
const Layout16 kRgba = {4, PHOTOMETRIC_RGB, ORIENTATION_TOPLEFT, true};
const Layout16 kRgbBottomLeft = {3, PHOTOMETRIC_RGB, ORIENTATION_BOTLEFT, false};
const Layout16 kRgbExtraSample = {3, PHOTOMETRIC_RGB, ORIENTATION_TOPLEFT, true};

// Writes a 16-bit, one-row image of row.size() / samplesPerPixel pixels.
bool writeTiff16(std::string const& path, Layout16 const& layout, std::vector<std::uint16_t> const& row) {
  TIFF* tif = TIFFOpen(path.c_str(), "w");
  if (tif == nullptr) {
    return false;
  }
  TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, (std::uint32_t)(row.size() / layout.samplesPerPixel));
  TIFFSetField(tif, TIFFTAG_IMAGELENGTH, (std::uint32_t)1);
  TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, 16);
  TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, layout.samplesPerPixel);
  TIFFSetField(tif, TIFFTAG_ORIENTATION, layout.orientation);
  TIFFSetField(tif, TIFFTAG_PHOTOMETRIC, layout.photometric);
  TIFFSetField(tif, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
  TIFFSetField(tif, TIFFTAG_ROWSPERSTRIP, 1);
  if (layout.associatedAlpha) {
    const std::uint16_t extraSample = EXTRASAMPLE_ASSOCALPHA;
    TIFFSetField(tif, TIFFTAG_EXTRASAMPLES, 1, &extraSample);
  }
  const bool ok = TIFFWriteScanline(tif, (void*)row.data(), 0, 0) >= 0;
  TIFFClose(tif);
  return ok;
}

struct Readback {
  bool ok = false;
  std::uint16_t bitsPerSample = 0;
  std::vector<std::uint16_t> samples;
};

// Reads every sample of an RGB TIFF with libtiff directly, widening 8-bit
// samples so both depths compare against the same integers.
Readback readBack(std::string const& path) {
  Readback result;
  TIFF* tif = TIFFOpen(path.c_str(), "r");
  if (tif == nullptr) {
    return result;
  }
  std::uint32_t width = 0, height = 0;
  TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &width);
  TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &height);
  TIFFGetField(tif, TIFFTAG_BITSPERSAMPLE, &result.bitsPerSample);

  std::vector<unsigned char> scanline((std::size_t)TIFFScanlineSize(tif));
  result.ok = true;
  for (std::uint32_t y = 0; y < height && result.ok; ++y) {
    result.ok = TIFFReadScanline(tif, scanline.data(), y, 0) >= 0;
    for (std::size_t i = 0; i < (std::size_t)width * 3 && result.ok; ++i) {
      if (result.bitsPerSample == 16) {
        std::uint16_t sample = 0;
        std::memcpy(&sample, scanline.data() + i * 2, sizeof sample);
        result.samples.push_back(sample);
      } else {
        result.samples.push_back(scanline[i]);
      }
    }
  }
  TIFFClose(tif);
  return result;
}

// Samples name at every texel centre through a fresh cache and compares
// each channel with the matching entry of values (three per pixel,
// row-major) divided by denominator.
void checkTexels(std::string const& name, int width, int height, std::vector<std::uint16_t> const& values,
                 double denominator, double tol, std::string const& row) {
  gman::TextureCache cache;
  static char const* const channelName[3] = {"R", "G", "B"};
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const GMANColor got = cache.sample(name, (RtFloat)(x + 0.5) / (RtFloat)width,
                                         (RtFloat)(y + 0.5) / (RtFloat)height, gman::TEXTURE_CLAMP);
      const RtFloat channel[3] = {got.getRed(), got.getGreen(), got.getBlue()};
      for (int c = 0; c < 3; ++c) {
        const double want = values[(std::size_t)(y * width + x) * 3 + (std::size_t)c] / denominator;
        check(std::fabs(channel[c] - want) <= tol, row + ": texel (" + std::to_string(x) + ", " + std::to_string(y) +
                                                       ") " + channelName[c] + " got " + std::to_string(channel[c]) +
                                                       ", want " + std::to_string(want));
      }
    }
  }
}

void checkReadback(std::string const& path, std::uint16_t bitsPerSample, std::vector<std::uint16_t> const& want,
                   std::string const& row) {
  const Readback got = readBack(path);
  check(got.ok, row + ": " + path + " reads back");
  check(got.bitsPerSample == bitsPerSample,
        row + ": BITSPERSAMPLE is " + std::to_string(got.bitsPerSample) + ", want " + std::to_string(bitsPerSample));
  check(got.samples == want, row + ": samples equal the picture's exactly");
}

// Runs fn with warnings captured to a fresh file at path and returns the
// file's contents, as tests/polygonnverts_test.cpp's captureLog does.
template <typename Fn> std::string captureLog(std::string const& path, Fn&& fn) {
  std::remove(path.c_str());
  setLogFile(path.c_str());
  setScreenOutput(false);

  fn();

  setLogFile("/dev/null");
  setScreenOutput(true);

  std::ifstream in(path, std::ios::binary);
  std::ostringstream contents;
  contents << in.rdbuf();
  return contents.str();
}

int countOccurrences(std::string const& haystack, std::string const& needle) {
  int count = 0;
  std::string::size_type pos = 0;
  while ((pos = haystack.find(needle, pos)) != std::string::npos) {
    ++count;
    pos += needle.size();
  }
  return count;
}

void testRgb16() {
  const std::string name = "texture16_rgb.tif";
  check(writeTiff16(name, kRgb, kRgbRow), "a: " + name + " writes");
  checkTexels(name, 2, 1, kRgbRow, 65535.0, kExactTol, "a (16-bit RGB)");
}

void testGrey16() {
  const std::string name = "texture16_grey.tif";
  check(writeTiff16(name, kGrey, kGreyRow), "b: " + name + " writes");
  checkTexels(name, 2, 1, kGreyAsRgb, 65535.0, kExactTol, "b (16-bit greyscale)");
}

void testRgba16() {
  const std::string name = "texture16_rgba.tif";
  check(writeTiff16(name, kRgba, kRgbaRow), "c: " + name + " writes");
  checkTexels(name, 2, 1, kRgbRow, 65535.0, kExactTol, "c (16-bit RGB, associated alpha)");
}

void testRgb8() {
  const std::string name = "texture16_checker.tif";
  check(writeCheckerTexture(name), "d: " + name + " writes");
  const std::string log = captureLog("texture16_checker.log",
                                     [&] { checkTexels(name, 2, 2, kChecker, 255.0, kExactTol, "d (8-bit RGB)"); });
  check(countOccurrences(log, kWarningPrefix) == 0, "d (8-bit RGB): no warning, log holds \"" + log + "\"");
}

void testBottomLeft16() {
  const std::string name = "texture16_botleft.tif";
  check(writeTiff16(name, kRgbBottomLeft, kRgbRow), "e: " + name + " writes");
  const std::string log = captureLog("texture16_botleft.log", [&] {
    checkTexels(name, 2, 1, kRgbRow, 65535.0, kEightBitTol, "e (16-bit RGB, bottom-left)");
  });
  check(countOccurrences(log, kWarningPrefix) == 1, "e (16-bit RGB, bottom-left): exactly one warning");
  check(countOccurrences(log, name) == 1, "e (16-bit RGB, bottom-left): the warning names " + name);
  check(countOccurrences(log, "8 bits") == 1, "e (16-bit RGB, bottom-left): the warning says 8 bits");

  const std::string texture = "texture16_botleft_out.tif";
  bool made = false;
  const std::string makeLog = captureLog(
      "texture16_botleft_make.log", [&] { made = gman::makeTexture(name.c_str(), texture.c_str(), "clamp", "clamp"); });
  check(made, "e (16-bit RGB, bottom-left): makeTexture succeeds");
  check(countOccurrences(makeLog, kWarningPrefix) == 1, "e (16-bit RGB, bottom-left): makeTexture warns once");
  check(countOccurrences(makeLog, "MakeTexture") == 1, "e (16-bit RGB, bottom-left): the warning names MakeTexture");
  check(countOccurrences(makeLog, name) == 1, "e (16-bit RGB, bottom-left): makeTexture's warning names " + name);
  check(countOccurrences(makeLog, "8 bits") == 1, "e (16-bit RGB, bottom-left): makeTexture's warning says 8 bits");
}

void testExtraSampleRgb16() {
  const std::string name = "texture16_extra.tif";
  const std::string texture = "texture16_extra_out.tif";
  std::remove(texture.c_str());
  check(writeTiff16(name, kRgbExtraSample, kRgbRow), "i: " + name + " writes");

  const std::string log = captureLog("texture16_extra.log", [&] {
    checkTexels(name, 2, 1, std::vector<std::uint16_t>(6, 0), 1.0, kExactTol, "i (16-bit RGB, extra sample)");
  });
  check(countOccurrences(log, kWarningPrefix) == 1 && countOccurrences(log, "cannot decode") == 1,
        "i (16-bit RGB, extra sample): one cannot-decode warning, log holds \"" + log + "\"");

  bool made = true;
  captureLog("texture16_extra_make.log",
             [&] { made = gman::makeTexture(name.c_str(), texture.c_str(), "clamp", "clamp"); });
  check(!made, "i (16-bit RGB, extra sample): makeTexture fails");
  check(!std::ifstream(texture).good(), "i (16-bit RGB, extra sample): makeTexture leaves no file");
}

void testMakeTexture16() {
  const std::string picture = "texture16_make_in.tif";
  const std::string texture = "texture16_make_out.tif";
  check(writeTiff16(picture, kRgb, kRgbRow), "f: " + picture + " writes");
  check(gman::makeTexture(picture.c_str(), texture.c_str(), "clamp", "clamp"), "f: makeTexture succeeds");
  checkReadback(texture, 16, kRgbRow, "f (makeTexture, 16-bit)");
}

void testMakeTexture8() {
  const std::string picture = "texture16_make8_in.tif";
  const std::string texture = "texture16_make8_out.tif";
  check(writeCheckerTexture(picture), "g: " + picture + " writes");
  check(gman::makeTexture(picture.c_str(), texture.c_str(), "clamp", "clamp"), "g: makeTexture succeeds");
  checkReadback(texture, 8, kChecker, "g (makeTexture, 8-bit)");
}

void testMakeLatLongEnvironment16() {
  const std::string picture = "texture16_latlong_in.tif";
  const std::string texture = "texture16_latlong_out.tif";
  check(writeTiff16(picture, kRgb, kRgbRow), "h: " + picture + " writes");
  check(gman::makeLatLongEnvironment(picture.c_str(), texture.c_str()), "h: makeLatLongEnvironment succeeds");
  checkReadback(texture, 16, kRgbRow, "h (makeLatLongEnvironment, 16-bit)");
}

} // namespace

int main() {
  testRgb16();
  testGrey16();
  testRgba16();
  testRgb8();
  testBottomLeft16();
  testExtraSampleRgb16();
  testMakeTexture16();
  testMakeTexture8();
  testMakeLatLongEnvironment16();

  return checkSummary("16-bit TIFF textures keep their depth through sampling, makeTexture and "
                      "makeLatLongEnvironment, and 8-bit textures sample as before");
}
