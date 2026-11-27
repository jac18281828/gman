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
 * The ray tracer renders its rows through gman::parallelFor with one
 * gman::TextureCache per worker, and the worker count leaves every
 * pixel unchanged. A scene with shadows, a partly transparent sphere and a mirror
 * renders byte-identically, colour, alpha and depth, from one, three and
 * eight workers, and so does a closed box lit through the radiosity pass;
 * the renderer reports which worker rendered each row; a texture no file
 * carries warns once however many workers ran; and a texture reached only through
 * a second reflection warns once in each of two consecutive renders, which
 * a tracer sampling through the process cache instead warns about in the
 * first render alone; and so does an opacity map only a shadow ray reaches,
 * each worker's shading occluder sampling through that worker's own cache.
 * A wall lit by one disk emitter and shadowed by a sphere renders
 * byte-identically from one, three and eight workers, every area-light draw
 * a function of its camera sample alone; and a disk emitter whose radiance
 * holds a NaN adds nothing to a scene a point light lights, not even on the
 * disk's own pixels, and leaves every pixel finite.
 *
 * With the argument --scaling the program does none of that. It times the
 * direct scene at one and at eight workers and prints the two wall times and
 * their ratio, asserting nothing.
 */

#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "check.h"
#include "gmanattributes.h"
#include "gmancolor.h"
#include "gmandictionary.h"
#include "gmanframebuffer.h"
#include "gmanlightsourcemgr.h"
#include "gmanlog.h"
#include "gmanmath.h"
#include "gmanmatrix4.h"
#include "gmanoptions.h"
#include "gmanowned.h"
#include "gmanparallel.h"
#include "gmanparameterlist.h"
#include "gmanpoint.h"
#include "gmanraydisk.h"
#include "gmanraypolygon.h"
#include "gmanraysphere.h"
#include "gmanraytracerenderer.h"
#include "gmanshading.h"
#include "gmansurfaceshader.h"
#include "gmantransform.h"
#include "gmanvector.h"
#include "gmanvsperspective.h"
#include "maketransform.h"
#include "ri.h"

