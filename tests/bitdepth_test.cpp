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
 * The sample depth of file output, chosen by gman --bit-depth or by
 * Quantize "rgba", end to end through the gman binary. A half plane of 0.3
 * covers columns 0 to 7 of a 16 by 16 image. At 16 bits 0.3 reads 19661
 * exactly, or 19660 and 19661 under a dither amplitude of 0.5, both
 * occurring; an uncovered pixel reads 0. With no Quantize, --bit-depth 16
 * writes 16-bit PNM, PNG and TIFF and --bit-depth 8 changes nothing. A
 * Quantize that contradicts the flag stops the file at WorldBegin with
 * RIE_CONSISTENCY, a JPEG refuses 16 bits with RIE_INCAPABLE, and OpenEXR,
 * which writes float, ignores the flag.
 */

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

#include <tiffio.h>

#ifdef GMAN_WITH_PNG
extern "C" {
#include <png.h>
}
#endif

#include "check.h"
#include "rungman.h"

namespace {

constexpr int kSide = 16;
constexpr int kCoveredColumns = 8;
constexpr std::uint32_t kCoveredExact = 19661;
constexpr std::uint32_t kCoveredLow = 19660;

struct Raster {
  bool ok = false;
  int width = 0;
  int height = 0;
  int channels = 0;
  int bits = 0;
  int maxval = 0;
  std::vector<std::uint32_t> samples;

  std::uint32_t at(int x, int y, int channel) const {
    return samples[(static_cast<std::size_t>(y * width + x) * static_cast<std::size_t>(channels)) +
                   static_cast<std::size_t>(channel)];
  }
};

std::vector<unsigned char> readBytes(std::string const& path) {
  std::FILE* const file = std::fopen(path.c_str(), "rb");
  if (file == nullptr) {
    return {};
  }
  std::vector<unsigned char> bytes;
  std::vector<unsigned char> chunk(4096);
  std::size_t got = 0;
  while ((got = std::fread(chunk.data(), 1, chunk.size(), file)) > 0) {
    bytes.insert(bytes.end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(got));
  }
  std::fclose(file);
  return bytes;
}

bool fileExists(std::string const& path) {
  std::FILE* const file = std::fopen(path.c_str(), "rb");
  if (file == nullptr) {
    return false;
  }
  std::fclose(file);
  return true;
}

Raster readPNM(std::string const& path) {
  Raster raster;
  std::FILE* const file = std::fopen(path.c_str(), "rb");
  if (file == nullptr) {
    return raster;
  }
  char magic[4] = {};
  int width = 0;
  int height = 0;
  int maxval = 0;
  if (std::fscanf(file, "%3s %d %d %d", magic, &width, &height, &maxval) != 4 || std::strcmp(magic, "P6") != 0) {
    std::fclose(file);
    return raster;
  }
  std::fgetc(file);

  std::size_t const bytesPerSample = maxval > 255 ? 2 : 1;
  std::size_t const count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 3;
  std::vector<unsigned char> bytes(count * bytesPerSample);
  bool const complete = std::fread(bytes.data(), 1, bytes.size(), file) == bytes.size();
  std::fclose(file);
  if (!complete) {
    return raster;
  }
  raster.samples.resize(count);
  for (std::size_t i = 0; i < count; ++i) {
    if (bytesPerSample == 2) {
      raster.samples[i] = static_cast<std::uint32_t>(bytes[2 * i]) << 8 | bytes[2 * i + 1];
    } else {
      raster.samples[i] = bytes[i];
    }
  }
  raster.width = width;
  raster.height = height;
  raster.channels = 3;
  raster.bits = maxval > 255 ? 16 : 8;
  raster.maxval = maxval;
  raster.ok = true;
  return raster;
}

Raster readTIFF(std::string const& path) {
  Raster raster;
  TIFF* const tif = TIFFOpen(path.c_str(), "r");
  if (tif == nullptr) {
    return raster;
  }
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint16_t channels = 0;
  std::uint16_t bits = 8;
  TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &width);
  TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &height);
  TIFFGetField(tif, TIFFTAG_SAMPLESPERPIXEL, &channels);
  TIFFGetField(tif, TIFFTAG_BITSPERSAMPLE, &bits);

  std::size_t const rowSamples = static_cast<std::size_t>(width) * channels;
  std::size_t const bytesPerSample = bits == 16 ? 2 : 1;
  std::vector<unsigned char> row(static_cast<std::size_t>(TIFFScanlineSize(tif)));
  raster.samples.resize(rowSamples * height);
  bool complete = row.size() >= rowSamples * bytesPerSample;
  for (std::uint32_t y = 0; complete && y < height; ++y) {
    complete = TIFFReadScanline(tif, row.data(), y) >= 0;
    for (std::size_t i = 0; complete && i < rowSamples; ++i) {
      if (bytesPerSample == 2) {
        std::uint16_t sample = 0;
        std::memcpy(&sample, &row[2 * i], sizeof(sample));
        raster.samples[y * rowSamples + i] = sample;
      } else {
        raster.samples[y * rowSamples + i] = row[i];
      }
    }
  }
  TIFFClose(tif);
  raster.width = static_cast<int>(width);
  raster.height = static_cast<int>(height);
  raster.channels = channels;
  raster.bits = bits;
  raster.ok = complete;
  return raster;
}

