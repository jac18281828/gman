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
 * The ray tracer lights a matte wall from an area emitter as the path tracer
 * does. Each tests/rib/rayarealight_*.rib fixture is direct-only by
 * construction: a black emitter and blocker, one receiving plane and a black
 * background, so the path tracer reaches light only by next-event estimation
 * or a BSDF draw from the wall. Each renders under both renderers and the
 * ray tracer's mean over a lit block and over the blocker's penumbra agrees
 * with the path tracer's within a tolerance of four standard errors of the
 * two renderers' noise plus a bias budget: 1.5% on the lit block, 4% on the
 * penumbra. The glass fixture, which puts a clear slab in front of the
 * emitter, checks the lit block alone, and its lit block over the panel
 * fixture's reads the two faces' transmittance under each renderer. Copies
 * of the sphere fixture prove that more draws per shading point sit closer to
 * the path tracer, that Option "raytracer" "integer arealightsamples" changes
 * the draws only through its value and that a value below 1 or one that is not
 * an integer warns once and reads as the default of 4, and that an emitter
 * gman::emitters skips warns once and never says an area light has no effect.
 */

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <tiffio.h>

#include "check.h"
#include "goldenimage.h"
#include "rungman.h"

namespace {

struct Block {
  uint32_t x;
  uint32_t y;
  uint32_t width;
  uint32_t height;
};

constexpr Block kLit = {24, 4, 16, 8};
constexpr Block kPenumbra = {26, 16, 12, 12};

constexpr double kLitTolerance = 0.015;
constexpr double kPenumbraTolerance = 0.04;
constexpr double kLitFloor = 64.0;
constexpr double kPenumbraFloor = 16.0;
constexpr double kPenumbraShare = 0.5;
constexpr double kGlassRatioLow = 0.90;
constexpr double kGlassRatioHigh = 0.94;
constexpr double kDrawsErrorShare = 0.5;

constexpr std::size_t kChannels = 3;
constexpr std::size_t kExpectedWarningCount = 1;

char const* const kRayTracer = "gmanraytracer";
char const* const kPathTracer = "gmanpathtracer";

constexpr char const* kSphereFixture = "rayarealight_sphere";
constexpr char const* kDisplayLine = "Display \"rayarealight_sphere.tif\" \"file\" \"rgba\"";
constexpr char const* kFormatLine = "Format 64 48 1";
constexpr char const* kPixelSamplesLine = "PixelSamples 2 2";
constexpr char const* kDrawsLine = "Option \"raytracer\" \"integer arealightsamples\" [64]";
constexpr char const* kEmitterLine = "Sphere 0.5 -0.5 0.5 360";

constexpr char const* kDefaultWarning = "must be an integer of at least 1; using 4.";
constexpr char const* kSkippedWarning = "ineligible or unsupported";
constexpr char const* kIgnoredWarning = "has no effect here";

using Mean = std::array<double, kChannels>;

struct Fixture {
  char const* name;
  bool penumbra;
};

struct Reading {
  GmanImage image;
  Mean lit{};
  Mean penumbra{};
};

struct Pair {
  Reading ray;
  Reading path;
};

std::string readFile(std::string const& path) {
  std::ifstream in(path);
  std::ostringstream contents;
  contents << in.rdbuf();
  return contents.str();
}

std::size_t countOccurrences(std::string const& haystack, std::string const& needle) {
  std::size_t count = 0;
  std::size_t pos = 0;
  while ((pos = haystack.find(needle, pos)) != std::string::npos) {
    ++count;
    pos += needle.size();
  }
  return count;
}

double channelOf(uint32_t pixel, std::size_t channel) {
  if (channel == 0) {
    return TIFFGetR(pixel);
  }
  if (channel == 1) {
    return TIFFGetG(pixel);
  }
  return TIFFGetB(pixel);
}

Mean blockMean(GmanImage const& image, Block const& block) {
  Mean mean{};
  for (uint32_t y = block.y; y < block.y + block.height; ++y) {
    for (uint32_t x = block.x; x < block.x + block.width; ++x) {
      for (std::size_t channel = 0; channel < kChannels; ++channel) {
        mean[channel] += channelOf(image.at(x, y), channel);
      }
    }
  }
  double const count = static_cast<double>(block.width) * static_cast<double>(block.height);
  for (double& value : mean) {
    value /= count;
  }
  return mean;
}

std::string describe(Mean const& mean) {
  return "(" + std::to_string(mean[0]) + ", " + std::to_string(mean[1]) + ", " + std::to_string(mean[2]) + ")";
}

bool sameImage(GmanImage const& a, GmanImage const& b) {
  return a.ok && b.ok && a.width == b.width && a.height == b.height && a.raster == b.raster;
}

struct Render {
  GmanImage image;
  std::string output;
};

Render renderTo(std::string const& gman, std::string const& renderer, std::string const& rib, std::string const& tif) {
  std::remove(tif.c_str());
  GMANRunResult const run = runGman(gman, {"-r", renderer, rib});
  check(run.exitStatus == 0, renderer + " renders " + rib + " (exit " + std::to_string(run.exitStatus) + ")");
  Render result;
  result.output = run.output;
  result.image = readGmanTIFF(tif);
  check(result.image.ok, renderer + ": " + tif + " reads back");
  return result;
}

Reading readingOf(GmanImage image) {
  Reading reading;
  reading.lit = blockMean(image, kLit);
  reading.penumbra = blockMean(image, kPenumbra);
  reading.image = std::move(image);
  return reading;
}

bool renderPair(std::string const& gman, std::string const& ribDir, std::string const& name, Pair& pair) {
  std::string const rib = ribDir + "/" + name + ".rib";
  std::string const tif = name + ".tif";
  Render const ray = renderTo(gman, kRayTracer, rib, tif);
  Render const path = renderTo(gman, kPathTracer, rib, tif);
  if (!ray.image.ok || !path.image.ok) {
    return false;
  }
  pair.ray = readingOf(ray.image);
  pair.path = readingOf(path.image);
  return true;
}

void checkSetup(Pair const& pair, std::string const& name) {
  for (std::size_t channel = 0; channel < kChannels; ++channel) {
    std::string const label = name + " channel " + std::to_string(channel) + " setup: ";
    check(pair.path.lit[channel] > kLitFloor,
          label + "the path tracer's lit block exceeds 64 levels, " + describe(pair.path.lit));
    check(pair.path.penumbra[channel] > kPenumbraFloor &&
              pair.path.penumbra[channel] < kPenumbraShare * pair.path.lit[channel],
          label + "the path tracer's penumbra block exceeds 16 levels and lies below half the lit block, " +
              describe(pair.path.penumbra));
  }
}

void checkAgreement(Pair const& pair, Fixture const& fixture) {
  for (std::size_t channel = 0; channel < kChannels; ++channel) {
    std::string const label = std::string(fixture.name) + " channel " + std::to_string(channel) + ": ";
    double const lit = pair.ray.lit[channel] / pair.path.lit[channel] - 1.0;
    std::printf("%slit: ray %.4f, path %.4f, ratio - 1 %+.4f, tolerance %.3f\n", label.c_str(), pair.ray.lit[channel],
                pair.path.lit[channel], lit, kLitTolerance);
    check(std::fabs(lit) <= kLitTolerance, label + "the ray tracer's lit block is within " +
                                               std::to_string(kLitTolerance * 100.0) + "% of the path tracer's (" +
                                               std::to_string(lit * 100.0) + "%)");
    if (!fixture.penumbra) {
      continue;
    }
    double const penumbra = pair.ray.penumbra[channel] / pair.path.penumbra[channel] - 1.0;
    std::printf("%spenumbra: ray %.4f, path %.4f, ratio - 1 %+.4f, tolerance %.3f\n", label.c_str(),
                pair.ray.penumbra[channel], pair.path.penumbra[channel], penumbra, kPenumbraTolerance);
    check(std::fabs(penumbra) <= kPenumbraTolerance,
          label + "the ray tracer's penumbra block is within " + std::to_string(kPenumbraTolerance * 100.0) +
              "% of the path tracer's (" + std::to_string(penumbra * 100.0) + "%)");
  }
}

void checkGlassTransmittance(Pair const& glass, Pair const& panel) {
  for (std::size_t channel = 0; channel < kChannels; ++channel) {
    std::string const label = "glass over panel, channel " + std::to_string(channel) + ": ";
    double const ray = glass.ray.lit[channel] / panel.ray.lit[channel];
    double const path = glass.path.lit[channel] / panel.path.lit[channel];
    std::printf("%s ray tracer %.4f, path tracer %.4f\n", label.c_str(), ray, path);
    check(ray >= kGlassRatioLow && ray <= kGlassRatioHigh,
          label + "the ray tracer's lit block is " + std::to_string(ray) + " of the panel's, in [0.90, 0.94]");
    check(path >= kGlassRatioLow && path <= kGlassRatioHigh,
          label + "the path tracer's lit block is " + std::to_string(path) + " of the panel's, in [0.90, 0.94]");
  }
}

std::string fixtureText(std::string const& ribDir, std::string const& name) {
  return readFile(ribDir + "/" + name + ".rib");
}

// text with each line that equals a key of edits replaced by that key's value,
// the whole line dropped when the value is empty. A key that equals no line of
// text fails a check, so a variant never runs unedited.
std::string withLines(std::string const& text, std::map<std::string, std::string> const& edits,
                      std::string const& label) {
  std::map<std::string, bool> found;
  std::istringstream in(text);
  std::ostringstream out;
  std::string line;
  while (std::getline(in, line)) {
    auto const edit = edits.find(line);
    if (edit == edits.end()) {
      out << line << "\n";
      continue;
    }
    found[line] = true;
    if (!edit->second.empty()) {
      out << edit->second << "\n";
    }
  }
  for (auto const& edit : edits) {
    check(found[edit.first], label + ": the fixture holds the line " + edit.first);
  }
  return out.str();
}

void writeFile(std::string const& path, std::string const& text) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out << text;
}

