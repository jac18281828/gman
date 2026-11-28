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
 * The path tracer samples a slot in two stages under a positive PixelVariance:
 * a pilot of minsamples paths sizes a fresh batch, and only the fresh batch
 * is the slot's estimate. Rendered in process at one slot per pixel, so a
 * pixel reads its slot bit-equal, over three kinds of slot: background-only
 * slots, flat slots whose every path returns the same radiance, and noisy
 * slots under an emitting sphere. Off is today: a PixelVariance of 0, a
 * negative one and NaN trace every slot to the maximum. Flat and background
 * slots trace the pilot and the floor of the fresh batch, twice minsamples,
 * and the slot's mean divides by the fresh batch's paths. A noisy slot that
 * cannot meet the threshold traces the maximum, pilot included. With
 * minsamples 3 and a maximum of 16 every count lies in [6, 16].
 * Minsamples is by default the root of the maximum; a bad one warns once and
 * takes the default; a pilot that would reach the maximum is skipped, and
 * the slot renders as at PixelVariance 0. One info line reports the paths
 * traced against the paths allowed. The sizing function is called directly
 * on hand-computed pilots.
 *
 * With the argument --measure <gman> <materials.rib> <outdir> [run ...] the
 * program does none of that. It renders samples/materials.rib through gman
 * at several path counts and PixelVariance values, each into <outdir> as a
 * 32-bit float TIFF, and asserts nothing. Named runs render alone, in table
 * order; with none named every run renders. Once all have ended it prints
 * each run's wall time, paths traced and RMSE against the reference run,
 * and, against the baseline run, the signed difference of each channel's
 * image mean in percent, the paired z of that difference over 8 x 8 pixel
 * blocks, and the percent of changed red pixels that fell. A figure whose
 * run did not render prints "-".
 */

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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

#include <tiffio.h>

#include "check.h"
#include "gmanattributes.h"
#include "gmancolor.h"
#include "gmanframebuffer.h"
#include "gmanlightsourcemgr.h"
#include "gmanlog.h"
#include "gmanmath.h"
#include "gmanmatrix4.h"
#include "gmanoptions.h"
#include "gmanowned.h"
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
#include "gmanworld.h"
#include "maketransform.h"
#include "pathtracerarealightscene.h"
#include "pathtracerscene.h"
#include "ri.h"
#include "rungman.h"

namespace {

constexpr RtInt kWidth = 24;
constexpr RtInt kHeight = 16;
constexpr RtInt kMaximum = 16;
constexpr RtInt kShortMaximum = 10;
constexpr RtInt kTinyMaximum = 4;
constexpr RtInt kSizingPilot = 3;
constexpr RtInt kExplicitPilot = 5;
constexpr RtInt kBelowTwo = 1;
constexpr RtInt kBeyondMaximum = 20;
constexpr std::uint32_t kDefaultPilot = 4;
constexpr std::uint32_t kTinyDefaultPilot = 2;
constexpr std::uint32_t kFlatProbeCounts[] = {1, 2, 4};

constexpr RtFloat kPositiveVariance = 0.01f;
constexpr RtFloat kUnmeetableVariance = 1.0e-9f;
constexpr RtFloat kSizingVariance = 0.02f;
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

constexpr RtFloat kExactTolerance = 0.5f;
constexpr RtFloat kTinyTolerance = 1.0e-6f;
constexpr std::uint32_t kFewest = 2;
constexpr std::uint32_t kLowerBound = 20;
constexpr std::uint32_t kUpperBound = 100;
constexpr std::uint32_t kNarrowUpperBound = 10;
constexpr std::uint32_t kFarLowerBound = 3;
constexpr std::uint32_t kSpreadPaths = 17;
constexpr std::uint32_t kUnitSpreadPaths = 5;

constexpr std::uint32_t kMeasureSamplesPerAxis = 2;
constexpr std::uint16_t kFloatBits = 32;
constexpr std::uint16_t kRgbaSamples = 4;
constexpr std::uint32_t kBlockSize = 8;
constexpr std::size_t kColourChannels = 3;
constexpr double kPercent = 100.0;
char const* const kMeasureDisplayLine = "Display \"materials.png\" \"file\" \"rgba\"";
char const* const kMeasureSamplesLine = "Option \"pathtracer\" \"integer samples\" [64]";
char const* const kMeasurePixelSamplesLine = "PixelSamples 2 2";
constexpr char const* kMeasureReference = "reference";
constexpr char const* kMeasureBaseline = "baseline";

struct MeasureRun {
  char const* name;
  RtInt samples;
  double variance;
};

constexpr MeasureRun kMeasureRuns[] = {
    {kMeasureReference, 4096, 0.0}, {kMeasureBaseline, 512, 0.0}, {"a512-003", 512, 0.003}, {"a512-01", 512, 0.01},
    {"a512-03", 512, 0.03},         {"a1024-003", 1024, 0.003},   {"a1024-01", 1024, 0.01},
};

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
  GMANWorld world;