namespace {

struct FrameSize {
  RtInt width;
  RtInt height;
};

constexpr FrameSize kDirectSize{32, 24};
constexpr FrameSize kScalingSize{384, 256};
constexpr FrameSize kBoxSize{24, 24};
constexpr FrameSize kTextureSize{24, 16};
constexpr FrameSize kReflectionSize{24, 24};

constexpr RtFloat kDirectPixelSamples = 2.0f;
constexpr RtFloat kScalingPixelSamples = 8.0f;
constexpr RtFloat kSinglePixelSample = 1.0f;

constexpr RtInt kSerialWorkers = 1;
constexpr RtInt kThreeWorkers = 3;
constexpr RtInt kParallelWorkers = 8;

GMANColor const kBackground(0.2f, 0.4f, 0.6f);
GMANColor const kOpaque(1.0f, 1.0f, 1.0f);
constexpr RtFloat kHalfOpacity = 0.5f;

constexpr RtFloat kDirectFov = 40.0f;
constexpr RtFloat kDirectNear = 0.5f;
constexpr RtFloat kDirectFar = 50.0f;
constexpr RtFloat kDirectIntensity = 60.0f;
GMANPoint const kDirectLight(-3.0f, 4.0f, 4.0f);

GMANColor const kFloorColor(0.5f, 0.5f, 0.5f);
GMANColor const kWallColor(0.7f, 0.6f, 0.4f);
GMANColor const kBlockerColor(0.8f, 0.3f, 0.3f);
GMANColor const kGlassColor(0.3f, 0.8f, 0.3f);
GMANColor const kMirrorColor(0.9f, 0.9f, 0.9f);
std::array<GMANPoint, 4> const kFloor = {GMANPoint(-8.0f, -2.0f, 2.0f), GMANPoint(-8.0f, -2.0f, 14.0f),
                                         GMANPoint(8.0f, -2.0f, 14.0f), GMANPoint(8.0f, -2.0f, 2.0f)};
std::array<GMANPoint, 4> const kWall = {GMANPoint(-8.0f, -2.0f, 14.0f), GMANPoint(-8.0f, 8.0f, 14.0f),
                                        GMANPoint(8.0f, 8.0f, 14.0f), GMANPoint(8.0f, -2.0f, 14.0f)};
GMANPoint const kBlockerCentre(-1.5f, -1.0f, 6.0f);
constexpr RtFloat kBlockerRadius = 1.0f;
GMANPoint const kGlassCentre(2.5f, -0.5f, 7.0f);
constexpr RtFloat kGlassRadius = 1.0f;
GMANPoint const kMirrorCentre(0.0f, 0.5f, 9.0f);
constexpr RtFloat kMirrorRadius = 1.2f;

constexpr RtFloat kBoxFov = 45.0f;
constexpr RtFloat kBoxNear = 0.5f;
constexpr RtFloat kBoxFar = 50.0f;
constexpr RtFloat kBoxIntensity = 4.0f;
GMANPoint const kBoxLight(0.0f, 2.5f, 4.0f);
constexpr RtFloat kBoxKa = 1.0f;
constexpr RtFloat kBoxKd = 1.0f;
constexpr RtFloat kRadiosityElementSize = 2.8f;
char const* const kRadiosityPass = "radiosity";
GMANColor const kRedWall(0.75f, 0.1f, 0.1f);
GMANColor const kGreenWall(0.1f, 0.75f, 0.1f);
GMANColor const kGreyWall(0.75f, 0.75f, 0.75f);
std::array<GMANPoint, 4> const kBoxLeft = {GMANPoint(-3.0f, -3.0f, -1.0f), GMANPoint(-3.0f, -3.0f, 9.0f),
                                           GMANPoint(-3.0f, 3.0f, 9.0f), GMANPoint(-3.0f, 3.0f, -1.0f)};
std::array<GMANPoint, 4> const kBoxRight = {GMANPoint(3.0f, -3.0f, -1.0f), GMANPoint(3.0f, 3.0f, -1.0f),
                                            GMANPoint(3.0f, 3.0f, 9.0f), GMANPoint(3.0f, -3.0f, 9.0f)};
std::array<std::array<GMANPoint, 4>, 4> const kBoxGrey = {{
    {GMANPoint(-3.0f, -3.0f, -1.0f), GMANPoint(3.0f, -3.0f, -1.0f), GMANPoint(3.0f, 3.0f, -1.0f),
     GMANPoint(-3.0f, 3.0f, -1.0f)},
    {GMANPoint(-3.0f, -3.0f, 9.0f), GMANPoint(-3.0f, 3.0f, 9.0f), GMANPoint(3.0f, 3.0f, 9.0f),
     GMANPoint(3.0f, -3.0f, 9.0f)},
    {GMANPoint(-3.0f, -3.0f, -1.0f), GMANPoint(3.0f, -3.0f, -1.0f), GMANPoint(3.0f, -3.0f, 9.0f),
     GMANPoint(-3.0f, -3.0f, 9.0f)},
    {GMANPoint(-3.0f, 3.0f, -1.0f), GMANPoint(-3.0f, 3.0f, 9.0f), GMANPoint(3.0f, 3.0f, 9.0f),
     GMANPoint(3.0f, 3.0f, -1.0f)},
}};

constexpr RtFloat kTextureFov = 30.0f;
constexpr RtFloat kTextureNear = 0.5f;
constexpr RtFloat kTextureFar = 100.0f;
constexpr RtFloat kQuadDepth = 5.0f;
constexpr RtFloat kQuadHalfExtent = 6.0f;
char const* const kMissingTexture = "raytracerthreads_missing.tif";
char const* const kSerialLog = "raytracerthreads_serial.log";
char const* const kParallelLog = "raytracerthreads_parallel.log";

constexpr RtFloat kReflectionIntensity = 50.0f;
GMANPoint const kReflectionLight(-10.0f, 0.0f, -10.0f);
char const* const kReflectedTexture = "raytracerthreads_reflected.tif";
char const* const kReflectedLogs[] = {"raytracerthreads_reflected_first.log", "raytracerthreads_reflected_second.log",
                                      "raytracerthreads_reflected_bmatte.log", "raytracerthreads_reflected_amatte.log"};
std::array<GMANPoint, 4> const kMirrorA = {GMANPoint(-4.0f, -4.0f, 9.0f), GMANPoint(4.0f, -4.0f, 1.0f),
                                           GMANPoint(4.0f, 4.0f, 1.0f), GMANPoint(-4.0f, 4.0f, 9.0f)};
std::array<GMANPoint, 4> const kMirrorB = {GMANPoint(-16.0f, -8.0f, -1.0f), GMANPoint(-4.0f, -8.0f, 11.0f),
                                           GMANPoint(-4.0f, 8.0f, 11.0f), GMANPoint(-16.0f, 8.0f, -1.0f)};
std::array<GMANPoint, 4> const kQuadQ = {GMANPoint(-25.0f, -15.0f, -20.0f), GMANPoint(5.0f, -15.0f, -20.0f),
                                         GMANPoint(5.0f, 15.0f, -20.0f), GMANPoint(-25.0f, 15.0f, -20.0f)};

char const* const kMissingOpacityMap = "raytracerthreads_missing_opacity.tif";
char const* const kCardShadowLogs[] = {"raytracerthreads_cardshadow_off.log", "raytracerthreads_cardshadow_first.log",
                                       "raytracerthreads_cardshadow_second.log"};
GMANPoint const kCardLight(-4.0f, 0.0f, 0.0f);
constexpr RtFloat kCardDepth = 2.5f;
constexpr RtFloat kCardInnerX = -1.0f;
constexpr RtFloat kCardOuterX = -3.0f;
constexpr RtFloat kCardHalfHeight = 1.0f;
constexpr RtFloat kCardOffPathRise = 20.0f;

constexpr FrameSize kEmitterSize{32, 24};
constexpr RtInt kEmitterSamples = 4;
constexpr RtFloat kEmitterRadianceValue = 40.0f;
constexpr RtFloat kEmitterDark = 0.0f;
constexpr RtFloat kEmitterFacesWall = 180.0f;
constexpr RtFloat kEmitterFacesCamera = 0.0f;
GMANPoint const kWallEmitterCentre(0.0f, 3.0f, 10.0f);
GMANPoint const kViewedEmitterCentre(0.0f, 2.0f, 9.0f);
constexpr RtFloat kEmitterRadius = 1.0f;
GMANPoint const kEmitterBlockerCentre(0.0f, 3.0f, 12.0f);
constexpr RtFloat kEmitterBlockerRadius = 0.8f;
GMANColor const kEmitterBlack(0.0f, 0.0f, 0.0f);
GMANColor const kNaNRadiance(std::numeric_limits<RtFloat>::quiet_NaN(), kEmitterRadianceValue, kEmitterRadianceValue);

GMANOptions::ScreenWindowStruct squareScreenWindow() {
  GMANOptions::ScreenWindowStruct sw;
  sw.left = -1.0f;
  sw.right = 1.0f;
  sw.bottom = -1.0f;
  sw.top = 1.0f;
  return sw;
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
  forgetWarningsOnce();
}

std::string endLogCapture(std::string const& path) {
  setLogFile("/dev/null");
  setScreenOutput(true);
  return readFile(path);
}

GMANParameterList floatParams(std::vector<RtToken> const& tokens, std::vector<RtFloat> values) {
  static GMANDictionary dictionary;
  std::vector<RtToken> names = tokens;
  std::vector<RtPointer> parms;
  for (RtFloat& value : values) {
    parms.push_back(&value);
  }
  return GMANParameterList(dictionary, static_cast<RtInt>(names.size()), names.data(), parms.data());
}

GMANParameterList matteParams(RtFloat ka, RtFloat kd) { return floatParams({RI_KA, RI_KD}, {ka, kd}); }

GMANParameterList textureNameParam(std::string const& name) {
  static GMANDictionary dictionary;
  RtToken tokens[1] = {RI_TEXTURENAME};
  char* strs[1] = {const_cast<char*>(name.c_str())};
  RtPointer parms[1] = {strs};
  return GMANParameterList(dictionary, 1, tokens, parms);
}

GMANParameterList opacityNameParam(std::string const& name) {
  static GMANDictionary dictionary;
  RtToken tokens[1] = {const_cast<RtToken>("string opacityname")};
  char* strs[1] = {const_cast<char*>(name.c_str())};
  RtPointer parms[1] = {strs};
  return GMANParameterList(dictionary, 1, tokens, parms);
}

std::shared_ptr<GMANSurfaceShader const> loadShader(std::string const& name, GMANParameterList const& params) {
  GMANAttributes attributes;
  attributes.setSurface(name, params);
  return attributes.getSurface(0.0);
}

// A scene in camera space, lit by one point light, rendered by its own
// GMANRaytraceRenderer at the worker count its caller names. Owns the light
// the primitives' appearances point at.
struct TracedScene {
  GMANLight const light;
  GMANOptions options;
  GMANMatrix4 const identity;
  gman::VSPerspective viewingSys;
  GMANFrameBuffer frameBuffer;
  GMANRaytraceRenderer renderer;

