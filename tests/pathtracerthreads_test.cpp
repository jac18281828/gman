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
 * The path tracer renders its rows through gman::parallelFor, one
 * gman::TextureCache per worker and one dropped-path count per row, and
 * the worker count leaves every pixel unchanged. A floor lit by a point light
 * and an emitting sphere renders byte-identically, colour, alpha and
 * depth, from one worker and from eight, with the same dropped-path count;
 * under a PixelVariance the same scene stops some slots early and runs
 * others to the maximum, and its colour, alpha, depth and every slot's
 * path count still match at one, three and eight workers;
 * the renderer reports which worker rendered each row; a scene that drops
 * paths drops the same count, matching an independent tally of the alpha
 * channel, at one, three and eight workers; a texture no file carries
 * warns once however many workers ran; and a viewing system answers its four
 * camera calls through a const reference.
 *
 * With the argument --scaling the program does none of that. It times the
 * floor scene at one and at eight workers and prints the two wall times
 * and their ratio, asserting nothing.
 */

#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "check.h"
#include "gmanattributes.h"
#include "gmandictionary.h"
#include "gmanframebuffer.h"
#include "gmanlightsourcemgr.h"
#include "gmanlog.h"
#include "gmanmatrix4.h"
#include "gmanoptions.h"
#include "gmanowned.h"
#include "gmanparallel.h"
#include "gmanparameterlist.h"
#include "gmanpathtracerenderer.h"
#include "gmanpoint.h"
#include "gmanray.h"
#include "gmanraypolygon.h"
#include "gmanraysphere.h"
#include "gmanshading.h"
#include "gmanvector.h"
#include "gmanviewingsystem.h"
#include "gmanvsperspective.h"
#include "gmanworld.h"
#include "pathtracerarealightscene.h"
#include "pathtracerscene.h"
#include "ri.h"

namespace {

struct SceneSize {
  RtInt width;
  RtInt height;
  RtInt samples;
};

constexpr SceneSize kFloorSize{48, 32, 12};
constexpr SceneSize kScalingSize{256, 192, 32};
constexpr SceneSize kDropSize{16, 16, 16};
constexpr SceneSize kTextureSize{16, 16, 2};

constexpr RtFloat kSceneNear = 0.5f;
constexpr RtFloat kSceneFar = 50.0f;
constexpr RtFloat kFilterWidth = 1.0f;

constexpr RtFloat kAdaptiveVariance = 0.05f;
constexpr RtFloat kPixelSamples = 2.0f;
constexpr RtInt kSerialWorkers = 1;
constexpr RtInt kThreeWorkers = 3;
constexpr RtInt kParallelWorkers = 8;

GMANColor const kFloorBackground(0.2f, 0.4f, 0.6f);
constexpr RtFloat kFloorReflectance = 0.5f;
constexpr RtFloat kPointIntensity = 60.0f;
constexpr RtFloat kPointX = -3.0f;
constexpr RtFloat kPointY = 4.0f;
constexpr RtFloat kPointZ = 8.0f;
constexpr RtFloat kSphereRadius = 1.0f;
constexpr RtFloat kSphereCentreX = 10.0f;
constexpr RtFloat kSphereCentreY = -2.0f;
constexpr RtFloat kSphereCentreZ = 9.0f;

constexpr RtFloat kDropSphereRadius = 10.0f;
constexpr RtFloat kDropSphereZMin = -8.0f;
constexpr RtFloat kDropReflectance = 0.75f;
constexpr RtFloat kDropIntensity = 100.0f;
constexpr double kDropTolerance = 0.5;

constexpr RtFloat kQuadDepth = 5.0f;
constexpr RtFloat kQuadHalfExtent = 6.0f;
char const* const kMissingTexture = "pathtracerthreads_missing.tif";
char const* const kSerialLog = "pathtracerthreads_serial.log";
char const* const kParallelLog = "pathtracerthreads_parallel.log";
char const* const kDropLog = "pathtracerthreads_drops.log";

constexpr RtInt kViewRes = 64;
constexpr RtFloat kViewFov = 90.0f;
constexpr RtFloat kViewNear = 1.0f;
constexpr RtFloat kViewFar = 100.0f;
constexpr RtFloat kRoundTripTolerance = 1.0e-3f;
constexpr RtFloat kQueryX = 17.0f;
constexpr RtFloat kQueryY = 41.0f;
constexpr RtFloat kViewRotateAngle = 0.4f;
constexpr RtFloat kViewRotateAxisX = 1.0f;
constexpr RtFloat kViewRotateAxisY = 0.3f;
constexpr RtFloat kViewRotateAxisZ = 0.0f;
constexpr RtFloat kViewTranslateX = 2.0f;
constexpr RtFloat kViewTranslateY = -1.5f;
constexpr RtFloat kViewTranslateZ = 4.0f;

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
  forgetWarningsOnce();
}

