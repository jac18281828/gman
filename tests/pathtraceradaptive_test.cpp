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
 * The path tracer stops a slot once its paths' standard error falls below
 * PixelVariance. Rendered in process at one slot per pixel, so a pixel
 * reads its slot bit-equal, over three kinds of slot: background-only
 * slots, flat slots whose every path returns the same radiance, and noisy
 * slots under an emitting sphere. Off is today: a PixelVariance of 0, a
 * negative one and NaN trace every slot to the maximum. Flat and
 * background slots stop at the first scheduled check, and the slot's mean
 * divides by the paths it traced. A noisy slot that cannot meet the
 * threshold traces the maximum and reads what a PixelVariance of 0 reads.
 * The checks fall at every minsamples-th path, by default the root of the
 * maximum; a bad minsamples warns once and takes the default. One info
 * line reports the paths traced against the paths allowed. The metric is
 * called directly on hand-computed paths.
 */

#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <format>
#include <fstream>
#include <limits>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "check.h"
#include "gmanattributes.h"
#include "gmancolor.h"
#include "gmanframebuffer.h"
#include "gmanlightsourcemgr.h"
#include "gmanlog.h"
#include "gmanmath.h"
#include "gmanmatrix4.h"
#include "gmanoptions.h"
#include "gmanparameterlist.h"
#include "gmanpathtracerenderer.h"
#include "gmanpathtracerweights.h"
#include "gmanpoint.h"
#include "gmanraydisk.h"
#include "gmanraypolygon.h"
#include "gmanshading.h"
#include "gmantransform.h"
#include "gmanvector.h"
#include "gmanvsperspective.h"
#include "maketransform.h"
#include "pathtracerarealightscene.h"
#include "pathtracerscene.h"
#include "ri.h"