  TracedScene(FrameSize const& size, RtFloat pixelSamples, RtFloat fov, RtFloat nearClip, RtFloat farClip,
              GMANPoint const& lightPosition, RtFloat intensity)
      : light(GMAN_LIGHT_POINT, GMANColor(intensity, intensity, intensity), lightPosition, GMANVector()),
        viewingSys(size.width, size.height, squareScreenWindow(), identity, fov, nearClip, farClip),
        frameBuffer(size.width, size.height, kBackground) {
    options.setFormat(size.width, size.height, 1.0f);
    options.setPixelSamples(pixelSamples, pixelSamples);
  }

  gman::Appearance appearance(std::shared_ptr<GMANSurfaceShader const> shader, GMANColor const& color,
                              RtFloat opacity) const {
    gman::Appearance result;
    result.shader = std::move(shader);
    result.Cs = color;
    result.Os = GMANColor(opacity, opacity, opacity);
    result.lights = {&light};
    return result;
  }

  void addPolygon(std::array<GMANPoint, 4> const& corners, gman::Appearance const& look) {
    auto polygon =
        gman::make<GMANRayPolygon>(std::vector<GMANPoint>(corners.begin(), corners.end()), GMANParameterList());
    polygon->setAppearance(look);
    renderer.getWorldManager()->add(std::move(polygon));
  }