std::string endLogCapture(std::string const& path) {
  setLogFile("/dev/null");
  setScreenOutput(true);
  return readFile(path);
}

GMANOptions pathtracerOptions(SceneSize const& size, GMANColor const& background) {
  GMANOptions options;
  options.setFormat(size.width, size.height, 1.0f);
  options.setPathtracerSamples(size.samples);
  options.setBackground(background);
  options.setPixelFilter(RiBoxFilter, kFilterWidth, kFilterWidth);
  return options;
}

gman::VSPerspective sceneViewingSystem(SceneSize const& size, GMANMatrix4 const& worldToCamera) {
  return gman::VSPerspective(size.width, size.height, squareScreenWindow(), worldToCamera, kViewFov, kSceneNear,
                             kSceneFar);
}

void renderScene(GMANPathtraceRenderer& renderer, GMANWorld const& world, GMANFrameBuffer& frameBuffer,
                 GMANViewingSystem& viewingSys, GMANOptions const& options) {
  GMANAttributes const attributes;
  renderer.render(world, &frameBuffer, &viewingSys, options, attributes);
}

// A floor lit by one point light, under an emitting sphere off to its side
// whose light reaches the floor by next-event estimation and by a
// BSDF-sampled bounce, so every vertex of a path exercises both. Owns
// everything the renderer's primitives point at, and a renderer fixed to
// one worker count.
struct SphereLitFloor {
  GMANLight const pointLight;
  GMANLight const areaLight;
  GMANOptions options;
  GMANMatrix4 const identity;
  gman::VSPerspective viewingSys;
  GMANFrameBuffer frameBuffer;
  GMANPathtraceRenderer renderer;
  GMANWorld world;

  SphereLitFloor(SceneSize const& size, RtInt workers)
      : pointLight(GMAN_LIGHT_POINT, GMANColor(kPointIntensity, kPointIntensity, kPointIntensity),
                   GMANPoint(kPointX, kPointY, kPointZ), GMANVector()),
        areaLight(GMAN_LIGHT_AREA, GMANColor(kAreaLe, kAreaLe, kAreaLe), GMANPoint(), GMANVector()),
        options(pathtracerOptions(size, kFloorBackground)), viewingSys(sceneViewingSystem(size, identity)),
        frameBuffer(size.width, size.height, kFloorBackground) {
    options.setPixelSamples(kPixelSamples, kPixelSamples);

    auto floor = buildFloor();
    gman::Appearance appearance;
    appearance.shader = loadShader("matte", matteParams(1.0f));
    appearance.Cs = GMANColor(kFloorReflectance, kFloorReflectance, kFloorReflectance);
    appearance.Os = GMANColor(1.0f, 1.0f, 1.0f);
    appearance.lights = {&pointLight};
    floor->setAppearance(appearance);
    world.add(std::move(floor));
    addEmittingSphere(world, areaLight, kSphereRadius, kSphereCentreX, kSphereCentreY, kSphereCentreZ);

    renderer.setWorkers(workers);
  }

  void render() { renderScene(renderer, world, frameBuffer, viewingSys, options); }
};

bool colorsEqual(GMANColor const& a, GMANColor const& b) {
  return a.getRed() == b.getRed() && a.getGreen() == b.getGreen() && a.getBlue() == b.getBlue();
}

bool alphasEqual(GMANAlpha const& a, GMANAlpha const& b) {
  return a.getRed() == b.getRed() && a.getGreen() == b.getGreen() && a.getBlue() == b.getBlue();
}

