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
 * GMANOutput::save's own pipeline -- gamma, then quantize, then a
 * NaN-safe [0, 1] clamp, all in float -- and what each driver does with
 * the float image it hands to writeImage.
 *
 * Two halves: what a driver receives, proven directly against a
 * test-local GMANOutput subclass that records the image writeImage is
 * called with; and what the file holds, proven by saving a small image
 * through gman::OutputPNM, gman::OutputTIFF and, where built,
 * gman::OutputPNG, then reading the written bytes back. No suite test
 * writes PNM, so PNM's byte identity rests on the checks here.
 */

#include <cstdio>
#include <cstring>
#include <functional>
#include <limits>
#include <string>
#include <vector>

#include "check.h"
#include "gmancolor.h"
#include "gmangamma.h"
#include "gmanoutput.h"
#include "gmanoutputpnm.h"
#include "gmanoutputtiff.h"

#ifdef GMAN_WITH_PNG
#include "gmanoutputpng.h"
extern "C" {
#include <png.h>
}
#endif

extern "C" {
#include <tiffio.h>
}

namespace {

// Records the image writeImage receives, instead of writing a file.
class RecordingOutput : public GMANOutput {
public:
  RecordingOutput(int width, int height) : GMANOutput("recording", width, height) {}

  std::vector<GMANColor> received;

protected:
  RtVoid writeImage(GMANOutput::DisplayMode /*mode*/, std::vector<GMANColor> const& image, RtFloat /*gamma*/) override {
    received = image;
  }
};

void checkReceives() {
  // 1. gain 1, gamma 1: 0.3 and 0.5 arrive bit-equal, as floats.
  {
    RecordingOutput out(2, 1);
    out.setPixel(0, 0, GMANColor(0.3f, 0.3f, 0.3f));
    out.setPixel(1, 0, GMANColor(0.5f, 0.5f, 0.5f));
    out.save(GMANOutput::RGB, 1.0f, 1.0f);
    check(out.received.size() == 2, "receive 1: writeImage receives both pixels");
    check(out.received[0].getRed() == 0.3f, "receive 1: 0.3 arrives bit-equal");
    check(out.received[1].getRed() == 0.5f, "receive 1: 0.5 arrives bit-equal");
  }

  // 2. gain 4, gamma 1: 0.8 arrives as exactly 1.
  {
    RecordingOutput out(1, 1);
    out.setPixel(0, 0, GMANColor(0.8f, 0.8f, 0.8f));
    out.save(GMANOutput::RGB, 4.0f, 1.0f);
    check(out.received[0].getRed() == 1.0f, "receive 2: gain 4 clamps 0.8 to exactly 1");
  }

  // 3. gain 0.5, gamma 1: 1.5 arrives as 0.75. A clamp ahead of the gain
  // would give 0.5 instead -- the clamp comes after.
  {
    RecordingOutput out(1, 1);
    out.setPixel(0, 0, GMANColor(1.5f, 1.5f, 1.5f));
    out.save(GMANOutput::RGB, 0.5f, 1.0f);
    check(out.received[0].getRed() == 0.75f, "receive 3: gain 0.5 brings 1.5 down to 0.75");
  }

  // 4. gain 1, gamma 1: -0.5 and NaN arrive as 0, positive infinity as 1.
  {
    RecordingOutput out(1, 1);
    RtFloat const nan = std::numeric_limits<RtFloat>::quiet_NaN();
    RtFloat const inf = std::numeric_limits<RtFloat>::infinity();
    out.setPixel(0, 0, GMANColor(-0.5f, nan, inf));
    out.save(GMANOutput::RGB, 1.0f, 1.0f);
    check(out.received[0].getRed() == 0.0f, "receive 4: -0.5 clamps to 0");
    check(out.received[0].getGreen() == 0.0f, "receive 4: NaN clamps to 0");
    check(out.received[0].getBlue() == 1.0f, "receive 4: +infinity clamps to 1");
  }

  // 5. gain 1, gamma 2.2: 0.002 arrives equal to gman::gammaCorrected's own
  // result, above 1/255.
  {
    RecordingOutput out(1, 1);
    out.setPixel(0, 0, GMANColor(0.002f, 0.002f, 0.002f));
    out.save(GMANOutput::RGB, 1.0f, 2.2f);
    GMANColor const expected = gman::gammaCorrected(GMANColor(0.002f, 0.002f, 0.002f), 1.0f, 2.2f);
    check(out.received[0].getRed() == expected.getRed(), "receive 5: matches gammaCorrected's own result");
    check(out.received[0].getRed() > 1.0f / 255.0f, "receive 5: 0.002 lifts above one byte step");
  }
}

// The narrowed bytes a saved file holds, independent of format: an
// interleaved byte buffer with a fixed sample count per pixel.
struct ByteImage {
  bool ok = false;
  int width = 0, height = 0;
  int samplesPerPixel = 0;
  std::vector<unsigned char> data;