  explicit FloorScene(Lighting lighting)
      : distantLight(GMAN_LIGHT_DISTANT, GMANColor(kDistantIntensity, kDistantIntensity, kDistantIntensity),
                     GMANPoint(), GMANVector(kDistantDirectionX, kDistantDirectionY, kDistantDirectionZ)),
        areaLight(GMAN_LIGHT_AREA, GMANColor(kAreaLe, kAreaLe, kAreaLe), GMANPoint(), GMANVector()),
        viewingSys(kWidth, kHeight, squareScreenWindow(), identity, kViewFov, kSceneNear, kSceneFar) {
    auto floor = buildFloor();
    gman::Appearance appearance;
    appearance.shader = loadShader("matte", matteParams(1.0f));
    appearance.Cs = GMANColor(kFlatReflectance, kFlatReflectance, kFlatReflectance);
    appearance.Os = GMANColor(1.0f, 1.0f, 1.0f);
    if (lighting == Lighting::distant) {
      appearance.lights = {&distantLight};
    }
    floor->setAppearance(appearance);
    world.add(std::move(floor));
    if (lighting == Lighting::emittingDisk) {
      addEmittingDisk();
    }
  }

  void addEmittingDisk() {
    GMANMatrix4 place;
    place.rot(GMANRadians(kDiskTiltDegrees), 1.0f, 0.0f, 0.0f);
    place.trans(kDiskCentreX, kDiskCentreY, kDiskCentreZ);
    GMANTransform const transform = makeTransform(place);
    auto disk = gman::make<GMANRayDisk>(0.0f, kDiskRadius, 360.0f, GMANParameterList(), transform);
    gman::Appearance appearance;
    appearance.areaLight = &areaLight;
    appearance.Os = GMANColor(1.0f, 1.0f, 1.0f);
    disk->setAppearance(appearance);
    world.add(std::move(disk));
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
    renderer.render(world, &frameBuffer, &viewingSys, options, attributes);
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
  for (RtFloat const variance : {RtFloat(0.0), RtFloat(-1.0), nan}) {
    std::string const label = "A: a PixelVariance of " + std::to_string(variance) + " ";
    Frame const flatFrame = flat.render(kMaximum, variance);
    Frame const noisyFrame = noisy.render(kMaximum, variance);
    check(allCountsAre(flatFrame, kMaximum) && allCountsAre(noisyFrame, kMaximum),
          label + "traces every slot to the maximum");
    check(flatFrame.log.find(kInfoPrefix) == std::string::npos && noisyFrame.log.find(kInfoPrefix) == std::string::npos,
          label + "prints no info line");
  }
}

std::uint32_t pilotAndFloor(std::uint32_t pilot) { return 2 * pilot; }

void checkFlatAndBackgroundCounts(FloorScene& flat) {
  flat.render(kMaximum, 0.0f);
  std::vector<std::size_t> const floorSlots = flat.floorSlots();
  std::vector<std::size_t> const backgroundSlots = flat.backgroundSlots();

  Frame const byDefault = flat.render(kMaximum, kPositiveVariance);
  check(allCountsAre(byDefault, floorSlots, pilotAndFloor(kDefaultPilot)),
        "B: flat slots trace exactly twice the default minimum, a pilot and the floor of the fresh batch");
  check(allCountsAre(byDefault, backgroundSlots, pilotAndFloor(kDefaultPilot)),
        "B: background slots trace exactly twice the default minimum");

  Frame const explicitMinimum = flat.render(kMaximum, kPositiveVariance, integerOption(kExplicitPilot));
  check(allCountsAre(explicitMinimum, floorSlots, pilotAndFloor(kExplicitPilot)),
        "B: flat slots trace exactly twice an explicit minsamples");
  check(allCountsAre(explicitMinimum, backgroundSlots, pilotAndFloor(kExplicitPilot)),
        "B: background slots trace exactly twice an explicit minsamples");
}

void checkNoisyRunsToMaximum(FloorScene& noisy) {
  noisy.render(kMaximum, 0.0f);
  std::vector<std::size_t> const floorSlots = noisy.floorSlots();
  Frame const strict = noisy.render(kMaximum, kUnmeetableVariance);
  check(allCountsAre(strict, floorSlots, kMaximum),
        "C: noisy slots trace exactly the maximum, pilot included, at a PixelVariance they cannot meet");
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
  check(allCountsAre(stopped, floorSlots, pilotAndFloor(kDefaultPilot)), "D setup: the flat slots sized a fresh batch");
  check(stoppedMean, "D: a flat slot that sized a fresh batch reads its path's radiance");
  check(fullMean, "D: the same slot at PixelVariance 0 agrees");
}

bool countsWithin(Frame const& frame, std::uint32_t lowest, std::uint32_t highest) {
  for (std::uint32_t const count : frame.counts) {
    if (count < lowest || count > highest) {
      return false;
    }
  }
  return !frame.counts.empty();
}

void checkSizingInRender(FloorScene& noisy) {
  Frame const sized = noisy.render(kMaximum, kSizingVariance, integerOption(kSizingPilot));
  std::set<std::uint32_t> seen;
  for (std::uint32_t const count : sized.counts) {
    seen.insert(count);
  }
  std::uint32_t const floor = pilotAndFloor(kSizingPilot);
  bool between = false;
  for (std::uint32_t const count : seen) {
    between = between || (count > floor && count < kMaximum);
  }
  check(countsWithin(sized, floor, kMaximum), "E: with minsamples 3 and N 16 every count lies in [6, 16]");
  check(seen.contains(floor) && seen.contains(kMaximum), "E setup: counts of 6 and 16 both occur");
  check(between, "E setup: some slot's count lies strictly between 6 and 16");
}

void checkDefaultPilotSize(FloorScene& flat) {
  flat.render(kMaximum, 0.0f);
  std::vector<std::size_t> const floorSlots = flat.floorSlots();
  Frame const shortRun = flat.render(kShortMaximum, kPositiveVariance);
  check(allCountsAre(shortRun, floorSlots, pilotAndFloor(kDefaultPilot)),
        "E: unset minsamples at N 10 sizes flat slots to 8");
  check(!shortRun.log.empty() && shortRun.log.find("minsamples") == std::string::npos,
        "E: an unset minsamples warns of nothing");
  Frame const longRun = flat.render(kMaximum, kPositiveVariance);
  check(allCountsAre(longRun, floorSlots, pilotAndFloor(kDefaultPilot)),
        "E: unset minsamples at N 16 sizes flat slots to 8");
}

void checkPilotSkippedWhenTwiceReachesMaximum(FloorScene& noisy) {
  Frame const fixed = noisy.render(kShortMaximum, 0.0f);
  Frame const skipped = noisy.render(kShortMaximum, kPositiveVariance, integerOption(kExplicitPilot));
  check(allCountsAre(skipped, kShortMaximum), "E: minsamples 5 at N 10 traces every slot's N paths");
  bool identical = skipped.pixels.size() == fixed.pixels.size();
  for (std::size_t slot = 0; identical && slot < fixed.pixels.size(); ++slot) {
    identical = colorsEqual(skipped.pixels[slot], fixed.pixels[slot]);
  }
  check(identical, "E: every pixel equals the PixelVariance 0 render bit for bit when twice minsamples is N");

  Frame const tiny = noisy.render(kTinyMaximum, kPositiveVariance);
  check(allCountsAre(tiny, kTinyMaximum), "E: the default minsamples of 2 at N 4 traces every slot's N paths");
  check(kTinyDefaultPilot * 2 == static_cast<std::uint32_t>(kTinyMaximum), "E setup: twice the default is N at N 4");
}

void checkBadMinsamplesWarns(FloorScene& flat) {
  flat.render(kMaximum, 0.0f);
  std::vector<std::size_t> const floorSlots = flat.floorSlots();
  Frame const belowTwo = flat.render(kMaximum, kPositiveVariance, integerOption(kBelowTwo));
  check(countOccurrences(belowTwo.log, minsamplesWarning(kDefaultPilot)) == 1,
        "E: minsamples 1 warns once, naming the default");
  check(allCountsAre(belowTwo, floorSlots, pilotAndFloor(kDefaultPilot)), "E: minsamples 1 takes the default");

  Minsamples const floats = GMANOptions::NamedOptionValue{std::vector<RtFloat>{3.0f}};
  Frame const wrongType = flat.render(kMaximum, kPositiveVariance, floats);
  check(countOccurrences(wrongType.log, minsamplesWarning(kDefaultPilot)) == 1,
        "E: a minsamples that is not an integer warns once");
  check(allCountsAre(wrongType, floorSlots, pilotAndFloor(kDefaultPilot)),
        "E: a minsamples that is not an integer takes the default");

  Frame const valid = flat.render(kMaximum, kPositiveVariance, integerOption(kSizingPilot));
  check(valid.log.find("minsamples") == std::string::npos, "E: a valid minsamples warns of nothing");

  Frame const atMaximum = flat.render(kMaximum, kPositiveVariance, integerOption(kMaximum));
  Frame const beyond = flat.render(kMaximum, kPositiveVariance, integerOption(kBeyondMaximum));
  check(allCountsAre(atMaximum, kMaximum) && allCountsAre(beyond, kMaximum),
        "E: a minsamples at or beyond the maximum runs every slot to the maximum");
}

void checkInfoLine(FloorScene& noisy) {
  Frame const frame = noisy.render(kMaximum, kSizingVariance);
  std::size_t const slots = static_cast<std::size_t>(kWidth) * static_cast<std::size_t>(kHeight);
  std::size_t const allowed = static_cast<std::size_t>(kMaximum) * slots;
  std::size_t const traced = totalPaths(frame);
  check(traced < allowed,
        "F setup: the render saved some paths (" + std::to_string(traced) + " of " + std::to_string(allowed) + ")");
  check(countOccurrences(frame.log, kInfoPrefix) == 1, "F: the info line prints once");
  check(countOccurrences(frame.log, infoLine(traced, allowed)) == 1,
        "F: its two numbers are the sum of slotPathCounts() and N times the slot count");
  check(traced <= allowed, "F: the paths traced are at most the paths allowed");
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

std::uint32_t sizeOf(std::vector<PathSample> const& pilot, RtFloat tolerance, std::uint32_t lowest,
                     std::uint32_t highest) {
  return gman::requiredPaths(statisticsOf(pilot), tolerance, lowest, highest);
}

GMANColor const kOpaque(1.0f, 1.0f, 1.0f);

PathSample redPath(RtFloat red) { return {GMANColor(red, 0.0f, 0.0f), kOpaque}; }

PathSample colourPath(RtFloat red, RtFloat green) { return {GMANColor(red, green, 0.0f), kOpaque}; }

PathSample coveragePath(RtFloat green) { return {GMANColor(0.0f, 0.0f, 0.0f), GMANColor(1.0f, green, 1.0f)}; }

void checkRequiredPaths() {
  std::vector<PathSample> const redSpread = {redPath(0.0f), redPath(2.0f), redPath(4.0f)};
  check(sizeOf(redSpread, kExactTolerance, kFewest, kUpperBound) == kSpreadPaths,
        "G: red 0, 2, 4, variance 4, at a tolerance of 0.5 needs 17 paths");
  check(sizeOf(redSpread, kExactTolerance, kLowerBound, kUpperBound) == kLowerBound,
        "G: the same pilot is raised to a lower bound of 20");
  check(sizeOf(redSpread, kExactTolerance, kFewest, kNarrowUpperBound) == kNarrowUpperBound,
        "G: the same pilot is held to an upper bound of 10");
  check(sizeOf(redSpread, kTinyTolerance, kFewest, kUpperBound) == kUpperBound,
        "G: a tolerance of 1e-6 saturates at the upper bound");

  std::vector<PathSample> const redUnit = {redPath(0.0f), redPath(1.0f), redPath(2.0f)};
  check(sizeOf(redUnit, kExactTolerance, kFewest, kUpperBound) == kUnitSpreadPaths,
        "G: red 0, 1, 2, variance 1, at a tolerance of 0.5 needs 5 paths");

  std::vector<PathSample> const greenSpread = {colourPath(0.0f, 0.0f), colourPath(0.0f, 2.0f), colourPath(0.0f, 4.0f)};
  check(sizeOf(greenSpread, kExactTolerance, kFewest, kUpperBound) == kSpreadPaths,
        "G: green 0, 2, 4 with red constant needs 17 paths");
  std::vector<PathSample> const opposed = {colourPath(0.0f, 0.0f), colourPath(1.0f, 2.0f), colourPath(2.0f, 4.0f)};
  check(sizeOf(opposed, kExactTolerance, kFewest, kUpperBound) == kSpreadPaths,
        "G: red 0, 1, 2 with green 0, 2, 4 needs 17 paths, the largest variance governing");
  std::vector<PathSample> const alphaOnly = {coveragePath(0.0f), coveragePath(2.0f), coveragePath(4.0f)};
  check(sizeOf(alphaOnly, kExactTolerance, kFewest, kUpperBound) == kSpreadPaths,
        "G: a coverage estimate that alone varies sizes the batch");

  std::vector<PathSample> const identical = {redPath(0.75f), redPath(0.75f), redPath(0.75f)};
  check(sizeOf(identical, kExactTolerance, kFarLowerBound, kUpperBound) == kFarLowerBound,
        "G: paths that all agree need the lower bound");

  RtFloat const nan = std::numeric_limits<RtFloat>::quiet_NaN();
  check(sizeOf({redPath(1.0f)}, kExactTolerance, kFewest, kUpperBound) == kUpperBound,
        "G: one path answers the upper bound");
  check(sizeOf({}, kExactTolerance, kFewest, kUpperBound) == kUpperBound, "G: no path answers the upper bound");
  check(sizeOf({redPath(0.0f), redPath(nan), redPath(0.0f)}, kExactTolerance, kFewest, kUpperBound) == kUpperBound,
        "G: a NaN path answers the upper bound");
  check(sizeOf(redSpread, 0.0f, kFewest, kUpperBound) == kUpperBound, "G: a tolerance of 0 answers the upper bound");
  check(sizeOf(redSpread, -kExactTolerance, kFewest, kUpperBound) == kUpperBound,
        "G: a negative tolerance answers the upper bound");
  check(sizeOf(redSpread, nan, kFewest, kUpperBound) == kUpperBound, "G: a NaN tolerance answers the upper bound");
}

struct FloatImage {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::vector<float> rgb;
};

std::optional<FloatImage> readFloatTIFF(std::string const& path) {
  TIFF* const tif = TIFFOpen(path.c_str(), "r");
  if (tif == nullptr) {
    return std::nullopt;
  }
  FloatImage image;
  std::uint16_t bits = 0;
  std::uint16_t samples = 0;
  std::uint16_t format = 0;
  TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &image.width);
  TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &image.height);
  TIFFGetField(tif, TIFFTAG_BITSPERSAMPLE, &bits);
  TIFFGetField(tif, TIFFTAG_SAMPLESPERPIXEL, &samples);
  TIFFGetFieldDefaulted(tif, TIFFTAG_SAMPLEFORMAT, &format);
  if (bits != kFloatBits || samples != kRgbaSamples || format != SAMPLEFORMAT_IEEEFP) {
    TIFFClose(tif);
    return std::nullopt;
  }
  std::vector<float> scanline(static_cast<std::size_t>(image.width) * kRgbaSamples);
  for (std::uint32_t y = 0; y < image.height; ++y) {
    if (TIFFReadScanline(tif, scanline.data(), y) < 0) {
      TIFFClose(tif);
      return std::nullopt;
    }
    for (std::uint32_t x = 0; x < image.width; ++x) {
      for (std::uint32_t c = 0; c < 3; ++c) {
        image.rgb.push_back(scanline[static_cast<std::size_t>(x) * kRgbaSamples + c]);
      }
    }
  }
  TIFFClose(tif);
  return image;
}