std::string displayLine(std::string const& name) { return "Display \"" + name + ".tif\" \"file\" \"rgba\""; }

std::string drawsLine(std::string const& typeAndName, std::string const& value) {
  return "Option \"raytracer\" \"" + typeAndName + "\" [" + value + "]";
}

// The root-mean-square of ray's reading minus path's over the penumbra block,
// every channel pooled.
double penumbraRms(GmanImage const& ray, GmanImage const& path) {
  double sum = 0.0;
  for (uint32_t y = kPenumbra.y; y < kPenumbra.y + kPenumbra.height; ++y) {
    for (uint32_t x = kPenumbra.x; x < kPenumbra.x + kPenumbra.width; ++x) {
      for (std::size_t channel = 0; channel < kChannels; ++channel) {
        double const difference = channelOf(ray.at(x, y), channel) - channelOf(path.at(x, y), channel);
        sum += difference * difference;
      }
    }
  }
  return std::sqrt(sum / (static_cast<double>(kPenumbra.width) * kPenumbra.height * kChannels));
}

void checkMoreDrawsSitCloser(std::string const& gman, std::string const& ribDir, Pair const& sphere) {
  std::string const name = "rayarealight_draws";
  std::string const text =
      withLines(fixtureText(ribDir, kSphereFixture),
                {{kDisplayLine, displayLine(name)}, {kDrawsLine, drawsLine("integer arealightsamples", "4")}}, "draws");
  writeFile(name + ".rib", text);
  Render const few = renderTo(gman, kRayTracer, name + ".rib", name + ".tif");
  if (!few.image.ok) {
    return;
  }
  double const fewRms = penumbraRms(few.image, sphere.path.image);
  double const manyRms = penumbraRms(sphere.ray.image, sphere.path.image);
  std::printf("penumbra rms against the path tracer: 4 draws %.4f, 64 draws %.4f, ratio %.4f\n", fewRms, manyRms,
              manyRms / fewRms);
  check(manyRms <= kDrawsErrorShare * fewRms,
        "the ray tracer at 64 draws is at most half as far from the path tracer, in rms over the penumbra, as at 4 "
        "draws (" +
            std::to_string(manyRms) + " against " + std::to_string(fewRms) + ")");
}