std::size_t differingPixels(SphereLitFloor const& a, SphereLitFloor const& b, SceneSize const& size) {
  std::size_t differing = 0;
  for (RtInt y = 0; y < size.height; ++y) {
    for (RtInt x = 0; x < size.width; ++x) {
      bool const same = colorsEqual(a.frameBuffer.getPixel(x, y), b.frameBuffer.getPixel(x, y)) &&
                        alphasEqual(a.frameBuffer.getAlpha(x, y), b.frameBuffer.getAlpha(x, y)) &&
                        a.renderer.getDepth(x, y) == b.renderer.getDepth(x, y);
      if (!same) {
        ++differing;
      }
    }
  }
  return differing;
}

std::size_t pixelsWithGeometry(SphereLitFloor const& scene, SceneSize const& size) {
  std::size_t covered = 0;
  for (RtInt y = 0; y < size.height; ++y) {
    for (RtInt x = 0; x < size.width; ++x) {
      if (scene.renderer.getDepth(x, y) != RI_INFINITY) {
        ++covered;
      }
    }
  }
  return covered;
}

bool everyWorkerIdIs(std::vector<RtInt> const& workers, RtInt expected) {
  for (RtInt worker : workers) {
    if (worker != expected) {
      return false;
    }
  }
  return true;
}

bool workerIdsInRange(std::vector<RtInt> const& workers, RtInt count) {
  for (RtInt worker : workers) {
    if (worker < 0 || worker >= count) {
      return false;
    }
  }
  return true;
}

std::size_t distinctWorkerIds(std::vector<RtInt> const& workers) {
  return std::set<RtInt>(workers.begin(), workers.end()).size();
}

void checkImagesAndWorkerIds() {
  SphereLitFloor serial(kFloorSize, kSerialWorkers);
  SphereLitFloor parallel(kFloorSize, kParallelWorkers);
  serial.render();
  parallel.render();

  std::size_t const pixels = static_cast<std::size_t>(kFloorSize.width) * static_cast<std::size_t>(kFloorSize.height);
  std::size_t const covered = pixelsWithGeometry(serial, kFloorSize);
  check(covered > pixels / 8 && covered < pixels,
        "A setup: the floor covers a share of the frame, so the comparison has pixels to disagree on");

  std::size_t const differing = differingPixels(serial, parallel, kFloorSize);
  check(differing == 0, "A: colour, alpha and depth of every pixel match between 1 and 8 workers (" +
                            std::to_string(differing) + " of " + std::to_string(pixels) + " differ)");
  check(serial.renderer.droppedPathCount() == parallel.renderer.droppedPathCount(),
        "A: droppedPathCount() matches between 1 and 8 workers");

  std::size_t const height = static_cast<std::size_t>(kFloorSize.height);
  check(serial.renderer.rowWorkers().size() == height, "B: the 1-worker render reports one worker per row");
  check(everyWorkerIdIs(serial.renderer.rowWorkers(), 0), "B: the 1-worker render runs every row as worker 0");
  check(parallel.renderer.rowWorkers().size() == height, "B: the 8-worker render reports one worker per row");
  check(workerIdsInRange(parallel.renderer.rowWorkers(), kParallelWorkers),
        "B: the 8-worker render runs every row as a worker id in [0, 8)");
}

std::size_t slotsBelow(std::vector<std::uint32_t> const& counts, std::uint32_t limit) {
  std::size_t below = 0;
  for (std::uint32_t const count : counts) {
    if (count < limit) {
      ++below;
    }
  }
  return below;
}

std::size_t slotsAt(std::vector<std::uint32_t> const& counts, std::uint32_t limit) {
  std::size_t at = 0;
  for (std::uint32_t const count : counts) {
    if (count == limit) {
      ++at;
    }
  }
  return at;
}

