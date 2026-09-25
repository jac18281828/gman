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
 * gman::TextureCache as an argument: independent caches proved directly,
 * GMANSurfaceEnv and gman::shade routing through whichever cache they are
 * given (or gman::textureCache()'s single process cache when given none),
 * and gman::parallelFor workers each decoding into their own cache with no
 * cross-worker hit.
 *
 * Every check turns on one contrast: a cache asked for a name before its
 * file exists holds opaque black for that name forever after, while a
 * cache asked only once the file exists decodes the checker -- red at its
 * (0.25, 0.25) texel centre, tests/checkertexture.h's own layout. A
 * missing name's warning is counted through setLogFile the way
 * tests/log_test.cpp reads its sink back, one name per check so counts
 * never cross checks.
 */

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "check.h"
#include "checkertexture.h"
#include "gmanattributes.h"
#include "gmancolor.h"
#include "gmandictionary.h"
#include "gmanlog.h"
#include "gmanparallel.h"
#include "gmanparameterlist.h"
#include "gmanshaderenvironment.h"
#include "gmanshading.h"
#include "gmantexture.h"
#include "ri.h"

namespace {

const RtFloat kSampleTol = (RtFloat)1.0e-4;

const GMANColor kRed((RtFloat)1.0, (RtFloat)0.0, (RtFloat)0.0);
const GMANColor kBlack((RtFloat)0.0, (RtFloat)0.0, (RtFloat)0.0);

bool colorNear(GMANColor const& got, GMANColor const& want, RtFloat tol) {
  return std::fabs(got.getRed() - want.getRed()) <= tol && std::fabs(got.getGreen() - want.getGreen()) <= tol &&
         std::fabs(got.getBlue() - want.getBlue()) <= tol;
}

std::string describe(GMANColor const& c) {
  return "(" + std::to_string(c.getRed()) + ", " + std::to_string(c.getGreen()) + ", " + std::to_string(c.getBlue()) +
         ")";
}

void checkColor(GMANColor const& got, GMANColor const& want, std::string const& what) {
  check(colorNear(got, want, kSampleTol), what + ": got " + describe(got) + ", want " + describe(want));
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

// A one-entry parameter list carrying "texturename", the way RiSurfaceV
// builds one for a single string parameter -- GMANParameterList's STRING
// case reads dt[i] as a char**, one element long here.
GMANParameterList textureNameParam(std::string const& name) {
  static GMANDictionary dictionary;
  RtToken tokens[1] = {RI_TEXTURENAME};
  char* strs[1] = {const_cast<char*>(name.c_str())};
  RtPointer parms[1] = {strs};
  return GMANParameterList(dictionary, 1, tokens, parms);
}

// A paintedplastic instance naming textureName, lit by one white ambient
// light: Ci = Cs * texture(s, t) * Ka * ambientColor, with Cs, Ka and
// ambientColor all white by default, so Ci equals the texture sample
// exactly -- black exactly when the sample is.
gman::Appearance paintedPlasticAppearance(std::string const& textureName) {
  GMANAttributes attr;
  attr.setSurface(RI_PAINTEDPLASTIC, textureNameParam(textureName));
  return gman::appearanceOf(attr);
}

gman::SurfacePoint texelCentrePoint() {
  gman::SurfacePoint point;
  point.P = GMANPoint(0.0f, 0.0f, 0.0f);
  point.N = GMANNormal(0.0f, 0.0f, 1.0f);
  point.Ng = GMANNormal(0.0f, 0.0f, 1.0f);
  point.I = GMANVector(0.0f, 0.0f, -1.0f);
  point.E = GMANPoint(0.0f, 0.0f, 5.0f);
  point.s = 0.25f;
  point.t = 0.25f;
  return point;
}

const GMANLight kAmbient(GMAN_LIGHT_AMBIENT, GMANColor(1.0f, 1.0f, 1.0f), GMANPoint(), GMANVector());

// ---- independent caches ----

void testIndependentCaches() {
  const std::string name = "texturecache_independent.tif";
  std::remove(name.c_str());

  gman::TextureCache cacheA;
  gman::TextureCache cacheB;

  checkColor(cacheA.sample(name, 0.25, 0.25, gman::TEXTURE_CLAMP), kBlack,
             "cache A's lookup before the file exists holds opaque black");

  check(writeCheckerTexture(name), name + " writes");

  checkColor(cacheB.sample(name, 0.25, 0.25, gman::TEXTURE_CLAMP), kRed,
             "cache B's first lookup after the file exists decodes the checker");
  checkColor(cacheA.sample(name, 0.25, 0.25, gman::TEXTURE_CLAMP), kBlack,
             "cache A still holds its own miss, taken before the file existed");
}

void testMissingNameWarnsOncePerCache() {
  const std::string logPath = "texturecache_independent_missing.log";
  std::remove(logPath.c_str());
  setLogFile(logPath.c_str());
  setScreenOutput(false);

  const std::string missingName = "texturecache_missing_1a2b3c.tif";
  gman::TextureCache cacheA;
  gman::TextureCache cacheB;
  cacheA.sample(missingName, 0.5, 0.5, gman::TEXTURE_CLAMP);
  cacheA.sample(missingName, 0.1, 0.1, gman::TEXTURE_CLAMP);
  cacheB.sample(missingName, 0.9, 0.9, gman::TEXTURE_CLAMP);
  cacheB.sample(missingName, 0.2, 0.2, gman::TEXTURE_CLAMP);

  check(countOccurrences(readFile(logPath), missingName) == 2,
        "a name no file carries, looked up several times through two caches, logs exactly two warnings");
}

// ---- the env routes ----

void testEnvRoutesTexture() {
  const std::string name = "texturecache_envtexture.tif";
  std::remove(name.c_str());

  gman::TextureCache cacheA;
  checkColor(cacheA.sample(name, 0.25, 0.25, gman::TEXTURE_CLAMP), kBlack, "cache A's own miss before the file exists");

  check(writeCheckerTexture(name), name + " writes");

  GMANSurfaceEnv envNamed;
  envNamed.textureCache = &cacheA;
  checkColor(envNamed.texture(name, 0.25, 0.25), kBlack, "texture() through a named cache holds that cache's own miss");

  GMANSurfaceEnv envDefault;
  checkColor(envDefault.texture(name, 0.25, 0.25), kRed,
             "texture() through a null cache reaches the process cache's fresh decode");
}

void testEnvRoutesEnvironment() {
  const std::string name = "texturecache_envenvironment.tif";
  std::remove(name.c_str());

  gman::TextureCache cacheA;
  checkColor(cacheA.sample(name, 0.25, 0.25, gman::TEXTURE_CLAMP), kBlack, "cache A's own miss before the file exists");

  check(writeCheckerTexture(name), name + " writes");

  // lat = 45 degrees, lon = 90 degrees: environment()'s own (s, t) =
  // (0.25, 0.25), the checker's top-left texel centre.
  const GMANVector r(0.0f, 1.0f, 1.0f);

  GMANSurfaceEnv envNamed;
  envNamed.textureCache = &cacheA;
  checkColor(envNamed.environment(name, r), kBlack, "environment() through a named cache holds that cache's own miss");

  GMANSurfaceEnv envDefault;
  checkColor(envDefault.environment(name, r), kRed,
             "environment() through a null cache reaches the process cache's fresh decode");
}

// ---- gman::shade forwards its cache ----

void testShadeForwardsCache() {
  const std::string name = "texturecache_shade.tif";
  std::remove(name.c_str());

  gman::TextureCache cache;
  checkColor(cache.sample(name, 0.25, 0.25, gman::TEXTURE_CLAMP), kBlack,
             "the shading cache's own miss before the file exists");

  check(writeCheckerTexture(name), name + " writes");

  gman::Appearance appearance = paintedPlasticAppearance(name);
  appearance.lights = {&kAmbient};
  gman::SurfacePoint const point = texelCentrePoint();
  GMANMatrix4 const cameraToWorld;

  gman::Shading const withCache = gman::shade(appearance, point, cameraToWorld, nullptr, nullptr, &cache);
  checkColor(withCache.Ci, kBlack, "Ci follows the cache passed to gman::shade, still holding its own miss");

  gman::Shading const withProcessCache = gman::shade(appearance, point, cameraToWorld);
  checkColor(withProcessCache.Ci, kRed, "Ci follows gman::textureCache() when the call passes none, decoding fresh");
}

// ---- one cache per worker ----

void testOneCachePerWorker() {
  const std::string name = "texturecache_worker.tif";
  std::remove(name.c_str());
  const std::string logPath = "texturecache_worker.log";
  std::remove(logPath.c_str());

  const RtInt count = 16;
  const RtInt requestedWorkers = 4;
  const RtInt numWorkers = gman::parallelWorkers(count, requestedWorkers);
  check(numWorkers == 4, "gman::parallelWorkers(16, 4) reports 4 workers");

  std::vector<gman::TextureCache> caches(static_cast<std::size_t>(numWorkers));

  gman::Appearance appearance = paintedPlasticAppearance(name);
  appearance.lights = {&kAmbient};
  gman::SurfacePoint const point = texelCentrePoint();
  GMANMatrix4 const cameraToWorld;

  setLogFile(logPath.c_str());
  setScreenOutput(false);

  std::vector<int> workerHits(static_cast<std::size_t>(numWorkers), 0);
  gman::parallelFor(
      count,
      [&](RtInt /*index*/, RtInt worker) {
        workerHits[static_cast<std::size_t>(worker)]++;
        gman::shade(appearance, point, cameraToWorld, nullptr, nullptr, &caches[static_cast<std::size_t>(worker)]);
      },
      requestedWorkers);

  bool everyWorkerRan = true;
  for (int hits : workerHits) {
    everyWorkerRan = everyWorkerRan && hits > 0;
  }
  check(everyWorkerRan, "worker indices 0, 1, 2 and 3 each ran at least one body");

  check(countOccurrences(readFile(logPath), name) == 4,
        "the log holds exactly four warnings naming the missing texture");

  check(writeCheckerTexture(name), name + " writes");

  for (RtInt w = 0; w < numWorkers; ++w) {
    checkColor(caches[static_cast<std::size_t>(w)].sample(name, 0.25, 0.25, gman::TEXTURE_CLAMP), kBlack,
               "worker " + std::to_string(w) + "'s cache still answers black once the file exists");
  }

  gman::TextureCache freshCache;
  checkColor(freshCache.sample(name, 0.25, 0.25, gman::TEXTURE_CLAMP), kRed,
             "a fresh cache decodes the checker once the file exists");
}

} // namespace

int main() {
  testIndependentCaches();
  testMissingNameWarnsOncePerCache();
  testEnvRoutesTexture();
  testEnvRoutesEnvironment();
  testShadeForwardsCache();
  testOneCachePerWorker();

  return checkSummary("gman::TextureCache holds as an argument: independent caches, GMANSurfaceEnv and gman::shade "
                      "routing, and one cache per parallelFor worker");
}