namespace {

constexpr RtInt kWidth = 24;
constexpr RtInt kHeight = 16;
constexpr RtInt kMaximum = 16;
constexpr RtInt kShortMaximum = 10;
constexpr RtInt kScheduleEvery = 3;
constexpr RtInt kExplicitEvery = 5;
constexpr RtInt kBelowTwo = 1;
constexpr RtInt kBeyondMaximum = 20;
constexpr std::uint32_t kDefaultInterval = 4;
constexpr std::uint32_t kFlatProbeCounts[] = {1, 2, 4};

constexpr RtFloat kPositiveVariance = 0.01f;
constexpr RtFloat kUnmeetableVariance = 1.0e-9f;
constexpr RtFloat kScheduleVariance = 0.02f;
constexpr double kMeanTolerance = 1.0e-6;

constexpr RtFloat kFlatReflectance = 0.5f;
constexpr RtFloat kDistantIntensity = 1.0f;
constexpr RtFloat kDistantDirectionX = 0.3f;
constexpr RtFloat kDistantDirectionY = -1.0f;
constexpr RtFloat kDistantDirectionZ = 0.2f;
constexpr RtFloat kDiskRadius = 2.0f;
constexpr RtFloat kDiskCentreX = 10.0f;
constexpr RtFloat kDiskCentreY = -0.5f;
constexpr RtFloat kDiskCentreZ = 6.0f;
constexpr RtFloat kDiskTiltDegrees = -90.0f;
constexpr RtFloat kSceneNear = 0.5f;
constexpr RtFloat kSceneFar = 50.0f;
constexpr RtFloat kViewFov = 90.0f;
constexpr RtFloat kFilterWidth = 1.0f;

constexpr double kJustAboveSqrtOneThird = 0.5774;
constexpr double kJustBelowSqrtOneThird = 0.5773;
constexpr double kHalfError = 0.5;
constexpr double kJustAboveHalfError = 0.5001;
constexpr double kJustBelowHalfError = 0.49;
constexpr double kHugeThreshold = 1.0e30;
constexpr double kTinyThreshold = 1.0e-30;

char const* const kRenderLog = "pathtraceradaptive_render.log";
char const* const kInfoPrefix = "gmanpathtracer: traced ";

std::string readFile(std::string const& path) {
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

void beginLogCapture(std::string const& path) {
  std::remove(path.c_str());
  setLogFile(path.c_str());
  setScreenOutput(false);
}

std::string endLogCapture(std::string const& path) {
  setLogFile("/dev/null");
  setScreenOutput(true);
  return readFile(path);
}

bool colorsEqual(GMANColor const& a, GMANColor const& b) {
  return a.getRed() == b.getRed() && a.getGreen() == b.getGreen() && a.getBlue() == b.getBlue();
}

bool colorsClose(GMANColor const& a, GMANColor const& b, double relativeTolerance) {
  for (int c = 0; c < 3; ++c) {
    double const scale = std::fabs(channel(b, c));
    if (std::fabs(channel(a, c) - channel(b, c)) > relativeTolerance * scale) {
      return false;
    }
  }
  return true;
}

struct Frame {
  std::vector<std::uint32_t> counts;
  std::vector<GMANColor> pixels;
  std::string log;
};

using Minsamples = std::optional<GMANOptions::NamedOptionValue>;

GMANColor const kBlack(0.0f, 0.0f, 0.0f);

enum class Lighting { distant, emittingDisk };

// A floor under black, lit either by one distant light, so every path that
// lands on it returns the same radiance, or by an emitting disk off to its
// side and above, facing down, so they differ. Owns everything the renderer's primitives
// point at.
struct FloorScene {
  GMANLight const distantLight;
  GMANLight const areaLight;
  GMANMatrix4 const identity;
  gman::VSPerspective viewingSys;
  GMANPathtraceRenderer renderer;

  explicit FloorScene(Lighting lighting)
      : distantLight(GMAN_LIGHT_DISTANT, GMANColor(kDistantIntensity, kDistantIntensity, kDistantIntensity),
                     GMANPoint(), GMANVector(kDistantDirectionX, kDistantDirectionY, kDistantDirectionZ)),
        areaLight(GMAN_LIGHT_AREA, GMANColor(kAreaLe, kAreaLe, kAreaLe), GMANPoint(), GMANVector()),
        viewingSys(kWidth, kHeight, squareScreenWindow(), identity, kViewFov, kSceneNear, kSceneFar) {
    GMANRayPolygon* floor = buildFloor();
    gman::Appearance appearance;
    appearance.shader = loadShader("matte", matteParams(1.0f));
    appearance.Cs = GMANColor(kFlatReflectance, kFlatReflectance, kFlatReflectance);
    appearance.Os = GMANColor(1.0f, 1.0f, 1.0f);
    if (lighting == Lighting::distant) {
      appearance.lights = {&distantLight};
    }
    floor->setAppearance(appearance);
    renderer.getWorldManager()->add(floor);
    if (lighting == Lighting::emittingDisk) {
      addEmittingDisk();
    }
  }

  void addEmittingDisk() {
    GMANMatrix4 place;
    place.rot(GMANRadians(kDiskTiltDegrees), 1.0f, 0.0f, 0.0f);
    place.trans(kDiskCentreX, kDiskCentreY, kDiskCentreZ);
    GMANTransform const transform = makeTransform(place);
    auto const disk = new GMANRayDisk(0.0f, kDiskRadius, 360.0f, GMANParameterList(), transform);
    gman::Appearance appearance;
    appearance.areaLight = &areaLight;
    appearance.Os = GMANColor(1.0f, 1.0f, 1.0f);
    disk->setAppearance(appearance);
    renderer.getWorldManager()->add(disk);
  }

  Frame render(RtInt maximum, RtFloat variance, Minsamples const& minsamples = std::nullopt) {
    GMANOptions options;
    options.setFormat(kWidth, kHeight, 1.0f);
    options.setPathtracerSamples(maximum);
    options.setBackground(kBlack);
    options.setPixelFilter(RiBoxFilter, kFilterWidth, kFilterWidth);
    options.setPixelSamples(1.0f, 1.0f);
    options.setPixelVariance(variance);
    if (minsamples) {
      options.setNamedOption("pathtracer", "minsamples", *minsamples);
    }

    GMANFrameBuffer frameBuffer(kWidth, kHeight, kBlack);
    GMANAttributes const attributes;
    beginLogCapture(kRenderLog);
    renderer.render(&frameBuffer, &viewingSys, options, attributes);
    Frame frame;
    frame.log = endLogCapture(kRenderLog);
    frame.counts = renderer.slotPathCounts();
    for (RtInt y = 0; y < kHeight; ++y) {
      for (RtInt x = 0; x < kWidth; ++x) {
        frame.pixels.push_back(frameBuffer.getPixel(x, y));
      }
    }
    return frame;
  }

  std::vector<std::size_t> backgroundSlots() const {
    std::vector<std::size_t> slots;
    for (RtInt y = 0; y < kHeight; ++y) {
      for (RtInt x = 0; x < kWidth; ++x) {
        if (renderer.getDepth(x, y) == RI_INFINITY) {
          slots.push_back(slotIndex(x, y));
        }
      }
    }
    return slots;
  }

  std::vector<std::size_t> floorSlots() {
    std::vector<std::size_t> slots;
    for (RtInt y = 0; y < kHeight; ++y) {
      for (RtInt x = 0; x < kWidth; ++x) {
        if (pixelMeasured(viewingSys, x, y)) {
          slots.push_back(slotIndex(x, y));
        }
      }
    }
    return slots;
  }

  static std::size_t slotIndex(RtInt x, RtInt y) {
    return static_cast<std::size_t>(y) * static_cast<std::size_t>(kWidth) + static_cast<std::size_t>(x);
  }
};

bool allCountsAre(Frame const& frame, std::vector<std::size_t> const& slots, std::uint32_t expected) {
  for (std::size_t const slot : slots) {
    if (frame.counts[slot] != expected) {
      return false;
    }
  }
  return !slots.empty();
}

bool allCountsAre(Frame const& frame, std::uint32_t expected) {
  for (std::uint32_t const count : frame.counts) {
    if (count != expected) {
      return false;
    }
  }
  return !frame.counts.empty();
}

std::size_t totalPaths(Frame const& frame) {
  std::size_t total = 0;
  for (std::uint32_t const count : frame.counts) {
    total += count;
  }
  return total;
}

std::string infoLine(std::size_t traced, std::size_t allowed) {
  return std::format("gmanpathtracer: traced {} of {} paths.", traced, allowed);
}

std::string minsamplesWarning(std::uint32_t used) {
  return std::format("gmanpathtracer: Option \"pathtracer\" \"minsamples\" must be an integer of at least 2; using {}.",
                     used);
}

GMANOptions::NamedOptionValue integerOption(RtInt value) { return std::vector<RtInt>{value}; }

void checkFlatSlotsAreFlat(FloorScene& flat, std::vector<std::size_t> const& floorSlots,
                           std::vector<std::size_t> const& backgroundSlots) {
  Frame const one = flat.render(kFlatProbeCounts[0], 0.0f);
  Frame const two = flat.render(kFlatProbeCounts[1], 0.0f);
  Frame const four = flat.render(kFlatProbeCounts[2], 0.0f);

  bool floorIdentical = true;
  bool floorLit = true;
  for (std::size_t const slot : floorSlots) {
    floorIdentical = floorIdentical && colorsEqual(one.pixels[slot], two.pixels[slot]) &&
                     colorsEqual(two.pixels[slot], four.pixels[slot]);
    floorLit = floorLit && one.pixels[slot].getRed() > 0.0f;
  }
  check(floorIdentical, "setup: a flat slot's radiance reads the same at 1, 2 and 4 paths, so every path agrees");
  check(floorLit, "setup: a flat slot's radiance is nonzero");

  bool backgroundBlack = true;
  for (std::size_t const slot : backgroundSlots) {
    backgroundBlack = backgroundBlack && colorsEqual(four.pixels[slot], kBlack);
  }
  check(backgroundBlack, "setup: a background slot reads the black default, so a bounce adds exactly 0");
}

void checkSetup(FloorScene& flat, FloorScene& noisy) {
  flat.render(kMaximum, 0.0f);
  noisy.render(kMaximum, 0.0f);
  std::vector<std::size_t> const flatFloor = flat.floorSlots();
  std::vector<std::size_t> const flatBackground = flat.backgroundSlots();
  std::vector<std::size_t> const noisyFloor = noisy.floorSlots();
  check(!flatFloor.empty() && !flatBackground.empty(), "setup: the flat scene holds flat and background slots");
  check(!noisyFloor.empty(), "setup: the noisy scene holds floor slots");
  checkFlatSlotsAreFlat(flat, flatFloor, flatBackground);

  Frame const two = noisy.render(kFlatProbeCounts[1], 0.0f);
  Frame const four = noisy.render(kFlatProbeCounts[2], 0.0f);
  std::size_t varying = 0;
  for (std::size_t const slot : noisyFloor) {
    if (!colorsEqual(two.pixels[slot], four.pixels[slot])) {
      ++varying;
    }
  }
  check(varying * 2 > noisyFloor.size(), "setup: most noisy slots' paths disagree (" + std::to_string(varying) +
                                             " of " + std::to_string(noisyFloor.size()) + ")");
}

void checkOffIsToday(FloorScene& flat, FloorScene& noisy) {
  RtFloat const nan = std::numeric_limits<RtFloat>::quiet_NaN();
  for (RtFloat const variance : {0.0f, -1.0f, nan}) {
    std::string const label = "A: a PixelVariance of " + std::to_string(variance) + " ";
    Frame const flatFrame = flat.render(kMaximum, variance);
    Frame const noisyFrame = noisy.render(kMaximum, variance);
    check(allCountsAre(flatFrame, kMaximum) && allCountsAre(noisyFrame, kMaximum),
          label + "traces every slot to the maximum");
    check(flatFrame.log.find(kInfoPrefix) == std::string::npos && noisyFrame.log.find(kInfoPrefix) == std::string::npos,
          label + "prints no info line");
  }
}

void checkFlatStopsEarly(FloorScene& flat) {
  flat.render(kMaximum, 0.0f);
  std::vector<std::size_t> const floorSlots = flat.floorSlots();
  std::vector<std::size_t> const backgroundSlots = flat.backgroundSlots();

  Frame const byDefault = flat.render(kMaximum, kPositiveVariance);
  check(allCountsAre(byDefault, floorSlots, kDefaultInterval), "B: flat slots trace exactly the default minimum");
  check(allCountsAre(byDefault, backgroundSlots, kDefaultInterval),
        "B: background slots trace exactly the default minimum");

  Frame const explicitMinimum = flat.render(kMaximum, kPositiveVariance, integerOption(kExplicitEvery));
  check(allCountsAre(explicitMinimum, floorSlots, kExplicitEvery),
        "B: flat slots trace exactly an explicit minsamples");
  check(allCountsAre(explicitMinimum, backgroundSlots, kExplicitEvery),
        "B: background slots trace exactly an explicit minsamples");
}

void checkNoisyRunsToMaximum(FloorScene& noisy) {
  Frame const reference = noisy.render(kMaximum, 0.0f);
  std::vector<std::size_t> const floorSlots = noisy.floorSlots();
  Frame const strict = noisy.render(kMaximum, kUnmeetableVariance);
  check(allCountsAre(strict, floorSlots, kMaximum),
        "C: noisy slots trace the maximum at a PixelVariance they cannot meet");

  bool identical = true;
  for (std::size_t slot = 0; slot < strict.counts.size(); ++slot) {
    if (strict.counts[slot] == kMaximum) {
      identical = identical && colorsEqual(strict.pixels[slot], reference.pixels[slot]);
    }
  }
  check(identical, "C: every slot that ran to the maximum equals the PixelVariance 0 render bit for bit");
}

void checkDivisor(FloorScene& flat) {
  Frame const single = flat.render(1, 0.0f);
  flat.render(kMaximum, 0.0f);
  std::vector<std::size_t> const floorSlots = flat.floorSlots();
  Frame const stopped = flat.render(kMaximum, kPositiveVariance);
  Frame const full = flat.render(kMaximum, 0.0f);

  bool stoppedMean = true;
  bool fullMean = true;
  for (std::size_t const slot : floorSlots) {
    stoppedMean = stoppedMean && colorsClose(stopped.pixels[slot], single.pixels[slot], kMeanTolerance);
    fullMean = fullMean && colorsClose(full.pixels[slot], single.pixels[slot], kMeanTolerance);
  }
  check(allCountsAre(stopped, floorSlots, kDefaultInterval), "D setup: the flat slots stopped early");
  check(stoppedMean, "D: a flat slot that stopped early reads its path's radiance");
  check(fullMean, "D: the same slot at PixelVariance 0 agrees");
}

void checkSchedule(FloorScene& flat, FloorScene& noisy) {
  Frame const scheduled = noisy.render(kMaximum, kScheduleVariance, integerOption(kScheduleEvery));
  std::set<std::uint32_t> const allowed = {3, 6, 9, 12, 15, 16};
  std::set<std::uint32_t> seen;
  for (std::uint32_t const count : scheduled.counts) {
    seen.insert(count);
  }
  bool onSchedule = true;
  bool midway = false;
  for (std::uint32_t const count : seen) {
    onSchedule = onSchedule && allowed.contains(count);
    midway = midway || (count >= 9 && count < kMaximum);
  }
  check(onSchedule, "E: with minsamples 3 and N 16 every count lies in {3, 6, 9, 12, 15, 16}");
  check(seen.contains(3) && seen.contains(kMaximum), "E setup: counts of 3 and 16 both occur");
  check(midway, "E setup: some slot stops at a count of 9 or more below 16, between two checks");

  flat.render(kMaximum, 0.0f);
  std::vector<std::size_t> const floorSlots = flat.floorSlots();
  Frame const shortRun = flat.render(kShortMaximum, kPositiveVariance);
  check(allCountsAre(shortRun, floorSlots, kDefaultInterval), "E: unset minsamples at N 10 stops flat slots at 4");
  check(!shortRun.log.empty() && shortRun.log.find("minsamples") == std::string::npos,
        "E: an unset minsamples warns of nothing");
  Frame const longRun = flat.render(kMaximum, kPositiveVariance);
  check(allCountsAre(longRun, floorSlots, kDefaultInterval), "E: unset minsamples at N 16 stops flat slots at 4");

  Frame const belowTwo = flat.render(kMaximum, kPositiveVariance, integerOption(kBelowTwo));
  check(countOccurrences(belowTwo.log, minsamplesWarning(kDefaultInterval)) == 1,
        "E: minsamples 1 warns once, naming the default");
  check(allCountsAre(belowTwo, floorSlots, kDefaultInterval), "E: minsamples 1 takes the default");

  Minsamples const floats = GMANOptions::NamedOptionValue{std::vector<RtFloat>{3.0f}};
  Frame const wrongType = flat.render(kMaximum, kPositiveVariance, floats);
  check(countOccurrences(wrongType.log, minsamplesWarning(kDefaultInterval)) == 1,
        "E: a minsamples that is not an integer warns once");
  check(allCountsAre(wrongType, floorSlots, kDefaultInterval),
        "E: a minsamples that is not an integer takes the default");

  Frame const valid = flat.render(kMaximum, kPositiveVariance, integerOption(kScheduleEvery));
  check(valid.log.find("minsamples") == std::string::npos, "E: a valid minsamples warns of nothing");

  Frame const atMaximum = flat.render(kMaximum, kPositiveVariance, integerOption(kMaximum));
  Frame const beyond = flat.render(kMaximum, kPositiveVariance, integerOption(kBeyondMaximum));
  check(allCountsAre(atMaximum, kMaximum) && allCountsAre(beyond, kMaximum),
        "E: a minsamples at or beyond the maximum runs every slot to the maximum");
}

void checkInfoLine(FloorScene& noisy) {
  Frame const frame = noisy.render(kMaximum, kScheduleVariance);
  std::size_t const slots = static_cast<std::size_t>(kWidth) * static_cast<std::size_t>(kHeight);
  std::size_t const allowed = static_cast<std::size_t>(kMaximum) * slots;
  std::size_t const traced = totalPaths(frame);
  check(traced < allowed,
        "F setup: the render saved some paths (" + std::to_string(traced) + " of " + std::to_string(allowed) + ")");
  check(countOccurrences(frame.log, kInfoPrefix) == 1, "F: the info line prints once");
  check(countOccurrences(frame.log, infoLine(traced, allowed)) == 1,
        "F: its two numbers are the sum of slotPathCounts() and N times the slot count");
}

struct PathSample {
  GMANColor L;
  GMANColor alpha;
};

gman::SlotStatistics statisticsOf(std::vector<PathSample> const& paths) {
  gman::SlotStatistics stats;
  for (PathSample const& path : paths) {
    gman::addPath(stats, path.L, path.alpha);
  }
  return stats;
}

bool converges(std::vector<PathSample> const& paths, double threshold) {
  return gman::slotConverged(statisticsOf(paths), static_cast<RtFloat>(threshold));
}

GMANColor const kOpaque(1.0f, 1.0f, 1.0f);

PathSample redPath(RtFloat red) { return {GMANColor(red, 0.0f, 0.0f), kOpaque}; }

void checkMetric() {
  std::vector<PathSample> const threePaths = {{GMANColor(0.0f, 0.0f, 0.0f), kOpaque},
                                              {GMANColor(1.0f, 0.5f, 0.0f), kOpaque},
                                              {GMANColor(2.0f, 1.0f, 0.0f), kOpaque}};
  check(converges(threePaths, kJustAboveSqrtOneThird),
        "G: red 0, 1, 2 has a standard error of sqrt(1/3) and converges at a threshold just above it");
  check(!converges(threePaths, kJustBelowSqrtOneThird),
        "G: red 0, 1, 2 does not converge at a threshold just below its standard error");

  std::vector<PathSample> const redHalf = {redPath(0.0f), redPath(1.0f)};
  check(!converges(redHalf, kHalfError), "G: an exactly representable error of 0.5 is not below a threshold of 0.5");
  check(converges(redHalf, kJustAboveHalfError), "G: that error converges at a threshold just above it");

  std::vector<PathSample> const alphaOnly = {{GMANColor(0.0f, 0.0f, 0.0f), GMANColor(0.0f, 0.0f, 0.0f)},
                                             {GMANColor(0.0f, 0.0f, 0.0f), GMANColor(1.0f, 0.0f, 0.0f)}};
  check(!converges(alphaOnly, kJustBelowHalfError), "G: a coverage estimate that alone varies does not converge");
  check(converges(alphaOnly, kJustAboveHalfError), "G: that coverage estimate converges above its error");

  std::vector<PathSample> const opposed = {{GMANColor(0.0f, 1.0f, 0.0f), kOpaque},
                                           {GMANColor(1.0f, 0.0f, 0.0f), kOpaque}};
  check(!converges(opposed, kJustBelowHalfError),
        "G: red and green moving in opposite directions, their mean constant, do not converge below either error");
  check(converges(opposed, kJustAboveHalfError), "G: they converge above it");

  std::vector<PathSample> const lone = {redPath(1.0f)};
  check(!converges(lone, kHugeThreshold) && !converges({}, kHugeThreshold), "G: fewer than two paths never converge");

  std::vector<PathSample> const identical = {redPath(0.75f), redPath(0.75f)};
  check(converges(identical, kTinyThreshold), "G: two identical paths converge at any positive threshold");
  check(!converges(identical, 0.0), "G: two identical paths do not converge at a threshold of 0");
  check(!converges(identical, -1.0), "G: nor at a negative threshold");
  check(!gman::slotConverged(statisticsOf(identical), std::numeric_limits<RtFloat>::quiet_NaN()),
        "G: nor at a NaN threshold");
}

int runChecks() {
  FloorScene flat(Lighting::distant);
  FloorScene noisy(Lighting::emittingDisk);

  checkSetup(flat, noisy);
  checkOffIsToday(flat, noisy);
  checkFlatStopsEarly(flat);
  checkNoisyRunsToMaximum(noisy);
  checkDivisor(flat);
  checkSchedule(flat, noisy);
  checkInfoLine(noisy);
  checkMetric();

  return checkSummary("the path tracer stops a slot at the first scheduled check where its standard error falls "
                      "below PixelVariance, and traces the maximum otherwise");
}

} // namespace

int main() { return runChecks(); }