  void addSphere(GMANPoint const& centre, RtFloat radius, gman::Appearance const& look) {
    GMANMatrix4 place;
    place.trans(centre.getX(), centre.getY(), centre.getZ());
    GMANTransform const transform = makeTransform(place);
    auto sphere = gman::make<GMANRaySphere>(radius, -radius, radius, 360.0f, GMANParameterList(), transform);
    sphere->setAppearance(look);
    renderer.getWorldManager()->add(std::move(sphere));
  }

  void render(RtInt workers) {
    renderer.setWorkers(workers);
    GMANAttributes const attributes;
    renderer.render(&frameBuffer, &viewingSys, options, attributes);
  }
};

struct DirectVariant {
  bool blocker = true;
  bool translucent = true;
  bool mirror = true;
};

// A matte floor and back wall, an opaque sphere shadowing the floor, a sphere
// at half opacity compositing over the wall and a mirror sphere reflecting
// the floor. A variant leaves the blocker or the translucent sphere out, or
// makes the mirror sphere matte.
std::unique_ptr<TracedScene> directScene(FrameSize const& size, RtFloat pixelSamples, DirectVariant const& variant) {
  auto scene = std::make_unique<TracedScene>(size, pixelSamples, kDirectFov, kDirectNear, kDirectFar, kDirectLight,
                                             kDirectIntensity);
  auto const matte = loadShader("matte", matteParams(1.0f, 1.0f));
  auto const mirror = loadShader("mirror", GMANParameterList());

  scene->addPolygon(kFloor, scene->appearance(matte, kFloorColor, 1.0f));
  scene->addPolygon(kWall, scene->appearance(matte, kWallColor, 1.0f));
  if (variant.blocker) {
    scene->addSphere(kBlockerCentre, kBlockerRadius, scene->appearance(matte, kBlockerColor, 1.0f));
  }
  if (variant.translucent) {
    scene->addSphere(kGlassCentre, kGlassRadius, scene->appearance(matte, kGlassColor, kHalfOpacity));
  }
  scene->addSphere(kMirrorCentre, kMirrorRadius,
                   scene->appearance(variant.mirror ? mirror : matte, kMirrorColor, 1.0f));
  return scene;
}

// The six polygons of tests/rib/cornellbox.rib, in camera space, lit by its
// point light; indirect names the radiosity pass or leaves it unnamed.
std::unique_ptr<TracedScene> boxScene(bool indirect) {
  auto scene =
      std::make_unique<TracedScene>(kBoxSize, kSinglePixelSample, kBoxFov, kBoxNear, kBoxFar, kBoxLight, kBoxIntensity);
  if (indirect) {
    scene->options.setIndirectPass(kRadiosityPass);
    scene->options.setRadiosityElementSize(kRadiosityElementSize);
  }
  auto const matte = loadShader("matte", matteParams(kBoxKa, kBoxKd));

  scene->addPolygon(kBoxLeft, scene->appearance(matte, kRedWall, 1.0f));
  scene->addPolygon(kBoxRight, scene->appearance(matte, kGreenWall, 1.0f));
  for (std::array<GMANPoint, 4> const& wall : kBoxGrey) {
    scene->addPolygon(wall, scene->appearance(matte, kGreyWall, 1.0f));
  }
  return scene;
}

// A TracedScene with one disk emitter: radiance is its area light's colour, and
// pointLit says whether the point light also lights the scene's surfaces. The
// scene asks for kEmitterSamples draws per shading point.
struct EmitterScene : TracedScene {
  GMANLight const emitted;
  bool const pointLit;

  EmitterScene(GMANColor const& radiance, bool pointLit, RtFloat pointIntensity)
      : TracedScene(kEmitterSize, kDirectPixelSamples, kDirectFov, kDirectNear, kDirectFar, kDirectLight,
                    pointIntensity),
        emitted(GMAN_LIGHT_AREA, radiance, GMANPoint(), GMANVector()), pointLit(pointLit) {
    options.setNamedOption("raytracer", "arealightsamples", std::vector<RtInt>{kEmitterSamples});
  }

  gman::Appearance surface(std::shared_ptr<GMANSurfaceShader const> shader, GMANColor const& color) const {
    gman::Appearance look = appearance(std::move(shader), color, 1.0f);
    if (!pointLit) {
      look.lights.clear();
    }
    return look;
  }