double rmse(FloatImage const& image, FloatImage const& reference) {
  double sum = 0.0;
  for (std::size_t i = 0; i < image.rgb.size(); ++i) {
    double const difference = static_cast<double>(image.rgb[i]) - static_cast<double>(reference.rgb[i]);
    sum += difference * difference;
  }
  return std::sqrt(sum / static_cast<double>(image.rgb.size()));
}

std::optional<std::string> replaceOnce(std::string text, std::string const& from, std::string const& to) {
  std::string::size_type const at = text.find(from);
  if (at == std::string::npos) {
    return std::nullopt;
  }
  return text.replace(at, from.size(), to);
}

std::optional<std::string> measureRib(std::string const& materials, MeasureRun const& run) {
  std::optional<std::string> rib =
      replaceOnce(materials, kMeasureDisplayLine,
                  std::format("Display \"{}.tif\" \"file\" \"rgba\"\nQuantize \"rgba\" 0 0 0 0", run.name));
  if (rib) {
    rib = replaceOnce(*rib, kMeasureSamplesLine,
                      std::format("Option \"pathtracer\" \"integer samples\" [{}]", run.samples));
  }
  if (rib) {
    rib = replaceOnce(*rib, kMeasurePixelSamplesLine,
                      std::format("{}\nPixelVariance {}", kMeasurePixelSamplesLine, run.variance));
  }
  return rib;
}