void checkAdaptiveSamplingAcrossWorkers() {
  SphereLitFloor serial(kFloorSize, kSerialWorkers);
  SphereLitFloor three(kFloorSize, kThreeWorkers);
  SphereLitFloor parallel(kFloorSize, kParallelWorkers);
  for (SphereLitFloor* scene : {&serial, &three, &parallel}) {
    scene->options.setPixelVariance(kAdaptiveVariance);
    scene->render();
  }

  std::uint32_t const maximum = static_cast<std::uint32_t>(kFloorSize.samples);
  std::vector<std::uint32_t> const& counts = serial.renderer.slotPathCounts();
  check(slotsBelow(counts, maximum) > 0, "F setup: some slots stop below the maximum");
  check(slotsAt(counts, maximum) > 0, "F setup: some slots reach the maximum");

  check(differingPixels(serial, three, kFloorSize) == 0,
        "F: colour, alpha and depth match between 1 and 3 workers under a PixelVariance");
  check(differingPixels(serial, parallel, kFloorSize) == 0,
        "F: colour, alpha and depth match between 1 and 8 workers under a PixelVariance");
  check(three.renderer.slotPathCounts() == counts, "F: every slot's path count matches between 1 and 3 workers");
  check(parallel.renderer.slotPathCounts() == counts, "F: every slot's path count matches between 1 and 8 workers");
}

// A sphere around the camera with an opening behind it, under a NaN
// background: every path that escapes through the opening is dropped.
struct DropScene {
  GMANLight const light;
  GMANOptions options;
  GMANMatrix4 const identity;
  gman::VSPerspective viewingSys;
  GMANFrameBuffer frameBuffer;
  GMANPathtraceRenderer renderer;
  GMANWorld world;

  explicit DropScene(RtInt workers)
      : light(GMAN_LIGHT_POINT, GMANColor(kDropIntensity, kDropIntensity, kDropIntensity), GMANPoint(0.0f, 0.0f, 0.0f),
              GMANVector()),
        options(pathtracerOptions(kDropSize, nanBackground())), viewingSys(sceneViewingSystem(kDropSize, identity)),
        frameBuffer(kDropSize.width, kDropSize.height, nanBackground()) {
    options.setPixelSamples(1.0f, 1.0f);

    auto sphere =
        gman::make<GMANRaySphere>(kDropSphereRadius, kDropSphereZMin, kDropSphereRadius, 360.0f, GMANParameterList());
    gman::Appearance appearance;
    appearance.shader = loadShader("matte", matteParams(1.0f));
    appearance.Cs = GMANColor(kDropReflectance, kDropReflectance, kDropReflectance);
    appearance.Os = GMANColor(1.0f, 1.0f, 1.0f);
    appearance.lights = {&light};
    sphere->setAppearance(appearance);
    world.add(std::move(sphere));
    renderer.setWorkers(workers);
  }

  static GMANColor nanBackground() {
    RtFloat const nan = std::numeric_limits<RtFloat>::quiet_NaN();
    return GMANColor(nan, nan, nan);
  }

  void render() { renderScene(renderer, world, frameBuffer, viewingSys, options); }

  double alphaSum() const {
    double sum = 0.0;
    for (RtInt y = 0; y < kDropSize.height; ++y) {
      for (RtInt x = 0; x < kDropSize.width; ++x) {
        sum += static_cast<double>(frameBuffer.getAlpha(x, y).getRed());
      }
    }
    return sum;
  }
};

std::size_t renderDropsAt(RtInt workers, double& alphaTally) {
  DropScene scene(workers);
  scene.render();
  alphaTally = static_cast<double>(kDropSize.samples) *
               (static_cast<double>(kDropSize.width * kDropSize.height) - scene.alphaSum());
  return scene.renderer.droppedPathCount();
}

void checkDropCounts() {
  beginLogCapture(kDropLog);
  double serialTally = 0.0;
  double threeTally = 0.0;
  double parallelTally = 0.0;
  std::size_t const serial = renderDropsAt(kSerialWorkers, serialTally);
  std::size_t const three = renderDropsAt(kThreeWorkers, threeTally);
  std::size_t const parallel = renderDropsAt(kParallelWorkers, parallelTally);
  endLogCapture(kDropLog);

  std::size_t const totalPaths = static_cast<std::size_t>(kDropSize.width) *
                                 static_cast<std::size_t>(kDropSize.height) *
                                 static_cast<std::size_t>(kDropSize.samples);
  check(serial > 0, "C: the 1-worker render drops paths");
  check(serial < totalPaths, "C: the 1-worker render drops fewer paths than the frame holds");
  check(three == serial, "C: the 3-worker render drops as many paths as the 1-worker render (" + std::to_string(three) +
                             " against " + std::to_string(serial) + ")");
  check(parallel == serial, "C: the 8-worker render drops as many paths as the 1-worker render (" +
                                std::to_string(parallel) + " against " + std::to_string(serial) + ")");
  check(std::fabs(serialTally - static_cast<double>(serial)) <= kDropTolerance,
        "C: N*(pixels - alpha sum) equals the 1-worker count within 0.5 (" + std::to_string(serialTally) + ")");
  check(std::fabs(threeTally - static_cast<double>(three)) <= kDropTolerance,
        "C: N*(pixels - alpha sum) equals the 3-worker count within 0.5 (" + std::to_string(threeTally) + ")");
  check(std::fabs(parallelTally - static_cast<double>(parallel)) <= kDropTolerance,
        "C: N*(pixels - alpha sum) equals the 8-worker count within 0.5 (" + std::to_string(parallelTally) + ")");
}