  unsigned char at(int x, int y, int channel) const {
    return data[(std::size_t)(y * width + x) * (std::size_t)samplesPerPixel + (std::size_t)channel];
  }
};

// Parses the P6 header gman::OutputPNM writes, then reads the raw bytes.
ByteImage readPNM(std::string const& path) {
  ByteImage img;
  std::FILE* f = std::fopen(path.c_str(), "rb");
  if (f == nullptr) {
    return img;
  }
  char magic[4] = {};
  int width = 0, height = 0, maxval = 0;
  if (std::fscanf(f, "%3s %d %d %d", magic, &width, &height, &maxval) != 4 || std::strcmp(magic, "P6") != 0) {
    std::fclose(f);
    return img;
  }
  std::fgetc(f); // the single whitespace byte the P6 grammar puts after maxval

  img.width = width;
  img.height = height;
  img.samplesPerPixel = 3;
  img.data.resize((std::size_t)width * (std::size_t)height * 3);
  img.ok = std::fread(img.data.data(), 1, img.data.size(), f) == img.data.size();
  std::fclose(f);
  return img;
}

// Reads a TIFF's raw, unreinterpreted scanline bytes, as
// tests/coveragealpha_test.cpp does.
ByteImage readTIFFRaw(std::string const& path) {
  ByteImage img;
  TIFF* tif = TIFFOpen(path.c_str(), "r");
  if (tif == nullptr) {
    return img;
  }

  uint32_t width = 0, height = 0;
  uint16_t samplesPerPixel = 0;
  TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &width);
  TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &height);
  TIFFGetField(tif, TIFFTAG_SAMPLESPERPIXEL, &samplesPerPixel);

  const tmsize_t scanlineSize = TIFFScanlineSize(tif);
  const std::size_t linebytes = (std::size_t)width * (std::size_t)samplesPerPixel;
  std::vector<unsigned char> scanline((std::size_t)scanlineSize);

  img.data.resize((std::size_t)height * linebytes);
  bool ok = true;
  for (uint32_t y = 0; y < height; ++y) {
    if (TIFFReadScanline(tif, scanline.data(), y) < 0) {
      ok = false;
      break;
    }
    std::copy(scanline.begin(), scanline.begin() + (std::ptrdiff_t)linebytes,
              img.data.begin() + (std::ptrdiff_t)((std::size_t)y * linebytes));
  }
  TIFFClose(tif);

  img.width = (int)width;
  img.height = (int)height;
  img.samplesPerPixel = samplesPerPixel;
  img.ok = ok;
  return img;
}

#ifdef GMAN_WITH_PNG
// Reads back the 8-bit RGBA gman::OutputPNG always writes, with no
// normalization: this reads this driver's own output, not an arbitrary
// PNG, so what it wrote is exactly what this expects.
ByteImage readPNGRaw(std::string const& path) {
  ByteImage img;
  FILE* fp = std::fopen(path.c_str(), "rb");
  if (fp == nullptr) {
    return img;
  }

  png_structp png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
  if (png_ptr == nullptr) {
    std::fclose(fp);
    return img;
  }
  png_infop info_ptr = png_create_info_struct(png_ptr);
  if (info_ptr == nullptr) {
    png_destroy_read_struct(&png_ptr, nullptr, nullptr);
    std::fclose(fp);
    return img;
  }
  if (setjmp(png_jmpbuf(png_ptr))) {
    png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
    std::fclose(fp);
    return ByteImage();
  }

  png_init_io(png_ptr, fp);
  png_read_info(png_ptr, info_ptr);

  png_uint_32 width = 0, height = 0;
  int bitDepth = 0, colorType = 0;
  png_get_IHDR(png_ptr, info_ptr, &width, &height, &bitDepth, &colorType, nullptr, nullptr, nullptr);
  png_read_update_info(png_ptr, info_ptr);

  png_uint_32 rowbytes = png_get_rowbytes(png_ptr, info_ptr);
  std::vector<png_byte> buffer((std::size_t)rowbytes * height);
  std::vector<png_bytep> rows(height);
  for (png_uint_32 y = 0; y < height; ++y) {
    rows[y] = buffer.data() + y * rowbytes;
  }
  png_read_image(png_ptr, rows.data());
  png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
  std::fclose(fp);

  img.width = (int)width;
  img.height = (int)height;
  img.samplesPerPixel = 4;
  img.data.assign(buffer.begin(), buffer.end());
  img.ok = true;
  return img;
}
#endif

void savePNM(std::string const& path, std::vector<GMANColor> const& pixels, int width, int height, RtFloat gain,
             RtFloat gamma) {
  gman::OutputPNM output(path.c_str(), width, height);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      output.setPixel(x, y, pixels[(std::size_t)y * (std::size_t)width + (std::size_t)x]);
    }
  }
  output.save(GMANOutput::RGB, gain, gamma);
}

