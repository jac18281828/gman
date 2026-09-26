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
 * GMANOutput::save's own pipeline: gain and gamma on colour alone, then
 * every channel -- R, G, B and coverage alpha -- rounded and dithered by
 * RISpec's Quantize rule from one draw per pixel, clamped into the
 * request's resolved range and handed to writeImage as integers at 8 or 16
 * bits. A request maxBitsPerSample() cannot honour falls back to RISpec's
 * default range with the requested amplitude, and warns.
 *
 * Two halves: what a driver receives, proven directly against a
 * test-local GMANOutput subclass that records the samples and bit depth
 * writeImage is called with; and what the file holds, proven by saving a
 * small image through gman::OutputPNM, gman::OutputTIFF and, where built,
 * gman::OutputPNG, then reading the written bytes back. No suite test
 * writes PNM, so PNM's byte identity rests on the checks here.
 */

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <limits>
#include <string>
#include <vector>

#ifdef GMAN_WITH_PNG
extern "C" {
#include <png.h>
}
#endif

extern "C" {
#include <tiffio.h>
}

#include "check.h"
#include "gmancolor.h"
#include "gmanoutput.h"
#include "gmanoutputpnm.h"
#include "gmanoutputtiff.h"
#include "gmanquantize.h"

#ifdef GMAN_WITH_PNG
#include "gmanoutputpng.h"
#endif

namespace {

// Records the samples and bit depth writeImage receives, instead of
// writing a file. widestBits stands in for a driver's own
// maxBitsPerSample(), so save's fallback logic is provable directly.
class RecordingOutput : public GMANOutput {
public:
  RecordingOutput(int width, int height, int widestBits = 8)
      : GMANOutput("recording", width, height), widestBits(widestBits) {}

  std::vector<std::uint16_t> received;
  int receivedBitsPerSample = 0;

  int maxBitsPerSample() const override { return widestBits; }

protected:
  RtVoid writeImage(GMANOutput::DisplayMode /*mode*/, std::vector<std::uint16_t> const& samples, int bitsPerSample,
                    RtFloat /*gamma*/) override {
    received = samples;
    receivedBitsPerSample = bitsPerSample;
  }

private:
  int widestBits;
};

// The share of a 64x64 field's R channel equal to hi, out of the field's
// 4096 pixels -- the dither's fairness bound is stated per pixel, not per
// sample, since a pixel's R, G, B and alpha share one draw.
double shareEqualTo(std::vector<std::uint16_t> const& samples, int width, int height, std::uint16_t hi) {
  long count = 0;
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      if (samples[4 * ((std::size_t)y * (std::size_t)width + (std::size_t)x)] == hi) {
        ++count;
      }
    }
  }
  return (double)count / (double)(width * height);
}

