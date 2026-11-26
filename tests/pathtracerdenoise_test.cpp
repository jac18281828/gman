/* SPDX-License-Identifier: LGPL-2.1-or-later */

/* This is part of GMAN, a RenderMan-compatible renderer.
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
 * The path tracer's denoise stage, rendered in process over a matte floor of
 * Kd 1 and Cs 0.5. Option "pathtracer" "string denoise" is off by default:
 * absent or "none" leaves guidedImage() empty and the image as it was, and
 * any other value warns once and renders without the filter. With "atrous"
 * the first hit of every camera ray writes the guides: the floor's albedo,
 * 0.5, and its normal, camera facing whichever way its vertices wind, and
 * zero where no path hit anything, also under a positive PixelVariance,
 * whose pilot paths never reach them. Under an emitting disk at four paths
 * the frame holds exactly atrousDenoise of the guided image; the test prints
 * the floor's error against a 256-path reference and asserts nothing on it.
 * One info line reports the filter's parameters, the colour sigma included;
 * a bad parameter warns once and takes its default; a black frame returns
 * unchanged. The denoised image is bitwise identical at one worker and at
 * three, leaves alpha as the sampling resolved it and holds, in
 * guidedImage(), the colour as it resolved before the filter.
 */

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <format>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "check.h"
#include "gmanattributes.h"
#include "gmancolor.h"
#include "gmandenoise.h"
#include "gmanframebuffer.h"
#include "gmanlightsourcemgr.h"
#include "gmanlog.h"
#include "gmanmatrix4.h"
#include "gmannormal.h"
#include "gmanoptions.h"
#include "gmanparameterlist.h"
#include "gmanpathtracerenderer.h"
#include "gmanpoint.h"
#include "gmanraydisk.h"
#include "gmanraypolygon.h"
#include "gmantransform.h"
#include "gmanvector.h"
#include "gmanvsperspective.h"
#include "maketransform.h"
#include "pathtracerarealightscene.h"
#include "pathtracerscene.h"
#include "ri.h"

namespace {

constexpr RtInt kWidth = 32;
constexpr RtInt kHeight = 32;
constexpr RtInt kQuickPaths = 4;
constexpr RtInt kReferencePaths = 256;
constexpr RtInt kPilotPaths = 16;
constexpr RtInt kWorkersFew = 1;
constexpr RtInt kWorkersMany = 3;
constexpr RtFloat kPositiveVariance = 0.01f;
constexpr double kGuideTolerance = 1.0e-4;
constexpr RtFloat kFloorReflectance = 0.5f;
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
constexpr RtFloat kDefaultSigmaNormal = 0.5f;
constexpr RtFloat kDefaultSigmaAlbedo = 0.2f;
constexpr int kDefaultIterations = 5;

char const* const kRenderLog = "pathtracerdenoise_render.log";

std::string const kSwitchWarning =
    "gmanpathtracer: Option \"pathtracer\" \"denoise\" must be \"atrous\" or \"none\"; rendering without denoising.";
std::string const kIterationsWarning =
    "gmanpathtracer: Option \"pathtracer\" \"denoiseiterations\" must be an integer of at least 1; using 5.";
std::string const kColorWarning =
    "gmanpathtracer: Option \"pathtracer\" \"denoisecolor\" must be a finite positive float; using the median color "
    "norm in the image.";
std::string const kNormalWarning =
    "gmanpathtracer: Option \"pathtracer\" \"denoisenormal\" must be a finite positive float; using 0.5.";
std::string const kAlbedoWarning =
    "gmanpathtracer: Option \"pathtracer\" \"denoisealbedo\" must be a finite positive float; using 0.2.";
std::string const kInfoPrefix = "gmanpathtracer: denoised with the a-trous filter: ";

using NamedOptions = std::vector<std::pair<std::string, GMANOptions::NamedOptionValue>>;

GMANOptions::NamedOptionValue text(std::string const& value) { return std::vector<std::string>{value}; }
GMANOptions::NamedOptionValue integer(RtInt value) { return std::vector<RtInt>{value}; }
GMANOptions::NamedOptionValue real(RtFloat value) { return std::vector<RtFloat>{value}; }

NamedOptions atrous() { return {{"denoise", text("atrous")}}; }

NamedOptions atrousWith(std::string const& name, GMANOptions::NamedOptionValue value) {
  NamedOptions options = atrous();
  options.emplace_back(name, std::move(value));
  return options;
}

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

enum class Lighting { none, distant, emittingDisk };
enum class Winding { forward, reversed };

struct Frame {
  std::string log;
  std::vector<GMANColor> pixels;
  std::vector<GMANAlpha> alphas;
  gman::GuidedImage guided;
  std::vector<std::uint32_t> counts;
};

struct Settings {
  RtInt paths = kQuickPaths;
  RtFloat variance = 0.0f;
  RtInt workers = kWorkersMany;
  NamedOptions named;
};

class FloorScene {
public:
  GMANLight const distantLight;
  GMANLight const areaLight;
  GMANMatrix4 const identity;
  gman::VSPerspective viewingSys;
  GMANPathtraceRenderer renderer;