#ifdef GMAN_WITH_PNG
Raster readPNG(std::string const& path) {
  Raster raster;
  std::FILE* const file = std::fopen(path.c_str(), "rb");
  if (file == nullptr) {
    return raster;
  }
  png_structp reader = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
  png_infop info = png_create_info_struct(reader);
  if (setjmp(png_jmpbuf(reader))) {
    png_destroy_read_struct(&reader, &info, nullptr);
    std::fclose(file);
    return Raster();
  }
  png_init_io(reader, file);
  png_read_info(reader, info);
  png_uint_32 width = 0;
  png_uint_32 height = 0;
  int bits = 0;
  int colourType = 0;
  png_get_IHDR(reader, info, &width, &height, &bits, &colourType, nullptr, nullptr, nullptr);
  int const channels = png_get_channels(reader, info);
  std::size_t const rowBytes = png_get_rowbytes(reader, info);
  std::vector<unsigned char> bytes(rowBytes * height);
  std::vector<png_bytep> rows(height);
  for (png_uint_32 y = 0; y < height; ++y) {
    rows[y] = bytes.data() + static_cast<std::size_t>(y) * rowBytes;
  }
  png_read_image(reader, rows.data());
  png_destroy_read_struct(&reader, &info, nullptr);
  std::fclose(file);

  std::size_t const bytesPerSample = bits == 16 ? 2 : 1;
  std::size_t const rowSamples = static_cast<std::size_t>(width) * static_cast<std::size_t>(channels);
  raster.samples.resize(rowSamples * height);
  for (std::size_t y = 0; y < height; ++y) {
    for (std::size_t i = 0; i < rowSamples; ++i) {
      unsigned char const* const at = &bytes[y * rowBytes + i * bytesPerSample];
      if (bytesPerSample == 2) {
        raster.samples[y * rowSamples + i] = static_cast<std::uint32_t>(at[0]) << 8 | at[1];
      } else {
        raster.samples[y * rowSamples + i] = at[0];
      }
    }
  }
  raster.width = static_cast<int>(width);
  raster.height = static_cast<int>(height);
  raster.channels = channels;
  raster.bits = bits;
  raster.ok = true;
  return raster;
}
#endif

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

using Flags = std::vector<std::string>;

// Renders ribDir/<fixture>.rib, removing <fixture>.<extension> first.
GMANRunResult render(std::string const& gman, std::string const& ribDir, std::string const& fixture,
                     std::string const& extension, Flags const& flags) {
  std::remove((fixture + "." + extension).c_str());
  Flags arguments{"-w"};
  arguments.insert(arguments.end(), flags.begin(), flags.end());
  arguments.push_back(ribDir + "/" + fixture + ".rib");
  GMANRunOptions options;
  options.timeoutSeconds = 20;
  return ::runGman(gman, arguments, options);
}