std::optional<std::size_t> tracedPaths(std::string const& output) {
  std::string::size_type const at = output.find(kInfoPrefix);
  if (at == std::string::npos) {
    return std::nullopt;
  }
  return static_cast<std::size_t>(std::strtoull(output.c_str() + at + std::strlen(kInfoPrefix), nullptr, 10));
}

struct MeasureOutcome {
  MeasureRun const* run;
  double seconds;
  std::size_t traced;
  std::size_t allowed;
  FloatImage image;
};

using ChannelTriple = std::array<double, kColourChannels>;

struct Bias {
  ChannelTriple percent;
  ChannelTriple z;
  std::optional<double> fellPercent;
};

ChannelTriple channelMeans(FloatImage const& image) {
  ChannelTriple sums{};
  for (std::size_t i = 0; i < image.rgb.size(); ++i) {
    sums[i % kColourChannels] += image.rgb[i];
  }
  double const pixels = static_cast<double>(image.rgb.size() / kColourChannels);
  for (double& sum : sums) {
    sum /= pixels;
  }
  return sums;
}

std::vector<double> blockMeans(FloatImage const& image, std::size_t channelIndex) {
  std::vector<double> means;
  for (std::uint32_t top = 0; top + kBlockSize <= image.height; top += kBlockSize) {
    for (std::uint32_t left = 0; left + kBlockSize <= image.width; left += kBlockSize) {
      double sum = 0.0;
      for (std::uint32_t y = top; y < top + kBlockSize; ++y) {
        for (std::uint32_t x = left; x < left + kBlockSize; ++x) {
          sum += image.rgb[(static_cast<std::size_t>(y) * image.width + x) * kColourChannels + channelIndex];
        }
      }
      means.push_back(sum / static_cast<double>(kBlockSize * kBlockSize));
    }
  }
  return means;
}

