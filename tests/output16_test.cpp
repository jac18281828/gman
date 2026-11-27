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
 * gman::OutputPNM and gman::OutputPNG write 16-bit samples, most significant
 * byte first, when Quantize's max exceeds 255, and 8-bit samples otherwise.
 * A 3 by 2 image of differing pixels is saved at gain 1 and gamma 1, and
 * each file is read back without the writer's code: the PNM by its P6
 * grammar, the PNG with libpng and no transform (tests/ is exempt from
 * AGENTS.md's one-includer rule for png.h). Every expected sample is
 * floor(max * v + 0.5), computed here in double. A max of 65536 stops the
 * file with RIE_INCAPABLE naming 16-bit samples.
 */

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <string>
#include <vector>

#include "check.h"

#ifdef GMAN_WITH_PNG
extern "C" {
#include <png.h>
}
#endif

#include "gmanerror.h"
#include "gmanoutput.h"
#include "gmanoutputpnm.h"
#include "ri.h"
#ifdef GMAN_WITH_PNG
#include "gmanoutputpng.h"
#endif

namespace {

constexpr int kWidth = 3;
constexpr int kHeight = 2;
constexpr int kPixels = kWidth * kHeight;

struct Pixel {
  GMANColor colour;
  GMANAlpha alpha;
  double coverage;
};

// Pixel 0 holds 0.3, pixel 2 holds 1.0 and pixel 3 holds 0. Pixel 4 has
// coverage (0.2 + 0.5 + 0.8) / 3 = 0.5, a power of two, so its straight
// colour divides exactly.
std::array<Pixel, kPixels> const kPixelTable = {{
    {GMANColor(0.3f, 1.0f, 0.0f), GMANAlpha(1.0f, 1.0f, 1.0f), 1.0},
    {GMANColor(0.1f, 0.5f, 0.9f), GMANAlpha(1.0f, 1.0f, 1.0f), 1.0},
    {GMANColor(1.0f, 1.0f, 1.0f), GMANAlpha(1.0f, 1.0f, 1.0f), 1.0},
    {GMANColor(0.0f, 0.0f, 0.0f), GMANAlpha(0.0f, 0.0f, 0.0f), 0.0},
    {GMANColor(0.2f, 0.3f, 0.4f), GMANAlpha(0.2f, 0.5f, 0.8f), 0.5},
    {GMANColor(0.3f, 0.3f, 0.3f), GMANAlpha(1.0f, 1.0f, 1.0f), 1.0},
}};

constexpr int kPartialPixel = 4;

template <typename Output> void fill(Output& output) {
  for (int i = 0; i < kPixels; ++i) {
    int const x = i % kWidth;
    int const y = i / kWidth;
    output.setPixel(x, y, kPixelTable[static_cast<std::size_t>(i)].colour);
    output.setAlpha(x, y, kPixelTable[static_cast<std::size_t>(i)].alpha);
  }
}

std::uint32_t expectedSample(double v, int max) {
  return static_cast<std::uint32_t>(std::clamp(std::floor(max * v + 0.5), 0.0, static_cast<double>(max)));
}

std::array<double, 3> colourOf(Pixel const& pixel) {
  return {pixel.colour.getRed(), pixel.colour.getGreen(), pixel.colour.getBlue()};
}

std::vector<unsigned char> readFile(std::string const& path) {
  std::FILE* const file = std::fopen(path.c_str(), "rb");
  if (file == nullptr) {
    return {};
  }
  std::vector<unsigned char> bytes;
  std::array<unsigned char, 4096> chunk{};
  std::size_t got = 0;
  while ((got = std::fread(chunk.data(), 1, chunk.size(), file)) > 0) {
    bytes.insert(bytes.end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(got));
  }
  std::fclose(file);
  return bytes;
}

struct PNMFile {
  bool ok = false;
  std::string magic;
  std::string size;
  std::string maxval;
  std::vector<unsigned char> samples;
};

PNMFile readPNM(std::string const& path) {
  PNMFile pnm;
  std::vector<unsigned char> const bytes = readFile(path);
  std::vector<std::string> lines;
  auto at = bytes.begin();
  while (lines.size() < 3) {
    auto const eol = std::find(at, bytes.end(), static_cast<unsigned char>('\n'));
    if (eol == bytes.end()) {
      return pnm;
    }
    lines.emplace_back(at, eol);
    at = std::next(eol);
  }
  pnm.magic = lines[0];
  pnm.size = lines[1];
  pnm.maxval = lines[2];
  pnm.samples.assign(at, bytes.end());
  pnm.ok = true;
  return pnm;
}

std::uint32_t sampleAt(std::vector<unsigned char> const& bytes, std::size_t index, std::size_t bytesPerSample) {
  if (bytesPerSample == 2) {
    return static_cast<std::uint32_t>(bytes[2 * index]) << 8 | bytes[2 * index + 1];
  }
  return bytes[index];
}

void checkPNMSamples(std::string const& label, PNMFile const& pnm, int max, std::size_t bytesPerSample) {
  check(pnm.samples.size() == 3 * kPixels * bytesPerSample,
        label + ": holds " + std::to_string(3 * kPixels * bytesPerSample) + " sample bytes (got " +
            std::to_string(pnm.samples.size()) + ")");
  if (pnm.samples.size() != 3 * kPixels * bytesPerSample) {
    return;
  }
  for (int i = 0; i < kPixels; ++i) {
    std::array<double, 3> const colour = colourOf(kPixelTable[static_cast<std::size_t>(i)]);
    for (std::size_t channel = 0; channel < 3; ++channel) {
      std::size_t const index = 3 * static_cast<std::size_t>(i) + channel;
      std::uint32_t const expected = expectedSample(colour[channel], max);
      std::uint32_t const actual = sampleAt(pnm.samples, index, bytesPerSample);
      check(actual == expected, label + ": pixel " + std::to_string(i) + " channel " + std::to_string(channel) +
                                    " reads " + std::to_string(actual) + " (expected " + std::to_string(expected) +
                                    ")");
    }
  }
}

void savePNM(std::string const& path, GMANQuantize const& request) {
  gman::OutputPNM output(path.c_str(), kWidth, kHeight);
  fill(output);
  output.save(GMANOutput::RGB, 1.0f, 1.0f, request);
}

void checkPNM16() {
  std::string const path = "output16.pnm";
  savePNM(path, GMANQuantize{65535, 0, 65535, 0});
  PNMFile const pnm = readPNM(path);
  check(pnm.ok, "pnm 65535: file reads back");
  if (!pnm.ok) {
    return;
  }
  check(pnm.magic == "P6", "pnm 65535: magic reads P6 (got " + pnm.magic + ")");
  check(pnm.size == "3 2", "pnm 65535: size reads 3 2 (got " + pnm.size + ")");
  check(pnm.maxval == "65535", "pnm 65535: maxval reads 65535 (got " + pnm.maxval + ")");
  checkPNMSamples("pnm 65535", pnm, 65535, 2);
  if (pnm.samples.size() >= 2) {
    check(pnm.samples[0] == 0x4c && pnm.samples[1] == 0xcd, "pnm 65535: 0.3 reads bytes 4c cd, high byte first");
  }
}

void checkPNMOddMax() {
  std::string const path = "output16_1000.pnm";
  savePNM(path, GMANQuantize{1000, 0, 1000, 0});
  PNMFile const pnm = readPNM(path);
  check(pnm.ok, "pnm 1000: file reads back");
  if (!pnm.ok) {
    return;
  }
  check(pnm.maxval == "1000", "pnm 1000: maxval reads 1000 (got " + pnm.maxval + ")");
  checkPNMSamples("pnm 1000", pnm, 1000, 2);
}

void checkPNM256() {
  const std::string path = "output16_256.pnm";
  savePNM(path, GMANQuantize{256, 0, 256, 0});
  const auto pnm = readPNM(path);
  check(pnm.ok, "pnm 256: file reads back");
  if (!pnm.ok) {
    return;
  }
  check(pnm.maxval == "256", "pnm 256: maxval reads 256 (got " + pnm.maxval + ")");
  checkPNMSamples("pnm 256", pnm, 256, 2);
  constexpr std::size_t kWhitePixelOffset = 2 * 3 * 2;
  if (pnm.samples.size() >= kWhitePixelOffset + 2) {
    check(pnm.samples[kWhitePixelOffset] == 0x01 && pnm.samples[kWhitePixelOffset + 1] == 0x00,
          "pnm 256: 1.0 reads bytes 01 00, high byte first");
  }
}

void checkPNM8() {
  std::string const path = "output8.pnm";
  savePNM(path, GMANQuantize{255, 0, 255, 0});
  PNMFile const pnm = readPNM(path);
  check(pnm.ok, "pnm 255: file reads back");
  if (!pnm.ok) {
    return;
  }
  check(pnm.maxval == "255", "pnm 255: maxval reads 255 (got " + pnm.maxval + ")");
  checkPNMSamples("pnm 255", pnm, 255, 1);
}

void checkRefusal(std::string const& label, std::string const& expectedMessage, auto const& save) {
  bool threw = false;
  try {
    save();
  } catch (GMANError const& error) {
    threw = true;
    check(error.getCode() == RIE_INCAPABLE, label + ": carries RIE_INCAPABLE");
    check(expectedMessage == error.getMessage(),
          label + ": message reads \"" + expectedMessage + "\" (got \"" + error.getMessage() + "\")");
  }
  check(threw, label + ": throws");
}

void checkPNMRefusal() {
  checkRefusal("pnm 65536", "PNM writes 16-bit samples; Quantize max 65536 cannot be honoured",
               [] { savePNM("output16_refused.pnm", GMANQuantize{65536, 0, 65536, 0}); });
}

#ifdef GMAN_WITH_PNG

struct PNGFile {
  bool ok = false;
  int bitDepth = 0;
  int colourType = -1;
  int channels = 0;
  png_uint_32 width = 0;
  png_uint_32 height = 0;
  png_uint_32 rowBytes = 0;
  std::vector<unsigned char> bytes;
};

PNGFile readPNG(std::string const& path) {
  PNGFile png;
  std::FILE* const file = std::fopen(path.c_str(), "rb");
  if (file == nullptr) {
    return png;
  }
  png_structp reader = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
  png_infop info = png_create_info_struct(reader);
  if (setjmp(png_jmpbuf(reader))) {
    png_destroy_read_struct(&reader, &info, nullptr);
    std::fclose(file);
    return PNGFile();
  }
  png_init_io(reader, file);
  png_read_info(reader, info);
  png_get_IHDR(reader, info, &png.width, &png.height, &png.bitDepth, &png.colourType, nullptr, nullptr, nullptr);
  png.channels = png_get_channels(reader, info);
  png.rowBytes = static_cast<png_uint_32>(png_get_rowbytes(reader, info));

  png.bytes.resize(static_cast<std::size_t>(png.rowBytes) * png.height);
  std::vector<png_bytep> rows(png.height);
  for (png_uint_32 y = 0; y < png.height; ++y) {
    rows[y] = png.bytes.data() + static_cast<std::size_t>(y) * png.rowBytes;
  }
  png_read_image(reader, rows.data());
  png_destroy_read_struct(&reader, &info, nullptr);
  std::fclose(file);
  png.ok = true;
  return png;
}

std::vector<double> expectedValues(GMANOutput::DisplayMode mode, Pixel const& pixel) {
  bool const straight = mode != GMANOutput::RGB;
  std::vector<double> values;
  if (mode != GMANOutput::A) {
    std::array<double, 3> colour = colourOf(pixel);
    for (double const channel : colour) {
      bool const divides = straight && pixel.coverage > 0 && pixel.coverage < 1;
      values.push_back(divides ? channel / pixel.coverage : channel);
    }
  }
  if (mode != GMANOutput::RGB) {
    values.push_back(pixel.coverage);
  }
  return values;
}

void checkPNGMode(std::string const& name, GMANOutput::DisplayMode mode, int colourType, int channels) {
  std::string const label = "png 65535 " + name;
  std::string const path = "output16_" + name + ".png";
  gman::OutputPNG output(path.c_str(), kWidth, kHeight);
  fill(output);
  output.save(mode, 1.0f, 1.0f, GMANQuantize{65535, 0, 65535, 0});

  PNGFile const png = readPNG(path);
  check(png.ok, label + ": file reads back");
  if (!png.ok) {
    return;
  }
  check(png.bitDepth == 16, label + ": bit depth reads 16 (got " + std::to_string(png.bitDepth) + ")");
  check(png.colourType == colourType,
        label + ": colour type reads " + std::to_string(colourType) + " (got " + std::to_string(png.colourType) + ")");
  check(png.channels == channels,
        label + ": " + std::to_string(channels) + " channels (got " + std::to_string(png.channels) + ")");
  check(png.rowBytes == png.width * static_cast<png_uint_32>(channels) * 2,
        label + ": two bytes a sample (row bytes " + std::to_string(png.rowBytes) + ")");
  if (png.bitDepth != 16 || png.channels != channels) {
    return;
  }
  for (int i = 0; i < kPixels; ++i) {
    std::vector<double> const values = expectedValues(mode, kPixelTable[static_cast<std::size_t>(i)]);
    std::size_t const row = static_cast<std::size_t>(i / kWidth);
    std::size_t const column = static_cast<std::size_t>(i % kWidth);
    for (std::size_t channel = 0; channel < values.size(); ++channel) {
      std::size_t const offset = row * png.rowBytes + (column * values.size() + channel) * 2;
      std::uint32_t const actual = static_cast<std::uint32_t>(png.bytes[offset]) << 8 | png.bytes[offset + 1];
      std::uint32_t const expected = expectedSample(values[channel], 65535);
      check(actual == expected, label + ": pixel " + std::to_string(i) + " sample " + std::to_string(channel) +
                                    " reads " + std::to_string(actual) + " (expected " + std::to_string(expected) +
                                    ")");
    }
  }
}

void checkPNGPartialCoverage() {
  std::string const path = "output16_partial.png";
  gman::OutputPNG output(path.c_str(), kWidth, kHeight);
  fill(output);
  output.save(GMANOutput::RGBA, 1.0f, 1.0f, GMANQuantize{65535, 0, 65535, 0});

  PNGFile const png = readPNG(path);
  check(png.ok && png.bitDepth == 16, "png partial: reads back at 16 bits");
  if (!png.ok || png.bitDepth != 16) {
    return;
  }
  Pixel const& pixel = kPixelTable[kPartialPixel];
  std::size_t const row = kPartialPixel / kWidth;
  std::size_t const column = kPartialPixel % kWidth;
  std::size_t const offset = row * png.rowBytes + column * 4 * 2;
  auto const word = [&png, offset](std::size_t channel) {
    return static_cast<std::uint32_t>(png.bytes[offset + 2 * channel]) << 8 | png.bytes[offset + 2 * channel + 1];
  };
  check(word(3) == 32768, "png partial: alpha at coverage 0.5 reads 32768 (got " + std::to_string(word(3)) + ")");
  std::array<double, 3> const colour = colourOf(pixel);
  for (std::size_t channel = 0; channel < 3; ++channel) {
    std::uint32_t const expected = expectedSample(colour[channel] / pixel.coverage, 65535);
    check(word(channel) == expected, "png partial: colour " + std::to_string(channel) + " divided by coverage reads " +
                                         std::to_string(word(channel)) + " (expected " + std::to_string(expected) +
                                         ")");
  }
}

void checkPNG8() {
  std::string const path = "output8.png";
  gman::OutputPNG output(path.c_str(), kWidth, kHeight);
  fill(output);
  output.save(GMANOutput::RGB, 1.0f, 1.0f, GMANQuantize{255, 0, 255, 0});

  PNGFile const png = readPNG(path);
  check(png.ok, "png 255: file reads back");
  if (!png.ok) {
    return;
  }
  check(png.bitDepth == 8, "png 255: bit depth reads 8 (got " + std::to_string(png.bitDepth) + ")");
  check(png.rowBytes == png.width * 3, "png 255: one byte a sample");
}

void checkPNGRefusal() {
  checkRefusal("png 65536", "PNG writes 16-bit samples; Quantize max 65536 cannot be honoured", [] {
    gman::OutputPNG output("output16_refused.png", kWidth, kHeight);
    fill(output);
    output.save(GMANOutput::RGB, 1.0f, 1.0f, GMANQuantize{65536, 0, 65536, 0});
  });
}

#endif // GMAN_WITH_PNG

} // namespace

int main() {
  checkPNM16();
  checkPNMOddMax();
  checkPNM256();
  checkPNM8();
  checkPNMRefusal();
#ifdef GMAN_WITH_PNG
  checkPNGMode("rgb", GMANOutput::RGB, PNG_COLOR_TYPE_RGB, 3);
  checkPNGMode("rgba", GMANOutput::RGBA, PNG_COLOR_TYPE_RGB_ALPHA, 4);
  checkPNGMode("a", GMANOutput::A, PNG_COLOR_TYPE_GRAY, 1);
  checkPNGPartialCoverage();
  checkPNG8();
  checkPNGRefusal();
#else
  std::printf("output16: png checks skipped (GMAN_WITH_PNG is off)\n");
#endif

  return checkSummary("output16 holds");
}