int countOccurrences(std::string const& haystack, std::string const& needle) {
  int count = 0;
  std::size_t pos = 0;
  while ((pos = haystack.find(needle, pos)) != std::string::npos) {
    ++count;
    pos += needle.size();
  }
  return count;
}

// Every covered sample of every channel reads one of `allowed`; with
// `bothOccur` set each of the two values reads at least once. Every
// uncovered sample of the first three channels reads 0.
void checkHalfPlane(std::string const& label, Raster const& raster, std::vector<std::uint32_t> const& allowed,
                    bool bothOccur) {
  check(raster.width == kSide && raster.height == kSide, label + ": reads back 16 by 16");
  if (raster.width != kSide || raster.height != kSide) {
    return;
  }
  std::vector<int> seen(allowed.size(), 0);
  bool coveredAllowed = true;
  bool uncoveredZero = true;
  for (int y = 0; y < kSide; ++y) {
    for (int x = 0; x < kSide; ++x) {
      for (int channel = 0; channel < 3; ++channel) {
        std::uint32_t const sample = raster.at(x, y, channel);
        if (x >= kCoveredColumns) {
          uncoveredZero = uncoveredZero && sample == 0;
          continue;
        }
        auto const found = std::find(allowed.begin(), allowed.end(), sample);
        if (found == allowed.end()) {
          coveredAllowed = false;
          continue;
        }
        ++seen[static_cast<std::size_t>(std::distance(allowed.begin(), found))];
      }
    }
  }
  std::string reads = std::to_string(allowed.front());
  if (allowed.size() > 1) {
    reads += " or " + std::to_string(allowed.back());
  }
  check(coveredAllowed, label + ": every covered sample reads " + reads);
  check(uncoveredZero, label + ": every uncovered sample reads 0");
  if (bothOccur) {
    check(std::all_of(seen.begin(), seen.end(), [](int count) { return count > 0; }),
          label + ": both neighbouring codes occur");
  }
}

void checkDithered(std::string const& label, Raster const& raster) {
  checkHalfPlane(label, raster, {kCoveredLow, kCoveredExact}, true);
}

void checkExact(std::string const& label, Raster const& raster) {
  checkHalfPlane(label, raster, {kCoveredExact}, false);
}

void checkFlag16(std::string const& gman, std::string const& ribDir) {
  GMANRunResult const pnmRun = render(gman, ribDir, "bitdepth_pnm", "pnm", {"--bit-depth", "16"});
  check(pnmRun.exitStatus == 0, "flag 16 pnm: renders");
  Raster const pnm = readPNM("bitdepth_pnm.pnm");
  check(pnm.ok, "flag 16 pnm: file reads back");
  if (pnm.ok) {
    check(pnm.maxval == 65535, "flag 16 pnm: maxval reads 65535 (got " + std::to_string(pnm.maxval) + ")");
    checkDithered("flag 16 pnm", pnm);
  }

#ifdef GMAN_WITH_PNG
  if (driverCompiled(gman, "png")) {
    GMANRunResult const pngRun = render(gman, ribDir, "bitdepth_png", "png", {"--bit-depth", "16"});
    check(pngRun.exitStatus == 0, "flag 16 png: renders");
    Raster const png = readPNG("bitdepth_png.png");
    check(png.ok, "flag 16 png: file reads back");
    if (png.ok) {
      check(png.bits == 16, "flag 16 png: bit depth reads 16 (got " + std::to_string(png.bits) + ")");
      checkDithered("flag 16 png", png);
    }
  } else {
    std::printf("skipped flag 16 png: gman is built without libpng\n");
  }
#else
  std::printf("skipped flag 16 png: the test is built without libpng\n");
#endif

  GMANRunResult const tifRun = render(gman, ribDir, "bitdepth_tif", "tif", {"--bit-depth", "16"});
  check(tifRun.exitStatus == 0, "flag 16 tif: renders");
  Raster const tif = readTIFF("bitdepth_tif.tif");
  check(tif.ok, "flag 16 tif: file reads back");
  if (tif.ok) {
    check(tif.bits == 16, "flag 16 tif: 16 bits a sample (got " + std::to_string(tif.bits) + ")");
    checkDithered("flag 16 tif", tif);
  }
}