double pairedZ(std::vector<double> const& image, std::vector<double> const& baseline) {
  double const count = static_cast<double>(image.size());
  double mean = 0.0;
  for (std::size_t i = 0; i < image.size(); ++i) {
    mean += (image[i] - baseline[i]) / count;
  }
  double sumSquares = 0.0;
  for (std::size_t i = 0; i < image.size(); ++i) {
    double const deviation = image[i] - baseline[i] - mean;
    sumSquares += deviation * deviation;
  }
  double const standardDeviation = std::sqrt(sumSquares / (count - 1.0));
  if (standardDeviation > 0.0) {
    return mean / (standardDeviation / std::sqrt(count));
  }
  return mean == 0.0 ? 0.0 : std::copysign(std::numeric_limits<double>::infinity(), mean);
}

std::optional<double> fellPercent(FloatImage const& image, FloatImage const& baseline) {
  std::size_t changed = 0;
  std::size_t fell = 0;
  for (std::size_t i = 0; i < image.rgb.size(); i += kColourChannels) {
    if (image.rgb[i] != baseline.rgb[i]) {
      ++changed;
      fell += image.rgb[i] < baseline.rgb[i] ? 1 : 0;
    }
  }
  if (changed == 0) {
    return std::nullopt;
  }
  return kPercent * static_cast<double>(fell) / static_cast<double>(changed);
}