void checkReceives() {
  // 1. 255 0 255 0: 0, 0.25, 0.5 and 1 arrive as 0, 64, 128 and 255, at 8
  // bits. Truncation gives 63 and 127.
  {
    RecordingOutput out(4, 1);
    out.setPixel(0, 0, GMANColor(0.0f, 0.0f, 0.0f));
    out.setPixel(1, 0, GMANColor(0.25f, 0.25f, 0.25f));
    out.setPixel(2, 0, GMANColor(0.5f, 0.5f, 0.5f));
    out.setPixel(3, 0, GMANColor(1.0f, 1.0f, 1.0f));
    out.save(GMANOutput::RGB, 1.0f, 1.0f, GMANQuantize{255, 0, 255, 0});
    check(out.receivedBitsPerSample == 8, "receive 1: 8 bits a sample");
    check(out.received[0] == 0, "receive 1: 0 rounds to 0");
    check(out.received[4] == 64, "receive 1: 0.25 rounds to 64 (got " + std::to_string(out.received[4]) + ")");
    check(out.received[8] == 128, "receive 1: 0.5 rounds to 128 (got " + std::to_string(out.received[8]) + ")");
    check(out.received[12] == 255, "receive 1: 1 rounds to 255");
  }

  // 2. 255 0 255 0, gain 0.5: 1.5 arrives as 191. A clamp ahead of the gain
  // gives 128.
  {
    RecordingOutput out(1, 1);
    out.setPixel(0, 0, GMANColor(1.5f, 1.5f, 1.5f));
    out.save(GMANOutput::RGB, 0.5f, 1.0f, GMANQuantize{255, 0, 255, 0});
    check(out.received[0] == 191,
          "receive 2: gain 0.5 brings 1.5 to 191 (got " + std::to_string(out.received[0]) + ")");
  }

  // 3. 255 0 255 0: -0.5 and NaN arrive as 0, positive infinity as 255.
  {
    RecordingOutput out(1, 1);
    RtFloat const nan = std::numeric_limits<RtFloat>::quiet_NaN();
    RtFloat const inf = std::numeric_limits<RtFloat>::infinity();
    out.setPixel(0, 0, GMANColor(-0.5f, nan, inf));
    out.save(GMANOutput::RGB, 1.0f, 1.0f, GMANQuantize{255, 0, 255, 0});
    check(out.received[0] == 0, "receive 3: -0.5 clamps to 0");
    check(out.received[1] == 0, "receive 3: NaN quantizes as 0 does");
    check(out.received[2] == 255, "receive 3: +infinity clamps to 255");
  }

  // 4. 255 50 100 0: 0, 0.25 and 1 arrive as 50, 64 and 100.
  {
    RecordingOutput out(3, 1);
    out.setPixel(0, 0, GMANColor(0.0f, 0.0f, 0.0f));
    out.setPixel(1, 0, GMANColor(0.25f, 0.25f, 0.25f));
    out.setPixel(2, 0, GMANColor(1.0f, 1.0f, 1.0f));
    out.save(GMANOutput::RGB, 1.0f, 1.0f, GMANQuantize{255, 50, 100, 0});
    check(out.received[0] == 50, "receive 4: 0 clamps to the requested min 50");
    check(out.received[4] == 64, "receive 4: 0.25 rounds to 64 inside [50, 100]");
    check(out.received[8] == 100, "receive 4: 1 clamps to the requested max 100");
  }

  // 5. widest 16, 255 0 1023 0: 1.6 arrives as 408, at 16 bits. A [0, 1]
  // clamp before quantizing gives 255.
  {
    RecordingOutput out(1, 1, 16);
    out.setPixel(0, 0, GMANColor(1.6f, 1.6f, 1.6f));
    out.save(GMANOutput::RGB, 1.0f, 1.0f, GMANQuantize{255, 0, 1023, 0});
    check(out.receivedBitsPerSample == 16, "receive 5: 16 bits a sample");
    check(out.received[0] == 408, "receive 5: 1.6 rounds to 408 (got " + std::to_string(out.received[0]) + ")");
  }

  // 6. widest 16, 65535 0 65535 0: 0.25 and 1 arrive as 16384 and 65535, at
  // 16 bits. Widening the 8-bit 64 gives 16448.
  {
    RecordingOutput out(2, 1, 16);
    out.setPixel(0, 0, GMANColor(0.25f, 0.25f, 0.25f));
    out.setPixel(1, 0, GMANColor(1.0f, 1.0f, 1.0f));
    out.save(GMANOutput::RGB, 1.0f, 1.0f, GMANQuantize{65535, 0, 65535, 0});
    check(out.receivedBitsPerSample == 16, "receive 6: 16 bits a sample");
    check(out.received[0] == 16384, "receive 6: 0.25 rounds to 16384 (got " + std::to_string(out.received[0]) + ")");
    check(out.received[4] == 65535, "receive 6: 1 rounds to 65535");
  }

  // 7. 255 0 255 0, gamma 2.2: 0.002 arrives as 15. Quantizing before gamma
  // gives 1.
  {
    RecordingOutput out(1, 1);
    out.setPixel(0, 0, GMANColor(0.002f, 0.002f, 0.002f));
    out.save(GMANOutput::RGB, 1.0f, 2.2f, GMANQuantize{255, 0, 255, 0});
    check(out.received[0] == 15,
          "receive 7: gamma 2.2 lifts 0.002 to 15 (got " + std::to_string(out.received[0]) + ")");
  }

  // 8. RGBA, 255 0 255 0, gamma 2.2: an alpha of 0.25 on all three channels
  // arrives as 64, where gamma would give 136; an alpha of (0, 0.5, 1)
  // arrives as 128, the channel mean taken inside save; a NaN alpha
  // arrives as 0.
  {
    RtFloat const nan = std::numeric_limits<RtFloat>::quiet_NaN();
    RecordingOutput out(3, 1);
    out.setPixel(0, 0, GMANColor(0.0f, 0.0f, 0.0f));
    out.setAlpha(0, 0, GMANAlpha(0.25f, 0.25f, 0.25f));
    out.setPixel(1, 0, GMANColor(0.0f, 0.0f, 0.0f));
    out.setAlpha(1, 0, GMANAlpha(0.0f, 0.5f, 1.0f));
    out.setPixel(2, 0, GMANColor(0.0f, 0.0f, 0.0f));
    out.setAlpha(2, 0, GMANAlpha(nan, nan, nan));
    out.save(GMANOutput::RGBA, 1.0f, 2.2f, GMANQuantize{255, 0, 255, 0});
    check(out.received[3] == 64,
          "receive 8: uniform alpha 0.25 skips gamma, rounding to 64 (got " + std::to_string(out.received[3]) + ")");
    check(out.received[7] == 128,
          "receive 8: alpha (0, 0.5, 1)'s mean rounds to 128 (got " + std::to_string(out.received[7]) + ")");
    check(out.received[11] == 0, "receive 8: NaN alpha quantizes as 0 does");
  }

  // 9. 255 0 255 0.5, a 64x64 field of colour and alpha 0.5: every sample
  // reads 127 or 128; R, G, B and alpha agree at every pixel; and 128's
  // share lies within 0.5 +/- 0.039, five standard deviations of 4096 fair
  // draws.
  std::vector<std::uint16_t> fieldHalf;
  {
    const int w = 64, h = 64;
    RecordingOutput out(w, h);
    for (int y = 0; y < h; ++y) {
      for (int x = 0; x < w; ++x) {
        out.setPixel(x, y, GMANColor(0.5f, 0.5f, 0.5f));
        out.setAlpha(x, y, GMANAlpha(0.5f, 0.5f, 0.5f));
      }
    }
    out.save(GMANOutput::RGBA, 1.0f, 1.0f, GMANQuantize{255, 0, 255, 0.5});
    bool onlyTwoValues = true;
    bool channelsAgree = true;
    for (int y = 0; y < h && (onlyTwoValues || channelsAgree); ++y) {
      for (int x = 0; x < w; ++x) {
        const std::size_t idx = 4 * ((std::size_t)y * (std::size_t)w + (std::size_t)x);
        for (int c = 0; c < 4; ++c) {
          if (out.received[idx + (std::size_t)c] != 127 && out.received[idx + (std::size_t)c] != 128) {
            onlyTwoValues = false;
          }
        }
        if (out.received[idx + 0] != out.received[idx + 1] || out.received[idx + 1] != out.received[idx + 2] ||
            out.received[idx + 2] != out.received[idx + 3]) {
          channelsAgree = false;
        }
      }
    }
    check(onlyTwoValues, "receive 9: every sample reads 127 or 128");
    check(channelsAgree, "receive 9: R, G, B and alpha agree at every pixel");
    const double share = shareEqualTo(out.received, w, h, 128);
    check(std::fabs(share - 0.5) <= 0.039,
          "receive 9: 128's share lies within 0.5 +/- 0.039 (got " + std::to_string(share) + ")");
    fieldHalf = out.received;
  }

  // 10. A second save of check 9's buffer receives identical samples.
  {
    const int w = 64, h = 64;
    RecordingOutput out(w, h);
    for (int y = 0; y < h; ++y) {
      for (int x = 0; x < w; ++x) {
        out.setPixel(x, y, GMANColor(0.5f, 0.5f, 0.5f));
        out.setAlpha(x, y, GMANAlpha(0.5f, 0.5f, 0.5f));
      }
    }
    out.save(GMANOutput::RGBA, 1.0f, 1.0f, GMANQuantize{255, 0, 255, 0.5});
    check(out.received == fieldHalf, "receive 10: a second save of the same buffer reproduces every sample");
  }

  // 11. 255 0 255 0.5: a 64x64 field of 0 arrives all 0, and of 1 all 255.
  // Widest 16, 65534 0 65535 0.5: a 64x64 field of 0.5 arrives all 32767.
  {
    const int w = 64, h = 64;
    RecordingOutput zero(w, h);
    RecordingOutput one(w, h);
    for (int y = 0; y < h; ++y) {
      for (int x = 0; x < w; ++x) {
        zero.setPixel(x, y, GMANColor(0.0f, 0.0f, 0.0f));
        zero.setAlpha(x, y, GMANAlpha(0.0f, 0.0f, 0.0f));
        one.setPixel(x, y, GMANColor(1.0f, 1.0f, 1.0f));
        one.setAlpha(x, y, GMANAlpha(1.0f, 1.0f, 1.0f));
      }
    }
    zero.save(GMANOutput::RGBA, 1.0f, 1.0f, GMANQuantize{255, 0, 255, 0.5});
    one.save(GMANOutput::RGBA, 1.0f, 1.0f, GMANQuantize{255, 0, 255, 0.5});
    check(std::all_of(zero.received.begin(), zero.received.end(), [](std::uint16_t v) { return v == 0; }),
          "receive 11: a field of 0 arrives all 0 under dither");
    check(std::all_of(one.received.begin(), one.received.end(), [](std::uint16_t v) { return v == 255; }),
          "receive 11: a field of 1 arrives all 255 under dither");

    RecordingOutput half(w, h, 16);
    for (int y = 0; y < h; ++y) {
      for (int x = 0; x < w; ++x) {
        half.setPixel(x, y, GMANColor(0.5f, 0.5f, 0.5f));
        half.setAlpha(x, y, GMANAlpha(0.5f, 0.5f, 0.5f));
      }
    }
    half.save(GMANOutput::RGBA, 1.0f, 1.0f, GMANQuantize{65534, 0, 65535, 0.5});
    check(std::all_of(half.received.begin(), half.received.end(), [](std::uint16_t v) { return v == 32767; }),
          "receive 11: widest 16, one 65534, a field of 0.5 arrives all 32767");
  }

  // 12. Widest 8: 65535 0 65535 0, 0 0 0 0 and 255 200 100 0 each arrive at
  // 8 bits with 0.25 as 64. Widest 8, 65535 0 65535 0.5: a 64x64 field of
  // 0.5 arrives at 8 bits, and 128's share lies within 0.5 +/- 0.039, so
  // the fallback keeps the requested amplitude.
  {
    GMANQuantize const invalid[] = {
        {65535, 0, 65535, 0},
        {0, 0, 0, 0},
        {255, 200, 100, 0},
    };
    for (GMANQuantize const& request : invalid) {
      RecordingOutput out(1, 1);
      out.setPixel(0, 0, GMANColor(0.25f, 0.25f, 0.25f));
      out.save(GMANOutput::RGB, 1.0f, 1.0f, request);
      check(out.receivedBitsPerSample == 8, "receive 12: an unhonoured request still falls back to 8 bits");
      check(out.received[0] == 64,
            "receive 12: the fallback rounds 0.25 to 64 (got " + std::to_string(out.received[0]) + ")");
    }

    const int w = 64, h = 64;
    RecordingOutput out(w, h);
    for (int y = 0; y < h; ++y) {
      for (int x = 0; x < w; ++x) {
        out.setPixel(x, y, GMANColor(0.5f, 0.5f, 0.5f));
      }
    }
    out.save(GMANOutput::RGB, 1.0f, 1.0f, GMANQuantize{65535, 0, 65535, 0.5});
    check(out.receivedBitsPerSample == 8, "receive 12: the fallback for an invalid amplitude request is 8 bits");
    const double share = shareEqualTo(out.received, w, h, 128);
    check(std::fabs(share - 0.5) <= 0.039,
          "receive 12: the fallback keeps the requested amplitude (128's share got " + std::to_string(share) + ")");
  }
}