void checkExplicit16(std::string const& gman, std::string const& ribDir, Flags const& flags, std::string const& label) {
  GMANRunResult const pnmRun = render(gman, ribDir, "bitdepth_exact_pnm", "pnm", flags);
  check(pnmRun.exitStatus == 0, label + " pnm: renders");
  Raster const pnm = readPNM("bitdepth_exact_pnm.pnm");
  check(pnm.ok, label + " pnm: file reads back");
  if (pnm.ok) {
    check(pnm.maxval == 65535, label + " pnm: maxval reads 65535 (got " + std::to_string(pnm.maxval) + ")");
    checkExact(label + " pnm", pnm);
  }

#ifdef GMAN_WITH_PNG
  if (driverCompiled(gman, "png")) {
    GMANRunResult const pngRun = render(gman, ribDir, "bitdepth_exact_png", "png", flags);
    check(pngRun.exitStatus == 0, label + " png: renders");
    Raster const png = readPNG("bitdepth_exact_png.png");
    check(png.ok, label + " png: file reads back");
    if (png.ok) {
      check(png.bits == 16, label + " png: bit depth reads 16 (got " + std::to_string(png.bits) + ")");
      checkExact(label + " png", png);
    }
  }
#endif
}

void checkFlag8(std::string const& gman, std::string const& ribDir) {
  GMANRunResult const plain = render(gman, ribDir, "bitdepth_pnm", "pnm", {});
  check(plain.exitStatus == 0, "no flag pnm: renders");
  std::vector<unsigned char> const plainBytes = readBytes("bitdepth_pnm.pnm");
  Raster const plainPNM = readPNM("bitdepth_pnm.pnm");
  check(plainPNM.ok && plainPNM.maxval == 255, "no flag pnm: maxval reads 255");

  GMANRunResult const flagged = render(gman, ribDir, "bitdepth_pnm", "pnm", {"--bit-depth", "8"});
  check(flagged.exitStatus == 0, "flag 8 pnm: renders");
  std::vector<unsigned char> const flaggedBytes = readBytes("bitdepth_pnm.pnm");
  check(!flaggedBytes.empty() && flaggedBytes == plainBytes, "flag 8 pnm: byte-identical to no flag and no Quantize");

  GMANRunResult const fifteen = render(gman, ribDir, "bitdepth_15_pnm", "pnm", {"--bit-depth", "8"});
  check(fifteen.exitStatus == 0, "flag 8 with Quantize max 15: renders");
  Raster const small = readPNM("bitdepth_15_pnm.pnm");
  check(small.ok && small.maxval == 15, "flag 8 with Quantize max 15: maxval reads 15");
}

void checkRefusal(std::string const& gman, std::string const& ribDir, std::string const& label,
                  std::string const& fixture, std::string const& extension, std::string const& driver,
                  Flags const& flags, std::string const& code, std::string const& message) {
  if (!driverCompiled(gman, driver)) {
    std::printf("skipped %s: gman is built without %s\n", label.c_str(), driver.c_str());
    return;
  }
  std::string const file = fixture + "." + extension;
  std::remove(file.c_str());
  Flags arguments{"-d"};
  arguments.insert(arguments.end(), flags.begin(), flags.end());
  arguments.push_back(ribDir + "/" + fixture + ".rib");
  GMANRunOptions options;
  options.timeoutSeconds = 20;
  GMANRunResult const run = ::runGman(gman, arguments, options);
  check(!run.crashed && !run.timedOut, label + ": does not crash or hang");
  check(run.exitStatus == EXIT_FAILURE, label + ": exits 1");
  std::string const report = "ERROR: " + code + " -- " + message;
  check(countOccurrences(run.output, report) == 1, label + ": prints \"" + report + "\" exactly once");
  check(!fileExists(file), label + ": writes no " + file);
  check(run.output.find("Keyword token: Display") != std::string::npos, label + ": parses Display");
  check(run.output.find("Keyword token: Sphere") == std::string::npos, label + ": stops before the world block parses");
}

