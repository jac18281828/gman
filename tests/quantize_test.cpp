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
 * RIB's Quantize keyword, end to end through gman: parsed, honoured by
 * GMANOutput::save and reflected in the file a driver writes. A request
 * TIFF's 16-bit range cannot hold, sent to PNM, falls back and warns.
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <sys/wait.h>

#include <tiffio.h>

#include "check.h"

namespace {

struct Result {
  int exitStatus;
  std::string output;
};

Result runGman(const std::string& gman, const std::string& rib) {
  const std::string command = "\"" + gman + "\" -w \"" + rib + "\" 2>&1";
  std::FILE* pipe = popen(command.c_str(), "r");
  Result result{-1, ""};
  if (pipe == nullptr) {
    return result;
  }
  char buffer[512];
  while (std::fgets(buffer, sizeof buffer, pipe) != nullptr) {
    result.output += buffer;
  }
  const int status = pclose(pipe);
  result.exitStatus = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
  return result;
}

int countLines(const std::string& text) {
  if (text.empty()) {
    return 0;
  }
  int lines = 0;
  for (char c : text) {
    if (c == '\n') {
      ++lines;
    }
  }
  return lines;
}

// gman's version banner prints unconditionally, ahead of argument parsing,
// so no log level ever suppresses it; Projection "orthographic" always
// defaults fov and warns about it, a pre-existing defect this unit does
// not own. Neither line is Quantize's own output, so this strips both
// before counting.
std::string withoutUnrelatedNoise(const std::string& text) {
  std::string result;
  std::size_t pos = 0;
  while (pos < text.size()) {
    std::size_t const eol = text.find('\n', pos);
    std::string const line = text.substr(pos, eol == std::string::npos ? std::string::npos : eol - pos);
    if (line.find("LGPL-2.1-or-later") == std::string::npos && line.find("FOV not set") == std::string::npos) {
      result += line + "\n";
    }
    if (eol == std::string::npos) {
      break;
    }
    pos = eol + 1;
  }
  return result;
}

// The raw, unreinterpreted samples a driver wrote -- no RGBA
// reinterpretation, so an 8- or 16-bit request reads back exactly what
// GMANOutput::save resolved. Read TIFF rows in order: LZW refuses random
// access.
struct RawImage {
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

RawImage readTIFFRaw(const std::string& path) {
  RawImage img;
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

struct PNMImage {
  bool ok = false;
  int width = 0, height = 0, maxval = 0;
  std::vector<unsigned char> data;

  unsigned char at(int x, int y, int channel) const {
    return data[(std::size_t)(y * width + x) * 3 + (std::size_t)channel];
  }
};

PNMImage readPNM(const std::string& path) {
  PNMImage img;
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
  img.maxval = maxval;
  img.data.resize((std::size_t)width * (std::size_t)height * 3);
  img.ok = std::fread(img.data.data(), 1, img.data.size(), f) == img.data.size();
  std::fclose(f);
  return img;
}

// 1. quantize8.rib, TIFF, 255 10 200 0, half plane: columns 0-7 read R, G
// and B 64 and alpha 200; columns 8-15 read 10 on all four.
void checkQuantize8(const std::string& gman, const std::string& ribDir) {
  std::remove("quantize8.tif");
  Result const r = runGman(gman, ribDir + "/quantize8.rib");
  check(r.exitStatus == 0, "quantize8: renders");
  check(r.output.find("unrecognized request") == std::string::npos, "quantize8: Quantize parses, not skipped");

  RawImage const img = readTIFFRaw("quantize8.tif");
  check(img.ok, "quantize8: file reads back");
  if (!img.ok) {
    return;
  }
  check(img.bitsPerSample == 8, "quantize8: 8 bits a sample");
  for (int x = 0; x < 8; ++x) {
    check(img.at(x, 8, 0) == 64 && img.at(x, 8, 1) == 64 && img.at(x, 8, 2) == 64,
          "quantize8: covered column " + std::to_string(x) + " reads R, G, B 64");
    check(img.at(x, 8, 3) == 200, "quantize8: covered column " + std::to_string(x) + " reads alpha 200");
  }
  for (int x = 8; x < 16; ++x) {
    check(img.at(x, 8, 0) == 10 && img.at(x, 8, 1) == 10 && img.at(x, 8, 2) == 10 && img.at(x, 8, 3) == 10,
          "quantize8: uncovered column " + std::to_string(x) + " reads 10 on all four");
  }
}

// 2. quantize_fallback.rib, .pnm, 65535 0 65535 0, full plane: exactly one
// line of output naming Quantize and the file; the P6 header's maxval is
// 255 and every sample is 64.
void checkQuantizeFallback(const std::string& gman, const std::string& ribDir) {
  std::remove("quantize_fallback.pnm");
  Result const r = runGman(gman, ribDir + "/quantize_fallback.rib");
  check(r.exitStatus == 0, "quantize_fallback: renders");
  std::string const relevant = withoutUnrelatedNoise(r.output);
  check(countLines(relevant) == 1, "quantize_fallback: exactly one line of Quantize's own output (got " +
                                       std::to_string(countLines(relevant)) + ")");
  check(relevant.find("Quantize") != std::string::npos, "quantize_fallback: names Quantize");
  check(relevant.find("quantize_fallback.pnm") != std::string::npos, "quantize_fallback: names the output file");

  PNMImage const img = readPNM("quantize_fallback.pnm");
  check(img.ok, "quantize_fallback: file reads back");
  if (!img.ok) {
    return;
  }
  check(img.maxval == 255, "quantize_fallback: P6 maxval is 255");
  bool everySampleIs64 = true;
  for (int y = 0; y < img.height && everySampleIs64; ++y) {
    for (int x = 0; x < img.width && everySampleIs64; ++x) {
      for (int c = 0; c < 3; ++c) {
        if (img.at(x, y, c) != 64) {
          everySampleIs64 = false;
        }
      }
    }
  }
  check(everySampleIs64, "quantize_fallback: every sample reads 64");
}

// 3. quantize16.rib, TIFF, 65535 1000 65535 0, half plane: 16 bits a
// sample, 4 samples a pixel; columns 0-7 read R, G and B 16384 and alpha
// 65535; columns 8-15 read 1000 on all four.
void checkQuantize16(const std::string& gman, const std::string& ribDir) {
  std::remove("quantize16.tif");
  Result const r = runGman(gman, ribDir + "/quantize16.rib");
  check(r.exitStatus == 0, "quantize16: renders");

  RawImage const img = readTIFFRaw("quantize16.tif");
  check(img.ok, "quantize16: file reads back");
  if (!img.ok) {
    return;
  }
  check(img.bitsPerSample == 16, "quantize16: 16 bits a sample");
  check(img.samplesPerPixel == 4, "quantize16: 4 samples a pixel");
  for (int x = 0; x < 8; ++x) {
    check(img.at(x, 8, 0) == 16384 && img.at(x, 8, 1) == 16384 && img.at(x, 8, 2) == 16384,
          "quantize16: covered column " + std::to_string(x) + " reads R, G, B 16384");
    check(img.at(x, 8, 3) == 65535, "quantize16: covered column " + std::to_string(x) + " reads alpha 65535");
  }
  for (int x = 8; x < 16; ++x) {
    check(img.at(x, 8, 0) == 1000 && img.at(x, 8, 1) == 1000 && img.at(x, 8, 2) == 1000 && img.at(x, 8, 3) == 1000,
          "quantize16: uncovered column " + std::to_string(x) + " reads 1000 on all four");
  }
}

} // namespace

int main(int argc, char* argv[]) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s <gman-binary> <tests/rib-dir>\n", argv[0]);
    return 2;
  }
  const std::string gman = argv[1];
  const std::string ribDir = argv[2];

  checkQuantize8(gman, ribDir);
  checkQuantizeFallback(gman, ribDir);
  checkQuantize16(gman, ribDir);

  return checkSummary("quantize holds");
}