// The narrowed bytes a saved file holds, independent of format: an
// interleaved byte buffer with a fixed sample count per pixel.
struct ByteImage {
  bool ok = false;
  int width = 0, height = 0;
  int samplesPerPixel = 0;
  int bitsPerSample = 8;
  std::vector<unsigned char> data;

  unsigned int at(int x, int y, int channel) const {
    const std::size_t base = ((std::size_t)(y * width + x) * (std::size_t)samplesPerPixel + (std::size_t)channel) *
                             (std::size_t)(bitsPerSample == 16 ? 2 : 1);
    if (bitsPerSample == 16) {
      std::uint16_t v;
      std::memcpy(&v, &data[base], sizeof(v));
      return v;
    }
    return data[base];
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
  uint16_t bitsPerSample = 8;
  TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &width);
  TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &height);
  TIFFGetField(tif, TIFFTAG_SAMPLESPERPIXEL, &samplesPerPixel);
  TIFFGetField(tif, TIFFTAG_BITSPERSAMPLE, &bitsPerSample);

  const tmsize_t scanlineSize = TIFFScanlineSize(tif);
  const std::size_t linebytes = (std::size_t)width * (std::size_t)samplesPerPixel * (bitsPerSample == 16 ? 2 : 1);
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
  img.bitsPerSample = bitsPerSample;
  img.ok = ok;
  return img;
}