// A one-entry parameter list carrying "texturename", the way RiSurfaceV
// builds one for a single string parameter.
GMANParameterList textureNameParam(std::string const& name) {
  static GMANDictionary dictionary;
  RtToken tokens[1] = {RI_TEXTURENAME};
  char* strs[1] = {const_cast<char*>(name.c_str())};
  RtPointer parms[1] = {strs};
  return GMANParameterList(dictionary, 1, tokens, parms);
}

gman::Appearance paintedPlasticAppearance(std::string const& textureName) {
  GMANAttributes attributes;
  attributes.setSurface(RI_PAINTEDPLASTIC, textureNameParam(textureName));
  return gman::appearanceOf(attributes);
}

// A quad filling the frame, painted plastic naming a texture no file
// carries: every camera ray shades through the worker's texture cache.
struct MissingTextureQuad {
  GMANOptions options;
  GMANMatrix4 const identity;
  gman::VSPerspective viewingSys;
  GMANFrameBuffer frameBuffer;
  GMANPathtraceRenderer renderer;
  GMANWorld world;

  explicit MissingTextureQuad(RtInt workers)
      : options(pathtracerOptions(kTextureSize, GMANColor(0.0f, 0.0f, 0.0f))),
        viewingSys(sceneViewingSystem(kTextureSize, identity)),
        frameBuffer(kTextureSize.width, kTextureSize.height, GMANColor(0.0f, 0.0f, 0.0f)) {
    options.setPixelSamples(1.0f, 1.0f);

    std::vector<GMANPoint> const verts = {GMANPoint(-kQuadHalfExtent, -kQuadHalfExtent, kQuadDepth),
                                          GMANPoint(-kQuadHalfExtent, kQuadHalfExtent, kQuadDepth),
                                          GMANPoint(kQuadHalfExtent, kQuadHalfExtent, kQuadDepth),
                                          GMANPoint(kQuadHalfExtent, -kQuadHalfExtent, kQuadDepth)};
    auto quad = gman::make<GMANRayPolygon>(verts, GMANParameterList());
    quad->setAppearance(paintedPlasticAppearance(kMissingTexture));
    world.add(std::move(quad));
    renderer.setWorkers(workers);
  }

  void render() { renderScene(renderer, world, frameBuffer, viewingSys, options); }
};

struct MissingTextureRun {
  int warnings;
  std::size_t workersThatRan;
};

MissingTextureRun missingTextureRun(RtInt workers, char const* logPath) {
  beginLogCapture(logPath);
  MissingTextureQuad scene(workers);
  scene.render();
  return {countOccurrences(endLogCapture(logPath), kMissingTexture), distinctWorkerIds(scene.renderer.rowWorkers())};
}

void checkMissingTextureWarnsOnce() {
  std::ifstream const existing(kMissingTexture);
  check(!existing.good(), "D setup: no file carries the missing texture's name");

  RtInt const parallelCaches = gman::parallelWorkers(kTextureSize.height, kParallelWorkers);
  check(parallelCaches == kParallelWorkers, "D setup: the frame is tall enough for 8 workers");

  check(missingTextureRun(kSerialWorkers, kSerialLog).warnings == 1,
        "D: the 1-worker render warns once about the missing texture");
  MissingTextureRun const parallel = missingTextureRun(kParallelWorkers, kParallelLog);
  check(parallel.workersThatRan >= 1, "D: the 8-worker render ran its rows on at least one worker");
  check(parallel.warnings == 1, "D: the 8-worker render warns once however many workers ran (" +
                                    std::to_string(parallel.warnings) + " from " +
                                    std::to_string(parallel.workersThatRan) + " workers)");
}