struct CountVariant {
  char const* label;
  std::string drawsOption;
  bool warns;
};

Render renderCountVariant(std::string const& gman, std::string const& ribDir, CountVariant const& variant,
                          std::string const& name) {
  std::string const text = withLines(fixtureText(ribDir, kSphereFixture),
                                     {{kDisplayLine, displayLine(name)},
                                      {kFormatLine, "Format 16 12 1"},
                                      {kPixelSamplesLine, "PixelSamples 1 1"},
                                      {kDrawsLine, variant.drawsOption}},
                                     variant.label);
  writeFile(name + ".rib", text);
  return renderTo(gman, kRayTracer, name + ".rib", name + ".tif");
}

void checkDrawCountOption(std::string const& gman, std::string const& ribDir) {
  std::string const integer = "integer arealightsamples";
  std::vector<CountVariant> const variants = {
      {"absent", "", false},
      {"four", drawsLine(integer, "4"), false},
      {"sixteen", drawsLine(integer, "16"), false},
      {"zero", drawsLine(integer, "0"), true},
      {"negative", drawsLine(integer, "-3"), true},
      {"float", drawsLine("float arealightsamples", "8"), true},
  };
  std::map<std::string, Render> renders;
  for (CountVariant const& variant : variants) {
    Render render = renderCountVariant(gman, ribDir, variant, std::string("rayarealight_count_") + variant.label);
    check(countOccurrences(render.output, kDefaultWarning) == (variant.warns ? kExpectedWarningCount : 0),
          std::string("draw count ") + variant.label + ": the default-count warning is logged " +
              (variant.warns ? "once" : "never"));
    renders[variant.label] = std::move(render);
  }

  GmanImage const& four = renders["four"].image;
  check(sameImage(renders["absent"].image, four), "draw count: an absent option renders as 4 draws do");
  check(four.ok && renders["sixteen"].image.ok && !sameImage(renders["sixteen"].image, four),
        "draw count: 16 draws render differently from 4");
  for (char const* label : {"zero", "negative", "float"}) {
    check(sameImage(renders[label].image, four),
          std::string("draw count ") + label + ": an invalid option renders as 4 draws do");
  }
}