#ifdef GMAN_WITH_PNG
// Reads back the 8-bit gman::OutputPNG always writes, with no
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
  img.samplesPerPixel = (colorType == PNG_COLOR_TYPE_RGB) ? 3 : 4;
  img.data.assign(buffer.begin(), buffer.end());
  img.ok = true;
  return img;
}
#endif

void savePNM(std::string const& path, GMANColor const& colour, GMANQuantize const& quantize) {
  gman::OutputPNM output(path.c_str(), 1, 1);
  output.setPixel(0, 0, colour);
  output.save(GMANOutput::RGB, 1.0f, 1.0f, quantize);
}

void saveTIFF(std::string const& path, GMANColor const& colour, GMANQuantize const& quantize) {
  gman::OutputTIFF output(path.c_str(), 1, 1);
  output.setPixel(0, 0, colour);
  output.save(GMANOutput::RGB, 1.0f, 1.0f, quantize);
}

#ifdef GMAN_WITH_PNG
void savePNG(std::string const& path, GMANColor const& colour, GMANQuantize const& quantize) {
  gman::OutputPNG output(path.c_str(), 1, 1);
  output.setPixel(0, 0, colour);
  output.save(GMANOutput::RGB, 1.0f, 1.0f, quantize);
}
#endif

// 13. PNM, TIFF and PNG: 255 0 255 0 writes 0.25 as 64, and 65535 0 65535 0
// writes it as 64 at 8 bits in PNM and PNG. TIFF now honours that request
// itself; checkTIFF16Bit proves it below.
void checkFileRounding(std::string const& driverName,
                       std::function<void(std::string const&, GMANColor const&, GMANQuantize const&)> const& save,
                       std::function<ByteImage(std::string const&)> const& read, bool has16Bit) {
  {
    std::string const path = "narrow_" + driverName + "_255.out";
    save(path, GMANColor(0.25f, 0.25f, 0.25f), GMANQuantize{255, 0, 255, 0});
    ByteImage const img = read(path);
    check(img.ok, driverName + " check13: file reads back");
    if (img.ok) {
      check(img.at(0, 0, 0) == 64,
            driverName + " check13: 255 0 255 0 rounds 0.25 to 64 (got " + std::to_string(img.at(0, 0, 0)) + ")");
    }
  }
  if (!has16Bit) {
    std::string const path = "narrow_" + driverName + "_fallback.out";
    save(path, GMANColor(0.25f, 0.25f, 0.25f), GMANQuantize{65535, 0, 65535, 0});
    ByteImage const img = read(path);
    check(img.ok, driverName + " check13: fallback file reads back");
    if (img.ok) {
      check(img.at(0, 0, 0) == 64, driverName +
                                       " check13: an unhonoured 16-bit request falls back to 8 bits, "
                                       "rounding 0.25 to 64 (got " +
                                       std::to_string(img.at(0, 0, 0)) + ")");
    }
  }
  std::printf("checked driver: %s\n", driverName.c_str());
}

