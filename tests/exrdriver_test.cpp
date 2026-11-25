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

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <Imath/ImathBox.h>
#include <OpenEXR/ImfChannelList.h>
#include <OpenEXR/ImfCompression.h>
#include <OpenEXR/ImfFrameBuffer.h>
#include <OpenEXR/ImfHeader.h>
#include <OpenEXR/ImfInputFile.h>
#include <OpenEXR/ImfPixelType.h>
#include <OpenEXR/ImfThreading.h>

#include "check.h"
#include "gmancolor.h"
#include "gmanerror.h"
#include "gmanoutput.h"
#include "gmanoutputexr.h"
#include "gmanquantize.h"
#include "ri.h"
#include "rungman.h"

namespace {

constexpr int kWidth = 3;
constexpr int kHeight = 2;
constexpr float kBigSample = 1.0e5f;
constexpr float kPiSample = 3.1415927f;
constexpr char const* kRefusalMessage =
    "OpenEXR does not quantize; Quantize one 255 cannot be honoured; use Quantize \"rgba\" 0 0 0 0 or omit it";

GMANAlpha const kPartialAlpha(0.2f, 0.5f, 0.8f);
GMANQuantize const kFloatRequest{0, 0, 0, 0};

struct DecodedEXR {
  bool ok = false;
  std::set<std::string> channels;
  std::set<std::string> nonFloatChannels;
  int minX = 0, minY = 0, maxX = 0, maxY = 0;
  Imf::Compression compression = Imf::NO_COMPRESSION;
  int width = 0, height = 0;
  std::map<std::string, std::vector<float>> data;