  void addDisk(GMANPoint const& centre, RtFloat yawDegrees, bool emitting) {
    GMANMatrix4 place;
    place.rot(GMANRadians(yawDegrees), 0.0f, 1.0f, 0.0f);
    place.trans(centre.getX(), centre.getY(), centre.getZ());
    GMANTransform const transform = makeTransform(place);
    auto disk = gman::make<GMANRayDisk>(0.0f, kEmitterRadius, 360.0f, GMANParameterList(), transform);
    gman::Appearance look = surface(loadShader("matte", matteParams(1.0f, 1.0f)), kEmitterBlack);
    if (emitting) {
      look.areaLight = &emitted;
    }
    disk->setAppearance(look);
    renderer.getWorldManager()->add(std::move(disk));
  }
};

// A matte wall lit by a disk emitter that faces it, shadowed by a sphere
// between them; emitting false leaves the disk's area light off and blocker
// false leaves the sphere out. Nothing else lights the wall.
std::unique_ptr<EmitterScene> shadowedWallScene(bool emitting, bool blocker) {
  auto scene = std::make_unique<EmitterScene>(GMANColor(kEmitterRadianceValue), false, kEmitterDark);
  auto const matte = loadShader("matte", matteParams(1.0f, 1.0f));
  scene->addPolygon(kWall, scene->surface(matte, kWallColor));
  scene->addDisk(kWallEmitterCentre, kEmitterFacesWall, emitting);
  if (blocker) {
    scene->addSphere(kEmitterBlockerCentre, kEmitterBlockerRadius, scene->surface(matte, kBlockerColor));
  }
  return scene;
}

// The direct scene's floor and wall under its point light, with a disk
// facing the camera in view whose area light has the given radiance; emitting
// false leaves the area light off and disk false leaves the disk out.
std::unique_ptr<EmitterScene> viewedEmitterScene(GMANColor const& radiance, bool emitting, bool disk) {
  auto scene = std::make_unique<EmitterScene>(radiance, true, kDirectIntensity);
  auto const matte = loadShader("matte", matteParams(1.0f, 1.0f));
  scene->addPolygon(kFloor, scene->surface(matte, kFloorColor));
  scene->addPolygon(kWall, scene->surface(matte, kWallColor));
  if (disk) {
    scene->addDisk(kViewedEmitterCentre, kEmitterFacesCamera, emitting);
  }
  return scene;
}

struct Snapshot {
  static constexpr std::size_t kChannels = 7;
  std::vector<std::array<RtFloat, kChannels>> pixels;
  std::vector<RtInt> workers;
};

Snapshot snapshotOf(TracedScene const& scene, FrameSize const& size) {
  Snapshot snapshot;
  for (RtInt y = 0; y < size.height; ++y) {
    for (RtInt x = 0; x < size.width; ++x) {
      GMANColor const color = scene.frameBuffer.getPixel(x, y);
      GMANAlpha const alpha = scene.frameBuffer.getAlpha(x, y);
      snapshot.pixels.push_back({color.getRed(), color.getGreen(), color.getBlue(), alpha.getRed(), alpha.getGreen(),
                                 alpha.getBlue(), scene.renderer.getDepth(x, y)});
    }
  }
  snapshot.workers = scene.renderer.rowWorkers();
  return snapshot;
}

Snapshot renderAt(TracedScene& scene, FrameSize const& size, RtInt workers) {
  scene.render(workers);
  return snapshotOf(scene, size);
}

constexpr std::size_t kDepthChannel = 6;

std::size_t differingPixels(Snapshot const& a, Snapshot const& b) {
  std::size_t differing = 0;
  for (std::size_t i = 0; i < a.pixels.size(); ++i) {
    if (a.pixels[i] != b.pixels[i]) {
      ++differing;
    }
  }
  return differing;
}

// Pixels whose nearest surface is the same in both snapshots and whose
// colour differs: what a change to the light reaching that surface did.
std::size_t relitPixels(Snapshot const& a, Snapshot const& b) {
  std::size_t relit = 0;
  for (std::size_t i = 0; i < a.pixels.size(); ++i) {
    if (a.pixels[i][kDepthChannel] == b.pixels[i][kDepthChannel] && a.pixels[i] != b.pixels[i]) {
      ++relit;
    }
  }
  return relit;
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

std::set<RtInt> distinctWorkerIds(std::vector<RtInt> const& workers) {
  return std::set<RtInt>(workers.begin(), workers.end());
}

std::string idList(std::set<RtInt> const& ids) {
  std::string list;
  for (RtInt id : ids) {
    list += (list.empty() ? "" : ",") + std::to_string(id);
  }
  return "{" + list + "}";
}

void checkWorkerIds(Snapshot const& snapshot, FrameSize const& size, RtInt workers, std::string const& scene) {
  std::string const label = "B: the " + scene + " scene at " + std::to_string(workers) + " workers";
  check(snapshot.workers.size() == static_cast<std::size_t>(size.height), label + " reports one worker id per row");
  if (workers == kSerialWorkers) {
    check(everyWorkerIdIs(snapshot.workers, 0), label + " runs every row as worker 0");
    return;
  }
  check(workerIdsInRange(snapshot.workers, workers), label + " runs every row as a worker id in [0, n)");
  std::printf("note: %s ran rows on workers %s\n", label.c_str(), idList(distinctWorkerIds(snapshot.workers)).c_str());
}

void checkMatchesSerial(Snapshot const& serial, Snapshot const& other, RtInt workers, std::string const& scene) {
  std::size_t const differing = differingPixels(serial, other);
  check(differing == 0, "A: colour, alpha and depth of the " + scene + " scene match between 1 and " +
                            std::to_string(workers) + " workers (" + std::to_string(differing) + " of " +
                            std::to_string(serial.pixels.size()) + " pixels differ)");
}

void checkDirectScene() {
  Snapshot const serial = renderAt(*directScene(kDirectSize, kDirectPixelSamples, {}), kDirectSize, kSerialWorkers);
  Snapshot const noBlocker =
      renderAt(*directScene(kDirectSize, kDirectPixelSamples, {false, true, true}), kDirectSize, kSerialWorkers);
  Snapshot const noGlass =
      renderAt(*directScene(kDirectSize, kDirectPixelSamples, {true, false, true}), kDirectSize, kSerialWorkers);
  Snapshot const matteMirror =
      renderAt(*directScene(kDirectSize, kDirectPixelSamples, {true, true, false}), kDirectSize, kSerialWorkers);
  check(relitPixels(serial, noBlocker) > 0, "A setup: the direct scene holds shadowed pixels");
  check(differingPixels(serial, noGlass) > 0, "A setup: the direct scene holds composited pixels");
  check(differingPixels(serial, matteMirror) > 0, "A setup: the direct scene holds reflected pixels");

  checkWorkerIds(serial, kDirectSize, kSerialWorkers, "direct");
  for (RtInt workers : {kThreeWorkers, kParallelWorkers}) {
    Snapshot const other = renderAt(*directScene(kDirectSize, kDirectPixelSamples, {}), kDirectSize, workers);
    checkMatchesSerial(serial, other, workers, "direct");
    checkWorkerIds(other, kDirectSize, workers, "direct");
  }
}

void checkIndirectScene() {
  Snapshot const serial = renderAt(*boxScene(true), kBoxSize, kSerialWorkers);
  Snapshot const direct = renderAt(*boxScene(false), kBoxSize, kSerialWorkers);
  check(differingPixels(serial, direct) > 0, "A setup: the radiosity pass loaded and changed the box's image");

  checkWorkerIds(serial, kBoxSize, kSerialWorkers, "indirect");
  for (RtInt workers : {kThreeWorkers, kParallelWorkers}) {
    Snapshot const other = renderAt(*boxScene(true), kBoxSize, workers);
    checkMatchesSerial(serial, other, workers, "indirect");
    checkWorkerIds(other, kBoxSize, workers, "indirect");
  }
}

gman::Appearance paintedPlasticAppearance(TracedScene const& scene, std::string const& textureName) {
  GMANAttributes attributes;
  attributes.setSurface(RI_PAINTEDPLASTIC, textureNameParam(textureName));
  gman::Appearance look = gman::appearanceOf(attributes);
  look.lights = {&scene.light};
  return look;
}

// A quad filling the frame, painted plastic naming a texture no file
// carries: every camera ray shades through the worker's texture cache.
std::unique_ptr<TracedScene> missingTextureScene() {
  auto scene = std::make_unique<TracedScene>(kTextureSize, kSinglePixelSample, kTextureFov, kTextureNear, kTextureFar,
                                             kDirectLight, kDirectIntensity);
  std::array<GMANPoint, 4> const quad = {GMANPoint(-kQuadHalfExtent, -kQuadHalfExtent, kQuadDepth),
                                         GMANPoint(-kQuadHalfExtent, kQuadHalfExtent, kQuadDepth),
                                         GMANPoint(kQuadHalfExtent, kQuadHalfExtent, kQuadDepth),
                                         GMANPoint(kQuadHalfExtent, -kQuadHalfExtent, kQuadDepth)};
  scene->addPolygon(quad, paintedPlasticAppearance(*scene, kMissingTexture));
  return scene;
}

struct MissingTextureRun {
  int warnings;
  std::size_t workersThatRan;
};

MissingTextureRun missingTextureRun(RtInt workers, char const* logPath) {
  beginLogCapture(logPath);
  auto const scene = missingTextureScene();
  scene->render(workers);
  return {countOccurrences(endLogCapture(logPath), kMissingTexture),
          distinctWorkerIds(scene->renderer.rowWorkers()).size()};
}

void checkMissingTextureWarnsOnce() {
  std::ifstream const existing(kMissingTexture);
  check(!existing.good(), "C setup: no file carries the missing texture's name");
  check(gman::parallelWorkers(kTextureSize.height, kParallelWorkers) == kParallelWorkers,
        "C setup: the frame is tall enough for 8 workers");

  MissingTextureRun const serial = missingTextureRun(kSerialWorkers, kSerialLog);
  check(serial.warnings == 1,
        "C: the 1-worker render warns once about the missing texture (" + std::to_string(serial.warnings) + ")");
  MissingTextureRun const parallel = missingTextureRun(kParallelWorkers, kParallelLog);
  check(parallel.workersThatRan >= 1, "C: the 8-worker render ran its rows on at least one worker");
  check(parallel.warnings == 1, "C: the 8-worker render warns once however many workers ran (" +
                                    std::to_string(parallel.warnings) + " from " +
                                    std::to_string(parallel.workersThatRan) + " workers)");
}

enum class Mirrored { both, aMatte, bMatte };

// The camera sees mirror A, mirror A sees mirror B, and only mirror B sees
// the textured quad: the depth-1 tracer shades it.
std::unique_ptr<TracedScene> doubleReflectionScene(Mirrored which) {
  auto scene = std::make_unique<TracedScene>(kReflectionSize, kSinglePixelSample, kTextureFov, kTextureNear,
                                             kTextureFar, kReflectionLight, kReflectionIntensity);
  auto const matte = loadShader("matte", matteParams(1.0f, 1.0f));
  auto const mirror = loadShader("mirror", GMANParameterList());
  scene->addPolygon(kMirrorA, scene->appearance(which == Mirrored::aMatte ? matte : mirror, kOpaque, 1.0f));
  scene->addPolygon(kMirrorB, scene->appearance(which == Mirrored::bMatte ? matte : mirror, kOpaque, 1.0f));
  scene->addPolygon(kQuadQ, paintedPlasticAppearance(*scene, kReflectedTexture));
  return scene;
}

int reflectedTextureWarnings(Mirrored which, char const* logPath) {
  beginLogCapture(logPath);
  doubleReflectionScene(which)->render(kSerialWorkers);
  return countOccurrences(endLogCapture(logPath), kReflectedTexture);
}

void checkSecondReflectionCache() {
  std::ifstream const existing(kReflectedTexture);
  check(!existing.good(), "D setup: no file carries the reflected texture's name");

  int const bMatte = reflectedTextureWarnings(Mirrored::bMatte, kReflectedLogs[2]);
  int const aMatte = reflectedTextureWarnings(Mirrored::aMatte, kReflectedLogs[3]);
  check(bMatte == 0, "D setup: with mirror B matte the quad's texture is unsampled (" + std::to_string(bMatte) + ")");
  check(aMatte == 0, "D setup: with mirror A matte the quad's texture is unsampled (" + std::to_string(aMatte) + ")");

  int const first = reflectedTextureWarnings(Mirrored::both, kReflectedLogs[0]);
  int const second = reflectedTextureWarnings(Mirrored::both, kReflectedLogs[1]);
  check(first == 1, "D: the first render warns once about a texture only a second reflection reaches (" +
                        std::to_string(first) + ")");
  check(second == 1, "D: the second render warns once again, each render sampling through its own caches (" +
                         std::to_string(second) + ")");
}

// A matte quad filling the frame, lit by a point light beside the camera,
// and a paintedplastic card whose opacity map no file carries. The card
// stands between the light and the quad, outside the camera's view, so only
// a shadow ray reaches it; raised clear of the shadow rays, nothing does.
std::unique_ptr<TracedScene> cardShadowScene(bool cardOnShadowPath) {
  auto scene = std::make_unique<TracedScene>(kTextureSize, kSinglePixelSample, kTextureFov, kTextureNear, kTextureFar,
                                             kCardLight, kDirectIntensity);
  std::array<GMANPoint, 4> const quad = {GMANPoint(-kQuadHalfExtent, -kQuadHalfExtent, kQuadDepth),
                                         GMANPoint(-kQuadHalfExtent, kQuadHalfExtent, kQuadDepth),
                                         GMANPoint(kQuadHalfExtent, kQuadHalfExtent, kQuadDepth),
                                         GMANPoint(kQuadHalfExtent, -kQuadHalfExtent, kQuadDepth)};
  scene->addPolygon(quad, scene->appearance(loadShader("matte", matteParams(1.0f, 1.0f)), kFloorColor, 1.0f));

  RtFloat const rise = cardOnShadowPath ? 0.0f : kCardOffPathRise;
  std::array<GMANPoint, 4> const card = {GMANPoint(kCardOuterX, rise - kCardHalfHeight, kCardDepth),
                                         GMANPoint(kCardOuterX, rise + kCardHalfHeight, kCardDepth),
                                         GMANPoint(kCardInnerX, rise + kCardHalfHeight, kCardDepth),
                                         GMANPoint(kCardInnerX, rise - kCardHalfHeight, kCardDepth)};
  GMANAttributes attributes;
  attributes.setSurface(RI_PAINTEDPLASTIC, opacityNameParam(kMissingOpacityMap));
  gman::Appearance look = gman::appearanceOf(attributes);
  look.lights = {&scene->light};
  scene->addPolygon(card, look);
  return scene;
}

int cardShadowWarnings(bool cardOnShadowPath, char const* logPath) {
  beginLogCapture(logPath);
  cardShadowScene(cardOnShadowPath)->render(kSerialWorkers);
  return countOccurrences(endLogCapture(logPath), kMissingOpacityMap);
}

void checkShadowOnlyOpacityMapCache() {
  std::ifstream const existing(kMissingOpacityMap);
  check(!existing.good(), "E setup: no file carries the card's opacity map's name");

  int const offPath = cardShadowWarnings(false, kCardShadowLogs[0]);
  check(offPath == 0,
        "E setup: with the card off the shadow path its opacity map is unsampled (" + std::to_string(offPath) + ")");

  int const first = cardShadowWarnings(true, kCardShadowLogs[1]);
  int const second = cardShadowWarnings(true, kCardShadowLogs[2]);
  check(first == 1, "E: the first render warns once about an opacity map only a shadow ray reaches (" +
                        std::to_string(first) + ")");
  check(second == 1, "E: the second render warns once again, each render's shading occluders sampling through its "
                     "own caches (" +
                         std::to_string(second) + ")");
}

void checkAreaLightWorkers() {
  Snapshot const serial = renderAt(*shadowedWallScene(true, true), kEmitterSize, kSerialWorkers);
  Snapshot const dark = renderAt(*shadowedWallScene(false, true), kEmitterSize, kSerialWorkers);
  Snapshot const unshadowed = renderAt(*shadowedWallScene(true, false), kEmitterSize, kSerialWorkers);
  check(differingPixels(serial, dark) > 0, "F setup: the disk's area light lights the wall");
  check(relitPixels(serial, unshadowed) > 0, "F setup: the sphere shadows the wall from the disk");

  for (RtInt workers : {kThreeWorkers, kParallelWorkers}) {
    Snapshot const other = renderAt(*shadowedWallScene(true, true), kEmitterSize, workers);
    std::size_t const differing = differingPixels(serial, other);
    check(differing == 0, "F: colour, alpha and depth of the area-lit wall match between 1 and " +
                              std::to_string(workers) + " workers (" + std::to_string(differing) + " of " +
                              std::to_string(serial.pixels.size()) + " pixels differ)");
  }
}

bool everyChannelFinite(Snapshot const& snapshot) {
  for (std::array<RtFloat, Snapshot::kChannels> const& pixel : snapshot.pixels) {
    for (std::size_t channel = 0; channel < kDepthChannel; ++channel) {
      if (!std::isfinite(pixel[channel])) {
        return false;
      }
    }
  }
  return true;
}

void checkNonFiniteEmitter() {
  Snapshot const withNaN = renderAt(*viewedEmitterScene(kNaNRadiance, true, true), kEmitterSize, kSerialWorkers);
  Snapshot const removed = renderAt(*viewedEmitterScene(kNaNRadiance, false, true), kEmitterSize, kSerialWorkers);
  Snapshot const noDisk = renderAt(*viewedEmitterScene(kNaNRadiance, false, false), kEmitterSize, kSerialWorkers);
  check(differingPixels(removed, noDisk) > 0, "G setup: the disk is in view");

  std::size_t const differing = differingPixels(withNaN, removed);
  check(differing == 0, "G: colour, alpha and depth match the scene without the disk's area light (" +
                            std::to_string(differing) + " of " + std::to_string(withNaN.pixels.size()) +
                            " pixels differ)");
  check(everyChannelFinite(withNaN), "G: every channel of every pixel is finite");
}

int runChecks() {
  checkDirectScene();
  checkIndirectScene();
  checkMissingTextureWarnsOnce();
  checkSecondReflectionCache();
  checkShadowOnlyOpacityMapCache();
  checkAreaLightWorkers();
  checkNonFiniteEmitter();

  return checkSummary("the ray tracer's rows render through gman::parallelFor: the image, its worker ids and its "
                      "missing-texture warning hold at every worker count, a texture reached through a second "
                      "reflection or a shadow ray samples through the cache of the worker that cast it, area-light "
                      "draws hold at every worker count and a non-finite emitter adds nothing");
}

double timedRenderSeconds(RtInt workers) {
  auto const scene = directScene(kScalingSize, kScalingPixelSamples, {});
  scene->render(workers);
  auto const start = std::chrono::steady_clock::now();
  scene->render(workers);
  return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

int runScaling() {
  double const serialSeconds = timedRenderSeconds(kSerialWorkers);
  double const parallelSeconds = timedRenderSeconds(kParallelWorkers);
  std::printf("scaling: %d x %d at %d x %d samples, 1 worker %.3f s, %d workers %.3f s, ratio %.2f, "
              "hardware default %d workers\n",
              kScalingSize.width, kScalingSize.height, static_cast<int>(kScalingPixelSamples),
              static_cast<int>(kScalingPixelSamples), serialSeconds, kParallelWorkers, parallelSeconds,
              serialSeconds / parallelSeconds, gman::parallelWorkers(kScalingSize.height, 0));
  return 0;
}

} // namespace

int main(int argc, char** argv) {
  if (argc > 1 && std::strcmp(argv[1], "--scaling") == 0) {
    return runScaling();
  }
  return runChecks();
}