Bias biasAgainst(FloatImage const& image, FloatImage const& baseline) {
  Bias bias{};
  ChannelTriple const imageMeans = channelMeans(image);
  ChannelTriple const baselineMeans = channelMeans(baseline);
  for (std::size_t c = 0; c < kColourChannels; ++c) {
    bias.percent[c] = kPercent * (imageMeans[c] - baselineMeans[c]) / baselineMeans[c];
    bias.z[c] = pairedZ(blockMeans(image, c), blockMeans(baseline, c));
  }
  bias.fellPercent = fellPercent(image, baseline);
  return bias;
}

std::string figure(std::optional<double> value, int precision) {
  return value ? std::format("{:.{}f}", *value, precision) : std::string("-");
}

std::string biasColumns(std::optional<Bias> const& bias) {
  if (!bias) {
    return "bias % R - G - B - z R - G - B - fell % -";
  }
  return std::format("bias % R {:+.3f} G {:+.3f} B {:+.3f} z R {:+.2f} G {:+.2f} B {:+.2f} fell % {}", bias->percent[0],
                     bias->percent[1], bias->percent[2], bias->z[0], bias->z[1], bias->z[2],
                     figure(bias->fellPercent, 1));
}

MeasureOutcome const* findOutcome(std::vector<MeasureOutcome> const& outcomes, char const* name) {
  for (MeasureOutcome const& outcome : outcomes) {
    if (std::strcmp(outcome.run->name, name) == 0) {
      return &outcome;
    }
  }
  return nullptr;
}