GMANMatrix4 offAxisWorldToCamera() {
  GMANMatrix4 m;
  m.rot(kViewRotateAngle, kViewRotateAxisX, kViewRotateAxisY, kViewRotateAxisZ);
  m.trans(kViewTranslateX, kViewTranslateY, kViewTranslateZ);
  return m;
}

bool raysIdentical(GMANRay const& a, GMANRay const& b) {
  return a.getOrigin().getX() == b.getOrigin().getX() && a.getOrigin().getY() == b.getOrigin().getY() &&
         a.getOrigin().getZ() == b.getOrigin().getZ() && a.getDirection().getX() == b.getDirection().getX() &&
         a.getDirection().getY() == b.getDirection().getY() && a.getDirection().getZ() == b.getDirection().getZ() &&
         a.getTMin() == b.getTMin() && a.getTMax() == b.getTMax();
}

void checkCameraCallsThrough(GMANViewingSystem const& view, GMANViewingSystem& twin, RtFloat x, RtFloat y) {
  check(raysIdentical(view.cameraRay(x, y), twin.cameraRay(x, y)),
        "E: cameraRay through a const reference matches the non-const call");
  check(raysIdentical(view.ray(x, y), twin.ray(x, y)), "E: ray through a const reference matches the non-const call");
  check(!raysIdentical(view.ray(x, y), view.cameraRay(x, y)),
        "E: ray carries the camera ray into world space through a const reference");

  RtFloat constX = x, constY = y, twinX = x, twinY = y;
  view.rasterToScreen(constX, constY);
  twin.rasterToScreen(twinX, twinY);
  check(constX == twinX && constY == twinY, "E: rasterToScreen through a const reference matches the non-const call");

  view.screenToRaster(constX, constY);
  twin.screenToRaster(twinX, twinY);
  check(constX == twinX && constY == twinY, "E: screenToRaster through a const reference matches the non-const call");
  check(std::fabs(constX - x) <= kRoundTripTolerance && std::fabs(constY - y) <= kRoundTripTolerance,
        "E: screenToRaster inverts rasterToScreen through a const reference");
}

void checkConstViewingSystem() {
  GMANMatrix4 const worldToCamera = offAxisWorldToCamera();
  gman::VSPerspective constSide(kViewRes, kViewRes, squareScreenWindow(), worldToCamera, kViewFov, kViewNear, kViewFar);
  gman::VSPerspective mutableSide(kViewRes, kViewRes, squareScreenWindow(), worldToCamera, kViewFov, kViewNear,
                                  kViewFar);
  checkCameraCallsThrough(constSide, mutableSide, kQueryX, kQueryY);
}

int runChecks() {
  checkImagesAndWorkerIds();
  checkAdaptiveSamplingAcrossWorkers();
  checkDropCounts();
  checkMissingTextureWarnsOnce();
  checkConstViewingSystem();

  return checkSummary("the path tracer's rows render through gman::parallelFor: the image, its worker ids, its dropped "
                      "paths and its missing-texture warning hold at every worker count, and a viewing system "
                      "answers its camera calls through a const reference");
}

double timedRenderSeconds(RtInt workers) {
  SphereLitFloor scene(kScalingSize, workers);
  scene.render();
  auto const start = std::chrono::steady_clock::now();
  scene.render();
  return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

int runScaling() {
  double const serialSeconds = timedRenderSeconds(kSerialWorkers);
  double const parallelSeconds = timedRenderSeconds(kParallelWorkers);
  std::printf("scaling: %d x %d at %d samples, 1 worker %.3f s, %d workers %.3f s, ratio %.2f, "
              "hardware default %d workers\n",
              kScalingSize.width, kScalingSize.height, kScalingSize.samples, serialSeconds, kParallelWorkers,
              parallelSeconds, serialSeconds / parallelSeconds, gman::parallelWorkers(kScalingSize.height, 0));
  return 0;
}

} // namespace

int main(int argc, char** argv) {
  if (argc > 1 && std::strcmp(argv[1], "--scaling") == 0) {
    return runScaling();
  }
  return runChecks();
}
