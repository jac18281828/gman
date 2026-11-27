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
 * GMANOutput::save and reflected in the file a driver writes. A request the
 * driver cannot store stops the file at WorldBegin with one error, before
 * the frame renders and with nothing written.
 */

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

#include <tiffio.h>

#include "check.h"
#include "rungman.h"

namespace {

GMANRunResult runGman(std::string const& gman, std::string const& rib) { return ::runGman(gman, {"-w", rib}); }

// gman's license banner and Projection "orthographic"'s "FOV not set"
// warning both print regardless of Quantize; this strips the two before
// counting so only Quantize's own output remains.
std::string withoutUnrelatedNoise(std::string const& text) {
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

RawImage readTIFFRaw(std::string const& path) {
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

// A 32-bit IEEE float TIFF's samples, as libtiff decodes them, with the tags
// that say how they are stored.
struct FloatImage {
  bool ok = false;
  int width = 0, height = 0;
  int samplesPerPixel = 0;
  int bitsPerSample = 0;
  int sampleFormat = SAMPLEFORMAT_UINT;
  std::vector<float> data;

  float at(int x, int y, int channel) const {
    return data[(std::size_t)(y * width + x) * (std::size_t)samplesPerPixel + (std::size_t)channel];
  }
};

FloatImage readTIFFFloat(std::string const& path) {
  FloatImage img;
  TIFF* tif = TIFFOpen(path.c_str(), "r");
  if (tif == nullptr) {
    return img;
  }

  uint32_t width = 0, height = 0;
  uint16_t samplesPerPixel = 0;
  uint16_t bitsPerSample = 0;
  uint16_t sampleFormat = SAMPLEFORMAT_UINT;
  TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &width);
  TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &height);
  TIFFGetField(tif, TIFFTAG_SAMPLESPERPIXEL, &samplesPerPixel);
  TIFFGetField(tif, TIFFTAG_BITSPERSAMPLE, &bitsPerSample);
  TIFFGetFieldDefaulted(tif, TIFFTAG_SAMPLEFORMAT, &sampleFormat);

  img.width = (int)width;
  img.height = (int)height;
  img.samplesPerPixel = samplesPerPixel;
  img.bitsPerSample = bitsPerSample;
  img.sampleFormat = sampleFormat;
  if (bitsPerSample != 32 || sampleFormat != SAMPLEFORMAT_IEEEFP) {
    TIFFClose(tif);
    return img;
  }

  const std::size_t rowSamples = (std::size_t)width * (std::size_t)samplesPerPixel;
  std::vector<float> scanline(rowSamples);
  img.data.resize((std::size_t)height * rowSamples);
  bool ok = (std::size_t)TIFFScanlineSize(tif) == rowSamples * sizeof(float);
  for (uint32_t y = 0; ok && y < height; ++y) {
    if (TIFFReadScanline(tif, scanline.data(), y) < 0) {
      ok = false;
      break;
    }
    std::copy(scanline.begin(), scanline.end(), img.data.begin() + (std::ptrdiff_t)((std::size_t)y * rowSamples));
  }
  TIFFClose(tif);

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

PNMImage readPNM(std::string const& path) {
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
void checkQuantize8(std::string const& gman, std::string const& ribDir) {
  std::remove("quantize8.tif");
  GMANRunResult const r = runGman(gman, ribDir + "/quantize8.rib");
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

// 2. quantize16.rib, TIFF, 65535 1000 65535 0, half plane: 16 bits a
// sample, 4 samples a pixel; columns 0-7 read R, G and B 16384 and alpha
// 65535; columns 8-15 read 1000 on all four.
void checkQuantize16(std::string const& gman, std::string const& ribDir) {
  std::remove("quantize16.tif");
  GMANRunResult const r = runGman(gman, ribDir + "/quantize16.rib");
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

// 3. quantizefloat.rib, TIFF, 0 0 0 0, half plane lit to 3.0: 32-bit IEEE
// float, 4 samples a pixel, no Quantize output; columns 0-7 read R, G and B
// exactly 3.0 and alpha exactly 1.0; columns 8-15 read 0.0 on all four.
void checkQuantizeFloat(std::string const& gman, std::string const& ribDir) {
  std::remove("quantizefloat.tif");
  GMANRunResult const r = runGman(gman, ribDir + "/quantizefloat.rib");
  check(r.exitStatus == 0, "quantizefloat: renders");
  std::string const relevant = withoutUnrelatedNoise(r.output);
  check(relevant.empty(), "quantizefloat: prints no Quantize line (got \"" + relevant + "\")");

  FloatImage const img = readTIFFFloat("quantizefloat.tif");
  check(img.bitsPerSample == 32, "quantizefloat: 32 bits a sample");
  check(img.sampleFormat == SAMPLEFORMAT_IEEEFP, "quantizefloat: IEEE float sample format");
  check(img.samplesPerPixel == 4, "quantizefloat: 4 samples a pixel");
  check(img.ok, "quantizefloat: file reads back");
  if (!img.ok) {
    return;
  }
  for (int x = 0; x < 8; ++x) {
    check(img.at(x, 8, 0) == 3.0f && img.at(x, 8, 1) == 3.0f && img.at(x, 8, 2) == 3.0f,
          "quantizefloat: covered column " + std::to_string(x) + " reads R, G, B 3.0");
    check(img.at(x, 8, 3) == 1.0f, "quantizefloat: covered column " + std::to_string(x) + " reads alpha 1.0");
  }
  for (int x = 8; x < 16; ++x) {
    check(img.at(x, 8, 0) == 0.0f && img.at(x, 8, 1) == 0.0f && img.at(x, 8, 2) == 0.0f && img.at(x, 8, 3) == 0.0f,
          "quantizefloat: uncovered column " + std::to_string(x) + " reads 0.0 on all four");
  }
}

// True when `name` is one of the tokens on gman --version's "drivers:" line.
bool driverCompiled(std::string const& gman, std::string const& name) {
  GMANRunOptions options;
  options.capture = GMANRunOptions::Capture::stdoutOnly;
  GMANRunResult const version = ::runGman(gman, {"--version"}, options);
  std::istringstream lines(version.output);
  std::string line;
  while (std::getline(lines, line)) {
    if (line.rfind("drivers:", 0) != 0) {
      continue;
    }
    std::istringstream tokens(line.substr(std::string("drivers:").size()));
    std::string token;
    while (tokens >> token) {
      if (token == name) {
        return true;
      }
    }
  }
  return false;
}

// 4. quantize15_pnm.rib, .pnm, 15 0 15 0.5, half plane lit to 1: columns
// 0-7 read R, G and B 15; columns 8-15 read 0; the P6 maxval reads 15.
void checkQuantize15PNM(std::string const& gman, std::string const& ribDir) {
  std::remove("quantize15_pnm.pnm");
  GMANRunResult const r = runGman(gman, ribDir + "/quantize15_pnm.rib");
  check(r.exitStatus == 0, "quantize15_pnm: renders");

  PNMImage const img = readPNM("quantize15_pnm.pnm");
  check(img.ok, "quantize15_pnm: file reads back");
  if (!img.ok) {
    return;
  }
  check(img.maxval == 15, "quantize15_pnm: P6 maxval is 15 (got " + std::to_string(img.maxval) + ")");
  for (int x = 0; x < 8; ++x) {
    check(img.at(x, 8, 0) == 15 && img.at(x, 8, 1) == 15 && img.at(x, 8, 2) == 15,
          "quantize15_pnm: covered column " + std::to_string(x) + " reads R, G, B 15");
  }
  for (int x = 8; x < 16; ++x) {
    check(img.at(x, 8, 0) == 0 && img.at(x, 8, 1) == 0 && img.at(x, 8, 2) == 0,
          "quantize15_pnm: uncovered column " + std::to_string(x) + " reads 0");
  }
}

// 5. quantize15_tiff.rib, TIFF, 15 0 15 0.5, half plane lit to 1: 8 bits a
// sample, 4 samples a pixel; columns 0-7 read R, G, B and alpha 15; columns
// 8-15 read 0 on all four.
void checkQuantize15TIFF(std::string const& gman, std::string const& ribDir) {
  std::remove("quantize15_tiff.tif");
  GMANRunResult const r = runGman(gman, ribDir + "/quantize15_tiff.rib");
  check(r.exitStatus == 0, "quantize15_tiff: renders");

  RawImage const img = readTIFFRaw("quantize15_tiff.tif");
  check(img.ok, "quantize15_tiff: file reads back");
  if (!img.ok) {
    return;
  }
  check(img.bitsPerSample == 8, "quantize15_tiff: 8 bits a sample");
  check(img.samplesPerPixel == 4, "quantize15_tiff: 4 samples a pixel");
  for (int x = 0; x < 8; ++x) {
    check(img.at(x, 8, 0) == 15 && img.at(x, 8, 1) == 15 && img.at(x, 8, 2) == 15 && img.at(x, 8, 3) == 15,
          "quantize15_tiff: covered column " + std::to_string(x) + " reads R, G, B and alpha 15");
  }
  for (int x = 8; x < 16; ++x) {
    check(img.at(x, 8, 0) == 0 && img.at(x, 8, 1) == 0 && img.at(x, 8, 2) == 0 && img.at(x, 8, 3) == 0,
          "quantize15_tiff: uncovered column " + std::to_string(x) + " reads 0 on all four");
  }
}

// 6. quantize15_jpg.rib, JPEG, 15 0 15 0.5: renders and writes the file. The
// encoder is lossy, so no decoded value is asserted.
void checkQuantize15JPEG(std::string const& gman, std::string const& ribDir) {
  if (!driverCompiled(gman, "jpeg")) {
    std::printf("skipped quantize15_jpg: gman is built without libjpeg\n");
    return;
  }
  std::remove("quantize15_jpg.jpg");
  GMANRunResult const r = runGman(gman, ribDir + "/quantize15_jpg.rib");
  check(r.exitStatus == 0, "quantize15_jpg: renders");
  std::FILE* f = std::fopen("quantize15_jpg.jpg", "rb");
  check(f != nullptr, "quantize15_jpg: writes the file");
  if (f != nullptr) {
    std::fclose(f);
  }
}

// Counts non-overlapping occurrences of needle in haystack.
int countOccurrences(std::string const& haystack, std::string const& needle) {
  int count = 0;
  std::size_t pos = 0;
  while ((pos = haystack.find(needle, pos)) != std::string::npos) {
    ++count;
    pos += needle.size();
  }
  return count;
}

bool fileExists(std::string const& path) {
  std::FILE* f = std::fopen(path.c_str(), "rb");
  if (f == nullptr) {
    return false;
  }
  std::fclose(f);
  return true;
}

struct Refusal {
  std::string name;
  std::string extension;
  std::string driver;
  std::string code;
  std::string message;
};

// 7. quantizerefuse_<name>.rib: each request its driver cannot store stops
// the file at WorldBegin -- exit 1, one error naming the code and the
// request, no file, and a trace that reaches Quantize and stops before Sphere.
void checkRefusal(std::string const& gman, std::string const& ribDir, Refusal const& refusal) {
  std::string const label = "quantizerefuse_" + refusal.name;
  if (!driverCompiled(gman, refusal.driver)) {
    std::printf("skipped %s: gman is built without %s\n", label.c_str(), refusal.driver.c_str());
    return;
  }
  std::string const file = label + "." + refusal.extension;
  std::remove(file.c_str());

  GMANRunOptions options;
  options.timeoutSeconds = 20;
  GMANRunResult const r = ::runGman(gman, {"-d", ribDir + "/" + label + ".rib"}, options);
  check(!r.crashed && !r.timedOut, label + ": does not crash or hang");
  check(r.exitStatus == EXIT_FAILURE, label + ": exits 1");
  std::string const report = "ERROR: " + refusal.code + " -- " + refusal.message;
  check(countOccurrences(r.output, report) == 1, label + ": prints \"" + report + "\" exactly once");
  check(!fileExists(file), label + ": writes no " + file);
  check(r.output.find("Keyword token: Quantize") != std::string::npos, label + ": parses Quantize");
  check(r.output.find("Keyword token: Sphere") == std::string::npos, label + ": stops before the world block parses");
}

void checkRefusals(std::string const& gman, std::string const& ribDir) {
  std::vector<Refusal> const refusals = {
      {"pnm_max", "pnm", "pnm", "RIE_INCAPABLE", "PNM writes 16-bit samples; Quantize max 65536 cannot be honoured"},
      {"pnm_float", "pnm", "pnm", "RIE_INCAPABLE", "PNM writes integer samples; Quantize one 0 cannot be honoured"},
      {"png_max", "png", "png", "RIE_INCAPABLE", "PNG writes 16-bit samples; Quantize max 65536 cannot be honoured"},
      {"png_float", "png", "png", "RIE_INCAPABLE", "PNG writes integer samples; Quantize one 0 cannot be honoured"},
      {"jpeg_max", "jpg", "jpeg", "RIE_INCAPABLE", "JPEG writes 8-bit samples; Quantize max 65535 cannot be honoured"},
      {"jpeg_float", "jpg", "jpeg", "RIE_INCAPABLE", "JPEG writes integer samples; Quantize one 0 cannot be honoured"},
      {"tiff_max", "tif", "tiff", "RIE_INCAPABLE", "TIFF writes 16-bit samples; Quantize max 65536 cannot be honoured"},
      {"tiff_minmax", "tif", "tiff", "RIE_RANGE", "Quantize min 200 exceeds max 100"},
      {"pnm_negmin", "pnm", "pnm", "RIE_RANGE", "Quantize min -1 must not be negative"},
      {"tiff_negone", "tif", "tiff", "RIE_RANGE", "Quantize one -1 must not be negative"},
      {"tiff_negdither", "tif", "tiff", "RIE_RANGE",
       "Quantize dither amplitude -0.5 must be a finite number of 0 or greater"},
  };
  for (Refusal const& refusal : refusals) {
    checkRefusal(gman, ribDir, refusal);
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
  checkQuantize16(gman, ribDir);
  checkQuantizeFloat(gman, ribDir);
  checkQuantize15PNM(gman, ribDir);
  checkQuantize15TIFF(gman, ribDir);
  checkQuantize15JPEG(gman, ribDir);
  checkRefusals(gman, ribDir);

  return checkSummary("quantize holds");
}