// 14. TIFF, 65535 0 65535 0, RGBA: BitsPerSample reads 16, 0.25 and 1 read
// 16384 and 65535, and an alpha of 0.25 reads 16384.
void checkTIFF16Bit() {
  std::string const path = "narrow_tiff_16bit.tif";
  gman::OutputTIFF output(path.c_str(), 2, 1);
  output.setPixel(0, 0, GMANColor(0.25f, 0.25f, 0.25f));
  output.setAlpha(0, 0, GMANAlpha(0.25f, 0.25f, 0.25f));
  output.setPixel(1, 0, GMANColor(1.0f, 1.0f, 1.0f));
  output.setAlpha(1, 0, GMANAlpha(1.0f, 1.0f, 1.0f));
  output.save(GMANOutput::RGBA, 1.0f, 1.0f, GMANQuantize{65535, 0, 65535, 0});

  ByteImage const img = readTIFFRaw(path);
  check(img.ok, "tiff check14: file reads back");
  if (img.ok) {
    check(img.bitsPerSample == 16, "tiff check14: BitsPerSample reads 16");
    check(img.at(0, 0, 0) == 16384, "tiff check14: 0.25 reads 16384 (got " + std::to_string(img.at(0, 0, 0)) + ")");
    check(img.at(1, 0, 0) == 65535, "tiff check14: 1 reads 65535 (got " + std::to_string(img.at(1, 0, 0)) + ")");
    check(img.at(0, 0, 3) == 16384,
          "tiff check14: alpha 0.25 reads 16384 (got " + std::to_string(img.at(0, 0, 3)) + ")");
  }
}