void printOutcomes(std::vector<MeasureOutcome> const& outcomes) {
  MeasureOutcome const* const reference = findOutcome(outcomes, kMeasureReference);
  MeasureOutcome const* const baseline = findOutcome(outcomes, kMeasureBaseline);
  for (MeasureOutcome const& outcome : outcomes) {
    std::optional<double> const error =
        reference != nullptr ? std::optional<double>(rmse(outcome.image, reference->image)) : std::nullopt;
    std::optional<Bias> bias;
    if (baseline != nullptr && &outcome != baseline) {
      bias = biasAgainst(outcome.image, baseline->image);
    }
    std::printf("%-10s samples %4d variance %-6g wall %8.1f s paths %11zu of %11zu rmse %s %s\n", outcome.run->name,
                outcome.run->samples, outcome.run->variance, outcome.seconds, outcome.traced, outcome.allowed,
                figure(error, 6).c_str(), biasColumns(bias).c_str());
  }
  std::fflush(stdout);
}

bool isRunName(std::string const& name) {
  for (MeasureRun const& run : kMeasureRuns) {
    if (name == run.name) {
      return true;
    }
  }
  return false;
}

bool wanted(std::vector<std::string> const& names, MeasureRun const& run) {
  return names.empty() || std::find(names.begin(), names.end(), run.name) != names.end();
}