void checkConflicts(std::string const& gman, std::string const& ribDir) {
  checkRefusal(gman, ribDir, "flag 16 against max 255", "bitdepth_refuse_pnm_max255", "pnm", "pnm",
               {"--bit-depth", "16"}, "RIE_CONSISTENCY",
               "Quantize max 255 asks for 8-bit samples; bit depth 16 contradicts it");
  checkRefusal(gman, ribDir, "flag 8 against max 65535", "bitdepth_refuse_pnm_max65535", "pnm", "pnm",
               {"--bit-depth", "8"}, "RIE_CONSISTENCY",
               "Quantize max 65535 asks for 16-bit samples; bit depth 8 contradicts it");
  checkRefusal(gman, ribDir, "flag 16 against one 0", "bitdepth_refuse_tif_float", "tif", "tiff", {"--bit-depth", "16"},
               "RIE_CONSISTENCY", "Quantize one 0 asks for float samples; bit depth 16 contradicts it");
}

void checkJPEG(std::string const& gman, std::string const& ribDir) {
  checkRefusal(gman, ribDir, "flag 16 jpeg", "bitdepth_refuse_jpeg", "jpg", "jpeg", {"--bit-depth", "16"},
               "RIE_INCAPABLE", "JPEG writes 8-bit samples; bit depth 16 cannot be honoured");
  if (!driverCompiled(gman, "jpeg")) {
    return;
  }
  GMANRunResult const run = render(gman, ribDir, "bitdepth_refuse_jpeg", "jpg", {"--bit-depth", "8"});
  check(run.exitStatus == 0, "flag 8 jpeg: renders");
  check(fileExists("bitdepth_refuse_jpeg.jpg"), "flag 8 jpeg: writes the file");
}

void checkEXR(std::string const& gman, std::string const& ribDir) {
  if (!driverCompiled(gman, "exr")) {
    std::printf("skipped exr: gman is built without OpenEXR\n");
    return;
  }
  GMANRunResult const plain = render(gman, ribDir, "bitdepth_exr", "exr", {});
  check(plain.exitStatus == 0, "no flag exr: renders");
  std::vector<unsigned char> const plainBytes = readBytes("bitdepth_exr.exr");
  GMANRunResult const flagged = render(gman, ribDir, "bitdepth_exr", "exr", {"--bit-depth", "16"});
  check(flagged.exitStatus == 0, "flag 16 exr: renders");
  std::vector<unsigned char> const flaggedBytes = readBytes("bitdepth_exr.exr");
  check(!plainBytes.empty() && plainBytes == flaggedBytes, "flag 16 exr: byte-identical to no flag");

  GMANRunResult const floating = render(gman, ribDir, "bitdepth_exr_float", "exr", {"--bit-depth", "16"});
  check(floating.exitStatus == 0, "flag 16 exr with Quantize one 0: exits 0");
}

} // namespace

int main(int argc, char* argv[]) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s <gman-binary> <tests/rib-dir>\n", argv[0]);
    return 2;
  }
  std::string const gman = argv[1];
  std::string const ribDir = argv[2];

  checkFlag16(gman, ribDir);
  checkExplicit16(gman, ribDir, {}, "Quantize 65535");
  checkExplicit16(gman, ribDir, {"--bit-depth", "16"}, "Quantize 65535 and flag 16");
  checkFlag8(gman, ribDir);
  checkConflicts(gman, ribDir);
  checkJPEG(gman, ribDir);
  checkEXR(gman, ribDir);

  return checkSummary("bitdepth holds");
}