// 15. TIFF, RGBA: a NaN alpha writes 0, as today.
void checkTIFFAlphaNaN() {
  RtFloat const nan = std::numeric_limits<RtFloat>::quiet_NaN();
  std::string const path = "narrow_tiff_check15.tif";

  gman::OutputTIFF output(path.c_str(), 1, 1);
  output.setPixel(0, 0, GMANColor(0.5f, 0.5f, 0.5f));
  output.setAlpha(0, 0, GMANAlpha(nan, nan, nan));
  output.save(GMANOutput::RGBA, 1.0f, 1.0f, GMANQuantize{255, 0, 255, 0});

  ByteImage const img = readTIFFRaw(path);
  check(img.ok, "tiff check15: file reads back");
  if (img.ok) {
    check(img.samplesPerPixel == 4, "tiff check15: RGBA writes 4 samples");
    check(img.at(0, 0, 3) == 0, "tiff check15: NaN alpha narrows to 0 (got " + std::to_string(img.at(0, 0, 3)) + ")");
  }
}

} // namespace

int main() {
  checkReceives();

  checkFileRounding("pnm", savePNM, readPNM, false);
  checkFileRounding("tiff", saveTIFF, readTIFFRaw, true);
#ifdef GMAN_WITH_PNG
  checkFileRounding("png", savePNG, readPNGRaw, false);
#else
  std::printf("checked driver: none (GMAN_WITH_PNG is off)\n");
#endif

  checkTIFF16Bit();
  checkTIFFAlphaNaN();

  return checkSummary("output narrowing holds");
}