int runMeasure(std::string const& gman, std::string const& materialsPath, std::string const& outdir,
               std::vector<std::string> const& names) {
  for (std::string const& name : names) {
    if (isRunName(name)) {
      continue;
    }
    std::fprintf(stderr, "measure: unknown run \"%s\"; the runs are", name.c_str());
    for (MeasureRun const& run : kMeasureRuns) {
      std::fprintf(stderr, " %s", run.name);
    }
    std::fprintf(stderr, "\n");
    return 2;
  }

  std::string const materials = readFile(materialsPath);
  std::vector<MeasureOutcome> outcomes;

  for (MeasureRun const& run : kMeasureRuns) {
    if (!wanted(names, run)) {
      continue;
    }
    std::optional<std::string> const rib = measureRib(materials, run);
    if (!rib) {
      std::fprintf(stderr, "measure: %s lacks a line the runs rewrite\n", materialsPath.c_str());
      return 1;
    }
    std::string const ribName = std::string(run.name) + ".rib";
    std::ofstream(outdir + "/" + ribName, std::ios::binary) << *rib;

    GMANRunOptions options;
    options.workingDirectory = outdir;
    auto const start = std::chrono::steady_clock::now();
    GMANRunResult const result = runGman(gman, {"-r", "gmanpathtracer", ribName}, options);
    double const seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();

    std::optional<FloatImage> image = readFloatTIFF(outdir + "/" + run.name + ".tif");
    if (result.exitStatus != 0 || !image) {
      std::fprintf(stderr, "measure: %s failed (exit %d)\n%s\n", run.name, result.exitStatus, result.output.c_str());
      return 1;
    }

    std::size_t const slots =
        static_cast<std::size_t>(image->width) * image->height * kMeasureSamplesPerAxis * kMeasureSamplesPerAxis;
    std::size_t const allowed = static_cast<std::size_t>(run.samples) * slots;
    std::size_t const traced = tracedPaths(result.output).value_or(allowed);
    outcomes.push_back({&run, seconds, traced, allowed, std::move(*image)});
  }
  printOutcomes(outcomes);
  return 0;
}

int runChecks() {
  FloorScene flat(Lighting::distant);
  FloorScene noisy(Lighting::emittingDisk);

  checkSetup(flat, noisy);
  checkOffIsToday(flat, noisy);
  checkFlatAndBackgroundCounts(flat);
  checkNoisyRunsToMaximum(noisy);
  checkDivisor(flat);
  checkSizingInRender(noisy);
  checkDefaultPilotSize(flat);
  checkPilotSkippedWhenTwiceReachesMaximum(noisy);
  checkBadMinsamplesWarns(flat);
  checkInfoLine(noisy);
  checkRequiredPaths();

  return checkSummary("the path tracer sizes a slot's fresh batch from a pilot under PixelVariance, bounded by the "
                      "maximum, and traces the maximum otherwise");
}

} // namespace

int main(int argc, char** argv) {
  if (argc == 1) {
    return runChecks();
  }
  if (argc >= 5 && std::strcmp(argv[1], "--measure") == 0) {
    return runMeasure(argv[2], argv[3], argv[4], std::vector<std::string>(argv + 5, argv + argc));
  }
  std::fprintf(stderr, "usage: %s [--measure <gman> <materials.rib> <outdir> [run ...]]\n", argv[0]);
  return 2;
}