void checkSkippedEmitter(std::string const& gman, std::string const& ribDir) {
  std::string const name = "rayarealight_cone";
  std::string const text = withLines(fixtureText(ribDir, kSphereFixture),
                                     {{kDisplayLine, displayLine(name)}, {kEmitterLine, "Cone 1 0.5 360"}}, "cone");
  writeFile(name + ".rib", text);
  Render const render = renderTo(gman, kRayTracer, name + ".rib", name + ".tif");
  check(countOccurrences(render.output, kSkippedWarning) == kExpectedWarningCount,
        "cone emitter: the ray tracer logs that the emitter is ineligible or unsupported exactly once");
  check(countOccurrences(render.output, kIgnoredWarning) == 0,
        "cone emitter: the ray tracer never says an area light has no effect");
}

} // namespace

int main(int argc, char* argv[]) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s <gman-binary> <tests/rib dir>\n", argv[0]);
    return 2;
  }
  std::string const gman = argv[1];
  std::string const ribDir = argv[2];

  std::map<std::string, Pair> pairs;
  for (Fixture const& fixture : {Fixture{"rayarealight_sphere", true}, Fixture{"rayarealight_disk", true},
                                 Fixture{"rayarealight_panel", true}, Fixture{"rayarealight_glass", false}}) {
    Pair& pair = pairs[fixture.name];
    if (!renderPair(gman, ribDir, fixture.name, pair)) {
      continue;
    }
    checkSetup(pair, fixture.name);
    checkAgreement(pair, fixture);
  }

  if (pairs["rayarealight_glass"].ray.image.ok && pairs["rayarealight_panel"].ray.image.ok) {
    checkGlassTransmittance(pairs["rayarealight_glass"], pairs["rayarealight_panel"]);
  }
  if (pairs[kSphereFixture].ray.image.ok && pairs[kSphereFixture].path.image.ok) {
    checkMoreDrawsSitCloser(gman, ribDir, pairs[kSphereFixture]);
  }
  checkDrawCountOption(gman, ribDir);
  checkSkippedEmitter(gman, ribDir);

  return checkSummary("the ray tracer's area-lit blocks agree with the path tracer's, its draw count and its "
                      "warnings follow their options");
}