  float at(std::string const& channel, int x, int y) const {
    return data.at(channel)[static_cast<std::size_t>(y * width + x)];
  }
};

DecodedEXR readEXR(std::string const& path) {
  DecodedEXR image;
  try {
    Imf::InputFile file(path.c_str());
    Imath::Box2i const window = file.header().dataWindow();
    image.minX = window.min.x;
    image.minY = window.min.y;
    image.maxX = window.max.x;
    image.maxY = window.max.y;
    image.width = window.max.x - window.min.x + 1;
    image.height = window.max.y - window.min.y + 1;
    image.compression = file.header().compression();

    Imf::FrameBuffer frameBuffer;
    Imf::ChannelList const& list = file.header().channels();
    for (auto it = list.begin(); it != list.end(); ++it) {
      std::string const name = it.name();
      image.channels.insert(name);
      if (it.channel().type != Imf::FLOAT) {
        image.nonFloatChannels.insert(name);
      }
      image.data[name].assign(static_cast<std::size_t>(image.width * image.height), -1.0f);
      char* const base =
          reinterpret_cast<char*>(image.data[name].data()) -
          sizeof(float) * (static_cast<std::size_t>(window.min.x) +
                           static_cast<std::size_t>(window.min.y) * static_cast<std::size_t>(image.width));
      frameBuffer.insert(
          name, Imf::Slice(Imf::FLOAT, base, sizeof(float), sizeof(float) * static_cast<std::size_t>(image.width)));
    }
    file.setFrameBuffer(frameBuffer);
    file.readPixels(window.min.y, window.max.y);
    image.ok = true;
  } catch (std::exception const&) {
    image.ok = false;
  }
  return image;
}

bool sameBits(float a, float b) { return std::memcmp(&a, &b, sizeof(float)) == 0; }

bool near(float a, float b) { return std::fabs(a - b) <= 1.0e-6f * std::fmax(1.0f, std::fabs(b)); }

float redAt(int x, int y) { return 0.5f + static_cast<float>(x) + 4.0f * static_cast<float>(y); }

float greenAt(int x, int y) { return 0.25f * static_cast<float>(1 + 2 * x + 8 * y); }

float blueAt(int x, int y) { return 0.0625f * static_cast<float>(1 + x + 3 * y); }

float alphaAt(int x, int y) {
  if (x == 0 && y == 0) {
    return 0.5f;
  }
  return 0.9375f - 0.125f * static_cast<float>(x + 3 * y);
}

float expectedRed(int x, int y) { return x == 1 && y == 1 ? kBigSample : redAt(x, y); }

float expectedGreen(int x, int y) { return x == 2 && y == 0 ? kPiSample : greenAt(x, y); }

void fill(gman::OutputEXR& output) {
  for (int y = 0; y < kHeight; ++y) {
    for (int x = 0; x < kWidth; ++x) {
      output.setPixel(x, y, GMANColor(expectedRed(x, y), expectedGreen(x, y), blueAt(x, y)));
      float const a = alphaAt(x, y);
      output.setAlpha(x, y, x == 0 && y == 0 ? kPartialAlpha : GMANAlpha(a, a, a));
    }
  }
}

std::string pointName(char const* what, std::string const& channel, int x, int y) {
  return std::string(what) + ": " + channel + " at (" + std::to_string(x) + ", " + std::to_string(y) + ")";
}

void saveFloat(std::string const& label, GMANOutput& output, GMANOutput::DisplayMode mode, RtFloat gain = 1.0f,
               RtFloat gamma = 1.0f) {
  try {
    output.save(mode, gain, gamma, kFloatRequest);
  } catch (GMANError const& error) {
    check(false, label + ": save throws \"" + error.getMessage() + "\"");
  }
}

void checkChannelSet(char const* label, GMANOutput::DisplayMode mode, std::set<std::string> const& expected) {
  std::string const path = std::string("channels_") + label + ".exr";
  gman::OutputEXR output(path.c_str(), kWidth, kHeight);
  fill(output);
  saveFloat(label, output, mode);

  DecodedEXR const image = readEXR(path);
  check(image.ok, std::string(label) + ": file reads back");
  if (!image.ok) {
    return;
  }
  check(image.channels == expected, std::string(label) + ": holds exactly the expected channels");
  check(image.nonFloatChannels.empty(), std::string(label) + ": every channel is 32-bit float");
}

void checkValues() {
  char const* const path = "values.exr";
  gman::OutputEXR output(path, kWidth, kHeight);
  fill(output);
  saveFloat("values", output, GMANOutput::RGBA);

  DecodedEXR const image = readEXR(path);
  check(image.ok, "values: file reads back");
  if (!image.ok) {
    return;
  }
  check(image.minX == 0 && image.minY == 0 && image.maxX == kWidth - 1 && image.maxY == kHeight - 1,
        "values: the data window spans (0, 0) to (2, 1)");
  check(image.compression == Imf::ZIP_COMPRESSION, "values: ZIP compression");
  for (int y = 0; y < kHeight; ++y) {
    for (int x = 0; x < kWidth; ++x) {
      check(sameBits(image.at("R", x, y), expectedRed(x, y)), pointName("values", "R", x, y) + " is bit-exact");
      check(sameBits(image.at("G", x, y), expectedGreen(x, y)), pointName("values", "G", x, y) + " is bit-exact");
      check(sameBits(image.at("B", x, y), blueAt(x, y)), pointName("values", "B", x, y) + " is bit-exact");
      check(sameBits(image.at("A", x, y), alphaAt(x, y)), pointName("values", "A", x, y) + " is bit-exact");
    }
  }
  check(sameBits(image.at("R", 1, 1), kBigSample), "values: 1.0e5 survives, which half cannot hold");
  check(sameBits(image.at("G", 2, 0), kPiSample), "values: 3.1415927 survives, which half cannot hold");
  check(image.at("A", 0, 0) == 0.5f, "values: pixel (0, 0) carries the partial coverage");
}

void checkChannelSets() {
  checkChannelSet("rgb", GMANOutput::RGB, {"R", "G", "B"});
  checkChannelSet("rgba", GMANOutput::RGBA, {"R", "G", "B", "A"});
  checkChannelSet("a", GMANOutput::A, {"A"});
  checkChannelSet("rgbaz", GMANOutput::RGBAZ, {"R", "G", "B", "A"});
  checkChannelSet("az", GMANOutput::AZ, {"R", "G", "B", "A"});
  checkChannelSet("z", GMANOutput::Z, {"R", "G", "B", "A"});
}

void checkAlphaOnlyValues() {
  char const* const path = "alphaonly.exr";
  gman::OutputEXR output(path, kWidth, kHeight);
  fill(output);
  saveFloat("alphaonly", output, GMANOutput::A);

  DecodedEXR const image = readEXR(path);
  check(image.ok, "alphaonly: file reads back");
  if (!image.ok) {
    return;
  }
  for (int y = 0; y < kHeight; ++y) {
    for (int x = 0; x < kWidth; ++x) {
      check(sameBits(image.at("A", x, y), alphaAt(x, y)), pointName("alphaonly", "A", x, y) + " is bit-exact");
    }
  }
}

void checkGainAndGamma() {
  char const* const gainPath = "gain.exr";
  gman::OutputEXR gained(gainPath, kWidth, kHeight);
  fill(gained);
  saveFloat("gain", gained, GMANOutput::RGBA, 2.0f, 1.0f);
  DecodedEXR const gainImage = readEXR(gainPath);
  check(gainImage.ok, "gain: file reads back");
  if (gainImage.ok) {
    check(sameBits(gainImage.at("R", 1, 0), 2.0f * redAt(1, 0)), "gain: gain 2 at gamma 1 doubles R");
    check(sameBits(gainImage.at("B", 2, 1), 2.0f * blueAt(2, 1)), "gain: gain 2 at gamma 1 doubles B");
    check(sameBits(gainImage.at("A", 1, 0), alphaAt(1, 0)), "gain: alpha skips gain");
  }

  char const* const gammaPath = "gamma.exr";
  gman::OutputEXR corrected(gammaPath, kWidth, kHeight);
  fill(corrected);
  float const gain = 1.5f;
  float const gamma = 2.0f;
  saveFloat("gamma", corrected, GMANOutput::RGBA, gain, gamma);
  DecodedEXR const gammaImage = readEXR(gammaPath);
  check(gammaImage.ok, "gamma: file reads back");
  if (gammaImage.ok) {
    for (int x = 0; x < kWidth; ++x) {
      float const expected = std::pow(gain * blueAt(x, 1), 1.0f / gamma);
      check(near(gammaImage.at("B", x, 1), expected),
            pointName("gamma", "B", x, 1) + " reads pow(gain * v, 1 / gamma)");
    }
  }
}

void checkNaNReadsZero() {
  float const nan = std::numeric_limits<float>::quiet_NaN();
  char const* const path = "nan.exr";
  gman::OutputEXR output(path, kWidth, kHeight);
  fill(output);
  output.setPixel(1, 0, GMANColor(nan, greenAt(1, 0), blueAt(1, 0)));
  output.setAlpha(2, 1, GMANAlpha(nan, nan, nan));
  saveFloat("nan", output, GMANOutput::RGBA);

  DecodedEXR const image = readEXR(path);
  check(image.ok, "nan: file reads back");
  if (!image.ok) {
    return;
  }
  check(image.at("R", 1, 0) == 0.0f, "nan: a NaN R reads 0");
  check(sameBits(image.at("G", 1, 0), greenAt(1, 0)), "nan: its neighbouring G is untouched");
  check(image.at("A", 2, 1) == 0.0f, "nan: a NaN alpha reads 0");
}

void checkInfinityPassesThrough() {
  float const infinity = std::numeric_limits<float>::infinity();
  char const* const path = "inf.exr";
  gman::OutputEXR output(path, kWidth, kHeight);
  fill(output);
  output.setPixel(1, 0, GMANColor(infinity, -infinity, blueAt(1, 0)));
  saveFloat("inf", output, GMANOutput::RGBA);

  DecodedEXR const image = readEXR(path);
  check(image.ok, "inf: file reads back");
  if (!image.ok) {
    return;
  }
  check(image.at("R", 1, 0) == infinity, "inf: +infinity reads +infinity");
  check(image.at("G", 1, 0) == -infinity, "inf: -infinity reads -infinity");
}

void checkNoThreads() {
  Imf::setGlobalThreadCount(2);
  gman::OutputEXR output("threads.exr", kWidth, kHeight);
  fill(output);
  saveFloat("threads", output, GMANOutput::RGBA);
  check(Imf::globalThreadCount() == 0, "threads: a save leaves OpenEXR's pool at zero threads");
}

void checkUnwritablePath() {
  gman::OutputEXR output("no-such-directory/unwritable.exr", kWidth, kHeight);
  fill(output);
  bool threwGMANError = false;
  bool threwOther = false;
  RtInt code = 0;
  RtInt severity = 0;
  try {
    output.save(GMANOutput::RGBA, 1.0f, 1.0f, kFloatRequest);
  } catch (GMANError const& error) {
    threwGMANError = true;
    code = error.getCode();
    severity = error.getSeverity();
  } catch (...) {
    threwOther = true;
  }
  check(threwGMANError && !threwOther, "unwritable: save throws GMANError and nothing else");
  check(code == RIE_SYSTEM && severity == RIE_SEVERE, "unwritable: RIE_SYSTEM at RIE_SEVERE");
}

bool fileExists(std::string const& path) {
  std::FILE* f = std::fopen(path.c_str(), "rb");
  if (f == nullptr) {
    return false;
  }
  std::fclose(f);
  return true;
}

void checkQuantizedRequestRefused() {
  char const* const path = "refused.exr";
  std::remove(path);
  gman::OutputEXR output(path, kWidth, kHeight);
  fill(output);
  bool threw = false;
  RtInt code = 0;
  RtInt severity = 0;
  std::string message;
  try {
    output.save(GMANOutput::RGBA, 1.0f, 1.0f, GMANQuantize{255, 0, 255, 0.5f});
  } catch (GMANError const& error) {
    threw = true;
    code = error.getCode();
    severity = error.getSeverity();
    message = error.getMessage();
  }
  check(threw, "refused: a nonzero Quantize one throws");
  check(code == RIE_INCAPABLE && severity == RIE_ERROR, "refused: RIE_INCAPABLE at RIE_ERROR");
  check(message == kRefusalMessage, "refused: the message names OpenEXR and the request (got \"" + message + "\")");
  check(!fileExists(path), "refused: writes no file");
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

void checkCoveredHalfPlane(std::string const& label, std::string const& path) {
  DecodedEXR const image = readEXR(path);
  check(image.ok, label + ": file reads back");
  if (!image.ok) {
    return;
  }
  check(image.channels == std::set<std::string>({"R", "G", "B", "A"}), label + ": holds R, G, B and A");
  check(image.nonFloatChannels.empty(), label + ": every channel is 32-bit float");
  for (int x = 0; x < 8; ++x) {
    check(image.at("R", x, 8) == 3.0f && image.at("G", x, 8) == 3.0f && image.at("B", x, 8) == 3.0f,
          label + ": covered column " + std::to_string(x) + " reads R, G, B 3.0");
    check(image.at("A", x, 8) == 1.0f, label + ": covered column " + std::to_string(x) + " reads alpha 1.0");
  }
}

void checkUncoveredHalfPlane(std::string const& label, std::string const& path) {
  DecodedEXR const image = readEXR(path);
  if (!image.ok) {
    return;
  }
  for (int x = 8; x < 16; ++x) {
    check(image.at("R", x, 8) == 0.0f && image.at("G", x, 8) == 0.0f && image.at("B", x, 8) == 0.0f &&
              image.at("A", x, 8) == 0.0f,
          label + ": uncovered column " + std::to_string(x) + " reads 0 on all four");
  }
}

void checkRenders(std::string const& gman, std::string const& ribDir, std::string const& name,
                  bool uncoveredReadsZero) {
  std::string const file = name + ".exr";
  std::remove(file.c_str());
  GMANRunResult const r = runGman(gman, {"-w", ribDir + "/" + name + ".rib"});
  check(r.exitStatus == 0, name + ": renders");
  checkCoveredHalfPlane(name, file);
  if (uncoveredReadsZero) {
    checkUncoveredHalfPlane(name, file);
  }
}

void checkRefusedRender(std::string const& gman, std::string const& ribDir) {
  std::string const label = "exrquantized";
  std::string const file = label + ".exr";
  std::remove(file.c_str());

  GMANRunOptions options;
  options.timeoutSeconds = 20;
  GMANRunResult const r = runGman(gman, {"-d", ribDir + "/" + label + ".rib"}, options);
  check(!r.crashed && !r.timedOut, label + ": does not crash or hang");
  check(r.exitStatus == EXIT_FAILURE, label + ": exits 1");
  std::string const report = std::string("ERROR: RIE_INCAPABLE -- ") + kRefusalMessage;
  check(countOccurrences(r.output, report) == 1, label + ": prints \"" + report + "\" exactly once");
  check(!fileExists(file), label + ": writes no " + file);
  check(r.output.find("Keyword token: Quantize") != std::string::npos, label + ": parses Quantize");
  check(r.output.find("Keyword token: Sphere") == std::string::npos, label + ": stops before the world block parses");
}

} // namespace

int main(int argc, char* argv[]) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s <gman-binary> <tests/rib-dir>\n", argv[0]);
    return 2;
  }
  std::string const gman = argv[1];
  std::string const ribDir = argv[2];

  checkValues();
  checkChannelSets();
  checkAlphaOnlyValues();
  checkGainAndGamma();
  checkNaNReadsZero();
  checkInfinityPassesThrough();
  checkNoThreads();
  checkUnwritablePath();
  checkQuantizedRequestRefused();

  checkRenders(gman, ribDir, "exrfloat", true);
  checkRenders(gman, ribDir, "exrdither", true);
  checkRenders(gman, ribDir, "exrimager", false);
  checkRefusedRender(gman, ribDir);

  return checkSummary("exrdriver holds");
}