void saveTIFF(std::string const& path, std::vector<GMANColor> const& pixels, int width, int height, RtFloat gain,
              RtFloat gamma) {
  gman::OutputTIFF output(path.c_str(), width, height);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      output.setPixel(x, y, pixels[(std::size_t)y * (std::size_t)width + (std::size_t)x]);
    }
  }
  output.save(GMANOutput::RGB, gain, gamma);
}

#ifdef GMAN_WITH_PNG
void savePNG(std::string const& path, std::vector<GMANColor> const& pixels, int width, int height, RtFloat gain,
             RtFloat gamma) {
  gman::OutputPNG output(path.c_str(), width, height);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      output.setPixel(x, y, pixels[(std::size_t)y * (std::size_t)width + (std::size_t)x]);
    }
  }
  output.save(GMANOutput::RGB, gain, gamma);
}
#endif

// Checks 1 through 3, run against one driver's own save and read
// functions. A named check that only ever passed for the wrong reason on
// one driver fails here on that driver alone once the mutation in §8.3
// that names it lands.
void checkBytesForDriver(
    std::string const& driverName,
    std::function<void(std::string const&, std::vector<GMANColor> const&, int, int, RtFloat, RtFloat)> const& save,
    std::function<ByteImage(std::string const&)> const& read) {
  // 1. gain 1, gamma 2.2: 0.002 writes 15.
  {
    std::string const path = "narrow_" + driverName + "_check1.out";
    save(path, {GMANColor(0.002f, 0.002f, 0.002f)}, 1, 1, 1.0f, 2.2f);
    ByteImage const img = read(path);
    check(img.ok, driverName + " check1: file reads back");
    if (img.ok) {
      check(img.at(0, 0, 0) == 15,
            driverName + " check1: gain 1 gamma 2.2 narrows 0.002 to 15 (got " + std::to_string(img.at(0, 0, 0)) + ")");
    }
  }

  // 2. gain 4, gamma 1: 0.8 writes 255.
  {
    std::string const path = "narrow_" + driverName + "_check2.out";
    save(path, {GMANColor(0.8f, 0.8f, 0.8f)}, 1, 1, 4.0f, 1.0f);
    ByteImage const img = read(path);
    check(img.ok, driverName + " check2: file reads back");
    if (img.ok) {
      check(img.at(0, 0, 0) == 255,
            driverName + " check2: gain 4 narrows 0.8 to 255 (got " + std::to_string(img.at(0, 0, 0)) + ")");
    }
  }

  // 3. gain 1, gamma 1: 0, 0.5 and 1 write 0, 127 and 255. Rounding would
  // write 128 for 0.5.
  {
    std::string const path = "narrow_" + driverName + "_check3.out";
    save(path, {GMANColor(0.0f, 0.0f, 0.0f), GMANColor(0.5f, 0.5f, 0.5f), GMANColor(1.0f, 1.0f, 1.0f)}, 3, 1, 1.0f,
         1.0f);
    ByteImage const img = read(path);
    check(img.ok, driverName + " check3: file reads back");
    if (img.ok) {
      check(img.at(0, 0, 0) == 0, driverName + " check3: 0 narrows to 0 (got " + std::to_string(img.at(0, 0, 0)) + ")");
      check(img.at(1, 0, 0) == 127,
            driverName + " check3: 0.5 truncates to 127 (got " + std::to_string(img.at(1, 0, 0)) + ")");
      check(img.at(2, 0, 0) == 255,
            driverName + " check3: 1 narrows to 255 (got " + std::to_string(img.at(2, 0, 0)) + ")");
    }
  }

  std::printf("checked driver: %s\n", driverName.c_str());
}

// 4. TIFF alone, GMANOutput::RGBA: a pixel whose alpha is NaN writes
// alpha 0.
void checkTIFFAlphaNaN() {
  RtFloat const nan = std::numeric_limits<RtFloat>::quiet_NaN();
  std::string const path = "narrow_tiff_check4.tif";

  gman::OutputTIFF output(path.c_str(), 1, 1);
  output.setPixel(0, 0, GMANColor(0.5f, 0.5f, 0.5f));
  output.setAlpha(0, 0, GMANAlpha(nan, nan, nan));
  output.save(GMANOutput::RGBA, 1.0f, 1.0f);

  ByteImage const img = readTIFFRaw(path);
  check(img.ok, "tiff check4: file reads back");
  if (img.ok) {
    check(img.samplesPerPixel == 4, "tiff check4: RGBA writes 4 samples");
    check(img.at(0, 0, 3) == 0, "tiff check4: NaN alpha narrows to 0 (got " + std::to_string(img.at(0, 0, 3)) + ")");
  }
}

} // namespace

int main() {
  checkReceives();

  checkBytesForDriver("pnm", savePNM, readPNM);
  checkBytesForDriver("tiff", saveTIFF, readTIFFRaw);
#ifdef GMAN_WITH_PNG
  checkBytesForDriver("png", savePNG, readPNGRaw);
#else
  std::printf("checked driver: none (GMAN_WITH_PNG is off)\n");
#endif

  checkTIFFAlphaNaN();

  return checkSummary("output narrowing holds");
}