  explicit FloorScene(Lighting lighting, Winding winding = Winding::forward)
      : distantLight(GMAN_LIGHT_DISTANT, GMANColor(kDistantIntensity, kDistantIntensity, kDistantIntensity),
                     GMANPoint(), GMANVector(kDistantDirectionX, kDistantDirectionY, kDistantDirectionZ)),
        areaLight(GMAN_LIGHT_AREA, GMANColor(kAreaLe, kAreaLe, kAreaLe), GMANPoint(), GMANVector()),
        viewingSys(kWidth, kHeight, squareScreenWindow(), identity, kViewFov, kSceneNear, kSceneFar) {
    GMANRayPolygon* floor = buildFloor();
    if (winding == Winding::reversed) {
      floor = buildReversedFloor();
    }
    gman::Appearance appearance;
    appearance.shader = loadShader("matte", matteParams(1.0f));
    appearance.Cs = GMANColor(kFloorReflectance, kFloorReflectance, kFloorReflectance);
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

  Frame render(Settings const& settings) {
    GMANOptions options;
    options.setFormat(kWidth, kHeight, 1.0f);
    options.setPathtracerSamples(settings.paths);
    options.setBackground(GMANColor(0.0f, 0.0f, 0.0f));
    options.setPixelFilter(RiBoxFilter, kFilterWidth, kFilterWidth);
    options.setPixelSamples(1.0f, 1.0f);
    options.setPixelVariance(settings.variance);
    for (auto const& [name, value] : settings.named) {
      options.setNamedOption("pathtracer", name, value);
    }
    renderer.setWorkers(settings.workers);

    GMANFrameBuffer frameBuffer(kWidth, kHeight, GMANColor(0.0f, 0.0f, 0.0f));
    GMANAttributes const attributes;
    beginLogCapture(kRenderLog);
    renderer.render(&frameBuffer, &viewingSys, options, attributes);
    Frame frame;
    frame.log = endLogCapture(kRenderLog);
    frame.guided = renderer.guidedImage();
    frame.counts = renderer.slotPathCounts();
    for (RtInt y = 0; y < kHeight; ++y) {
      for (RtInt x = 0; x < kWidth; ++x) {
        frame.pixels.push_back(frameBuffer.getPixel(x, y));
        frame.alphas.push_back(frameBuffer.getAlpha(x, y));
      }
    }
    return frame;
  }

  bool measured(int x, int y) { return pixelMeasured(viewingSys, x, y); }

private:
  static GMANRayPolygon* buildReversedFloor() {
    std::vector<GMANPoint> const verts = {
        GMANPoint(kFloorXMin, kFloorY, kFloorZMin), GMANPoint(kFloorXMax, kFloorY, kFloorZMin),
        GMANPoint(kFloorXMax, kFloorY, kFloorZMax), GMANPoint(kFloorXMin, kFloorY, kFloorZMax)};
    return new GMANRayPolygon(verts, GMANParameterList());
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
};

std::size_t pixelIndex(int x, int y) { return static_cast<std::size_t>(y) * static_cast<std::size_t>(kWidth) + x; }

bool bitwiseEqual(std::vector<GMANColor> const& a, std::vector<GMANColor> const& b) {
  if (a.size() != b.size()) {
    return false;
  }
  for (std::size_t i = 0; i < a.size(); ++i) {
    for (int c = 0; c < 3; ++c) {
      if (channel(a[i], c) != channel(b[i], c)) {
        return false;
      }
    }
  }
  return true;
}

bool alphasEqual(std::vector<GMANAlpha> const& a, std::vector<GMANAlpha> const& b) {
  if (a.size() != b.size()) {
    return false;
  }
  for (std::size_t i = 0; i < a.size(); ++i) {
    for (int c = 0; c < 3; ++c) {
      if (channel(a[i], c) != channel(b[i], c)) {
        return false;
      }
    }
  }
  return true;
}

bool guideEmpty(gman::GuidedImage const& guided) {
  return guided.width == 0 && guided.height == 0 && guided.color.empty() && guided.albedo.empty() &&
         guided.normal.empty();
}

int occurrences(Frame const& frame, std::string const& needle) { return countOccurrences(frame.log, needle); }

double rmsError(FloorScene& scene, std::vector<GMANColor> const& image, std::vector<GMANColor> const& reference) {
  double sum = 0.0;
  std::size_t count = 0;
  for (int y = 0; y < kHeight; ++y) {
    for (int x = 0; x < kWidth; ++x) {
      if (!scene.measured(x, y)) {
        continue;
      }
      for (int c = 0; c < 3; ++c) {
        double const difference = channel(image[pixelIndex(x, y)], c) - channel(reference[pixelIndex(x, y)], c);
        sum += difference * difference;
        ++count;
      }
    }
  }
  return std::sqrt(sum / static_cast<double>(count));
}

bool guidesMatchFloor(FloorScene& scene, Frame const& frame, bool& sawMeasured, bool& sawSky) {
  bool ok = true;
  for (int y = 0; y < kHeight; ++y) {
    for (int x = 0; x < kWidth; ++x) {
      std::size_t const i = pixelIndex(x, y);
      GMANColor const albedo = frame.guided.albedo[i];
      GMANNormal const normal = frame.guided.normal[i];
      if (scene.measured(x, y)) {
        sawMeasured = true;
        for (int c = 0; c < 3; ++c) {
          ok = ok && std::fabs(channel(albedo, c) - kFloorReflectance) <= kGuideTolerance;
        }
        ok = ok && std::fabs(normal.getX()) <= kGuideTolerance && std::fabs(normal.getY() - 1.0f) <= kGuideTolerance &&
             std::fabs(normal.getZ()) <= kGuideTolerance;
      } else if (scene.renderer.getDepth(x, y) == RI_INFINITY) {
        sawSky = true;
        for (int c = 0; c < 3; ++c) {
          ok = ok && channel(albedo, c) == 0.0;
        }
        ok = ok && normal.getX() == 0.0f && normal.getY() == 0.0f && normal.getZ() == 0.0f;
      }
    }
  }
  return ok;
}

RtFloat lowerMedianNorm(std::vector<GMANColor> const& colors) {
  std::vector<double> norms;
  for (GMANColor const& pixel : colors) {
    double const r = pixel.getRed();
    double const g = pixel.getGreen();
    double const b = pixel.getBlue();
    double const norm = std::sqrt(r * r + g * g + b * b);
    if (std::isfinite(norm) && norm > 0.0) {
      norms.push_back(norm);
    }
  }
  if (norms.empty()) {
    return 0.0f;
  }
  std::sort(norms.begin(), norms.end());
  return static_cast<RtFloat>(norms[(norms.size() - 1) / 2]);
}

std::string infoLine(RtFloat sigmaColor, int iterations = kDefaultIterations, RtFloat sigmaNormal = kDefaultSigmaNormal,
                     RtFloat sigmaAlbedo = kDefaultSigmaAlbedo) {
  return std::format("{}{} iterations, sigma color {}, normal {}, albedo {}.", kInfoPrefix, iterations, sigmaColor,
                     sigmaNormal, sigmaAlbedo);
}

void checkOff() {
  FloorScene scene(Lighting::emittingDisk);
  Settings absent;
  Frame const none = [&] {
    Settings settings;
    settings.named = {{"denoise", text("none")}};
    return scene.render(settings);
  }();
  Frame const missing = scene.render(absent);
  check(guideEmpty(missing.guided), "without the option guidedImage() is empty");
  check(guideEmpty(none.guided), "with \"none\" guidedImage() is empty");
  check(bitwiseEqual(missing.pixels, none.pixels), "\"none\" renders the image an absent option renders");
  check(occurrences(missing, kInfoPrefix) == 0 && occurrences(none, kInfoPrefix) == 0 &&
            occurrences(missing, "must be") == 0 && occurrences(none, "must be") == 0,
        "absent and \"none\" log nothing about denoising");

  Settings bogus;
  bogus.named = {{"denoise", text("bogus")}};
  Frame const bogusFrame = scene.render(bogus);
  check(occurrences(bogusFrame, kSwitchWarning) == 1, "an unknown filter name warns once");
  check(guideEmpty(bogusFrame.guided), "an unknown filter name renders without the stage");
  check(bitwiseEqual(bogusFrame.pixels, missing.pixels), "an unknown filter name renders the undenoised image");

  Settings number;
  number.named = {{"denoise", integer(1)}};
  Frame const numberFrame = scene.render(number);
  check(occurrences(numberFrame, kSwitchWarning) == 1, "an integer for the filter name warns once");
  check(guideEmpty(numberFrame.guided), "an integer for the filter name renders without the stage");
}

void checkGuides() {
  FloorScene scene(Lighting::distant);
  Settings settings;
  settings.named = atrous();
  Frame const frame = scene.render(settings);
  bool sawMeasured = false;
  bool sawSky = false;
  check(frame.guided.width == kWidth && frame.guided.height == kHeight, "guidedImage() takes the frame's size");
  check(guidesMatchFloor(scene, frame, sawMeasured, sawSky), "the floor's guides read albedo 0.5 and normal +y");
  check(sawMeasured && sawSky, "the frame holds floor pixels and sky pixels");

  FloorScene reversed(Lighting::distant, Winding::reversed);
  Frame const reversedFrame = reversed.render(settings);
  bool reversedMeasured = false;
  bool reversedSky = false;
  check(guidesMatchFloor(reversed, reversedFrame, reversedMeasured, reversedSky),
        "a floor wound the other way reads the same camera-facing normal");

  Settings adaptive;
  adaptive.paths = kPilotPaths;
  adaptive.variance = kPositiveVariance;
  adaptive.named = atrous();
  Frame const adaptiveFrame = scene.render(adaptive);
  bool adaptiveMeasured = false;
  bool adaptiveSky = false;
  bool pilotRan = false;
  for (std::uint32_t const count : adaptiveFrame.counts) {
    pilotRan = pilotRan || count > 0;
  }
  check(pilotRan && guidesMatchFloor(scene, adaptiveFrame, adaptiveMeasured, adaptiveSky),
        "a pilot's paths add nothing to the guides under a positive PixelVariance");
}

void checkWiring() {
  FloorScene scene(Lighting::emittingDisk);
  Settings reference;
  reference.paths = kReferencePaths;
  Frame const referenceFrame = scene.render(reference);

  Settings noisy;
  Frame const noisyFrame = scene.render(noisy);
  Settings denoised;
  denoised.named = atrous();
  Frame const denoisedFrame = scene.render(denoised);

  gman::AtrousParameters const defaults{};
  std::vector<GMANColor> const filtered = gman::atrousDenoise(denoisedFrame.guided, defaults, kWorkersFew);
  check(bitwiseEqual(denoisedFrame.pixels, filtered), "the frame holds atrousDenoise of guidedImage()");
  check(bitwiseEqual(denoisedFrame.guided.color, noisyFrame.pixels),
        "guidedImage() holds the colour as it resolved before the filter");
  check(alphasEqual(denoisedFrame.alphas, noisyFrame.alphas), "the filter leaves alpha as sampling resolved it");

  bool changed = false;
  for (int y = 0; y < kHeight; ++y) {
    for (int x = 0; x < kWidth; ++x) {
      if (scene.measured(x, y)) {
        for (int c = 0; c < 3; ++c) {
          changed = changed || channel(denoisedFrame.pixels[pixelIndex(x, y)], c) !=
                                   channel(denoisedFrame.guided.color[pixelIndex(x, y)], c);
        }
      }
    }
  }
  check(changed, "the filter changes a measured floor pixel");

  std::printf("RMS against the 256-path reference: noisy %.5f, denoised %.5f\n",
              rmsError(scene, noisyFrame.pixels, referenceFrame.pixels),
              rmsError(scene, denoisedFrame.pixels, referenceFrame.pixels));
}

void checkInfoLine() {
  FloorScene scene(Lighting::emittingDisk);
  Settings settings;
  settings.named = atrous();
  Frame const frame = scene.render(settings);
  RtFloat const sigmaColor = lowerMedianNorm(frame.guided.color);
  check(sigmaColor > 0.0f, "the emitting-disk frame is not black");
  check(occurrences(frame, kInfoPrefix) == 1, "a denoised render logs one info line");
  check(occurrences(frame, infoLine(sigmaColor)) == 1,
        "the info line reports the lower median of the positive colour norms and the default parameters");

  Settings given;
  given.named = atrous();
  given.named.emplace_back("denoiseiterations", integer(3));
  given.named.emplace_back("denoisecolor", real(2.5f));
  given.named.emplace_back("denoisenormal", real(0.25f));
  given.named.emplace_back("denoisealbedo", real(0.125f));
  Frame const givenFrame = scene.render(given);
  check(occurrences(givenFrame, infoLine(2.5f, 3, 0.25f, 0.125f)) == 1, "the info line reports the given parameters");
  check(occurrences(givenFrame, "must be") == 0, "valid parameters warn of nothing");
}

void checkParameters() {
  FloorScene scene(Lighting::emittingDisk);
  Settings probe;
  probe.named = atrous();
  RtFloat const sigmaColor = lowerMedianNorm(scene.render(probe).guided.color);

  struct Case {
    NamedOptions named;
    std::string warning;
    std::string name;
  };
  std::vector<Case> const cases = {
      {atrousWith("denoiseiterations", integer(0)), kIterationsWarning, "denoiseiterations 0"},
      {atrousWith("denoiseiterations", real(3.0f)), kIterationsWarning, "a float denoiseiterations"},
      {atrousWith("denoisecolor", real(-1.0f)), kColorWarning, "denoisecolor -1"},
      {atrousWith("denoisenormal", real(0.0f)), kNormalWarning, "denoisenormal 0"},
      {atrousWith("denoisealbedo", real(std::numeric_limits<RtFloat>::quiet_NaN())), kAlbedoWarning,
       "denoisealbedo NaN"},
  };
  for (Case const& bad : cases) {
    Settings settings;
    settings.named = bad.named;
    Frame const frame = scene.render(settings);
    check(occurrences(frame, bad.warning) == 1, "a bad value warns once: " + bad.name);
    check(occurrences(frame, "must be") == 1, "a bad value warns of nothing else: " + bad.name);
    check(occurrences(frame, infoLine(sigmaColor)) == 1, "a bad value takes the defaults: " + bad.name);
  }
}

void checkBlackFrame() {
  FloorScene scene(Lighting::none);
  Settings off;
  Frame const plain = scene.render(off);
  Settings on;
  on.named = atrous();
  Frame const frame = scene.render(on);
  check(bitwiseEqual(frame.pixels, plain.pixels), "a black frame denoises to the same black frame");
  check(occurrences(frame, infoLine(0.0f)) == 1, "a black frame reports a colour sigma of 0");
}

void checkWorkers() {
  FloorScene scene(Lighting::emittingDisk);
  Settings few;
  few.named = atrous();
  few.workers = kWorkersFew;
  Settings many;
  many.named = atrous();
  many.workers = kWorkersMany;
  Frame const a = scene.render(few);
  Frame const b = scene.render(many);
  check(bitwiseEqual(a.pixels, b.pixels), "a denoised render is bitwise identical at one worker and at three");
}

} // namespace

int main() {
  checkOff();
  checkGuides();
  checkWiring();
  checkInfoLine();
  checkParameters();
  checkBlackFrame();
  checkWorkers();
  return checkSummary("pathtracerdenoise ok");
}
