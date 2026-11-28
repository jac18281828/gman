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
 * A world owns the primitives it is given. A GMANWorld destroys
 * each primitive added to it when the world itself goes, and an empty
 * gman::Owned adds nothing. Each factory returns an empty owner for a mesh
 * with no face, and a request that yields no geometry adds nothing to the
 * world: a render's radiosity pass counts no primitive it could not dice. A
 * NuPatch the ray factory drops warns once per world. The light manager owns
 * its lights the same way, and an empty owner stores no light.
 */

#include <cstddef>
#include <cstdio>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "check.h"
#include "gmanattributes.h"
#include "gmancolor.h"
#include "gmandictionary.h"
#include "gmanlightsourcemgr.h"
#include "gmanlog.h"
#include "gmanoptions.h"
#include "gmanowned.h"
#include "gmanpoint.h"
#include "gmanpolygonmesh.h"
#include "gmanpolygonmeshfactory.h"
#include "gmanprimitive.h"
#include "gmanrasterprimitivefactory.h"
#include "gmanrayprimitivefactory.h"
#include "gmanrendermanimpl.h"
#include "gmantransform.h"
#include "gmanvector.h"
#include "gmanworld.h"
#include "ri.h"

namespace {

int liveCount = 0;

class CountedPrimitive : public GMANPrimitive {
public:
  CountedPrimitive() { ++liveCount; }
  CountedPrimitive(CountedPrimitive const&) = delete;
  CountedPrimitive& operator=(CountedPrimitive const&) = delete;
  ~CountedPrimitive() override { --liveCount; }
};

std::vector<GMANPrimitive const*> walk(GMANWorld const& world) {
  std::vector<GMANPrimitive const*> walked;
  for (GMANPrimitive const& primitive : world) {
    walked.push_back(&primitive);
  }
  return walked;
}

void testWorldDestroysWhatItOwns() {
  std::vector<GMANPrimitive const*> expected;
  std::vector<GMANPrimitive const*> walked;
  int liveWithinScope = 0;
  {
    GMANWorld world;
    for (int i = 0; i < 3; ++i) {
      auto primitive = gman::make<CountedPrimitive>();
      expected.push_back(primitive.get());
      world.add(std::move(primitive));
    }
    liveWithinScope = liveCount;
    walked = walk(world);
  }
  check(liveWithinScope == 3, "the world holds the three primitives added to it");
  check(walked == expected, "the world walk yields the three primitives in the order added");
  check(liveCount == 0, "the world destroys its three primitives when it goes");
}

void testEmptyOwnerAddsNothing() {
  GMANWorld world;
  world.add(gman::Owned<GMANPrimitive>());
  auto primitive = gman::make<CountedPrimitive>();
  auto const* const added = static_cast<GMANPrimitive const*>(primitive.get());
  world.add(std::move(primitive));

  const auto walked = walk(world);
  check(walked.size() == 1 && walked[0] == added, "an empty owner adds nothing: the walk yields only the primitive");
}

// A mesh of four collinear points, which no face survives, handed to manager.
template <typename Manager> gman::Owned<GMANPrimitive> collinearMeshRequest(Manager& manager) {
  std::vector<RtFloat> p = {0, 0, 0, 1, 0, 0, 2, 0, 0, 3, 0, 0};
  GMANDictionary dictionary;
  RtToken tokens[1] = {RI_P};
  RtPointer parms[1] = {p.data()};
  const auto mesh = gman::polygonMesh(4, dictionary, 1, tokens, parms, nullptr);
  check(mesh.has_value(), "the factory builds a mesh of four collinear points");
  if (!mesh.has_value()) {
    return nullptr;
  }
  GMANOptions options;
  GMANAttributes attr;
  GMANTransform transform;
  return manager.getRSPolygonMesh(*mesh, options, attr, transform);
}

void testFactoriesReturnEmptyOwnerForNoFace() {
  GMANRayPrimitiveFactory rayManager;
  GMANRasterPrimitiveFactory rasterManager;
  auto rayPrimitive = collinearMeshRequest(rayManager);
  auto rasterPrimitive = collinearMeshRequest(rasterManager);
  check(rayPrimitive == nullptr, "the ray factory returns an empty owner for a mesh with no face");
  check(rasterPrimitive == nullptr, "the raster factory returns an empty owner for a mesh with no face");

  GMANWorld world;
  world.add(std::move(rayPrimitive));
  world.add(std::move(rasterPrimitive));
  auto counted = gman::make<CountedPrimitive>();
  auto const* const added = static_cast<GMANPrimitive const*>(counted.get());
  world.add(std::move(counted));
  check(walk(world) == std::vector<GMANPrimitive const*>{added},
        "a world handed each empty owner holds only the primitive added after them");
}

std::string readFile(std::string const& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream contents;
  contents << in.rdbuf();
  return contents.str();
}

template <typename Fn> std::string captureLog(std::string const& path, Fn&& fn) {
  std::remove(path.c_str());
  setLogFile(path.c_str());
  setScreenOutput(false);

  fn();

  setLogFile("/dev/null");
  setScreenOutput(true);
  return readFile(path);
}

void beginRayTracerScene(GMANRenderManImpl& renderMan, std::string displayName) {
  renderMan.RiBegin("gmanraytracer");
  renderMan.RiFormat(8, 8, 1.0f);
  renderMan.RiDisplayV(displayName.data(), RI_FILE, RI_RGBA, 0, nullptr, nullptr);
}

void renderFailedRequestScene() {
  GMANRenderManImpl renderMan;
  beginRayTracerScene(renderMan, "primitiveowners_failedrequest.tif");

  char radiosity[] = "radiosity";
  char* indirect = radiosity;
  RtToken optionTokens[1] = {"string indirect"};
  RtPointer optionParms[1] = {&indirect};
  renderMan.RiOptionV("render", 1, optionTokens, optionParms);
  renderMan.RiWorldBegin();

  RtFloat intensity = 20.0f;
  RtFloat from[3] = {2.0f, 2.0f, 2.0f};
  RtToken lightTokens[2] = {"intensity", "from"};
  RtPointer lightParms[2] = {&intensity, from};
  renderMan.RiLightSourceV("pointlight", 2, lightTokens, lightParms);

  RtFloat collinear[9] = {-1, -1, 0, 0, 0, 0, 1, 1, 0};
  RtToken polygonTokens[1] = {RI_P};
  RtPointer polygonParms[1] = {collinear};
  renderMan.RiPolygonV(3, 1, polygonTokens, polygonParms, nullptr);

  RtInt nverts[2] = {4, -1};
  RtFloat square[12] = {-1, -1, 0, 1, -1, 0, 1, 1, 0, -1, 1, 0};
  RtPointer generalParms[1] = {square};
  renderMan.RiGeneralPolygonV(2, nverts, 1, polygonTokens, generalParms, nullptr);

  renderMan.RiSphereV(1.0f, -1.0f, 1.0f, 360.0f, 0, nullptr, nullptr);
  renderMan.RiWorldEnd();
  renderMan.RiEnd();
}

void testFailedRequestAddsNothing() {
  const auto log = captureLog("primitiveowners_failedrequest.log", renderFailedRequestScene);
  check(log.find("GeneralPolygon: nverts[1] = -1 is negative; ignoring.") != std::string::npos,
        "a failed request warns: the log holds the negative-nverts message");
  check(log.find("could not be diced") == std::string::npos,
        "a failed request adds nothing: the radiosity pass skips no primitive");
}

void renderTwoNuPatchesScene() {
  GMANRenderManImpl renderMan;
  beginRayTracerScene(renderMan, "primitiveowners_nupatch.tif");
  renderMan.RiWorldBegin();

  const RtInt order = 2;
  RtFloat knots[4] = {0, 0, 1, 1};
  RtFloat controlPoints[12] = {-0.5f, -0.5f, 0.0f, 0.5f, -0.4f, 0.2f, -0.4f, 0.5f, -0.1f, 0.5f, 0.5f, 0.3f};
  RtToken tokens[1] = {RI_P};
  RtPointer parms[1] = {controlPoints};
  for (int request = 0; request < 2; ++request) {
    renderMan.RiNuPatchV(order, order, knots, 0, 1, order, order, knots, 0, 1, 1, tokens, parms, nullptr);
  }

  renderMan.RiWorldEnd();
  renderMan.RiEnd();
}

void testNuPatchWarnsOncePerWorld() {
  const auto log = captureLog("primitiveowners_nupatch.log", renderTwoNuPatchesScene);
  const std::string text = "NuPatch: the ray and path tracers cannot render a NuPatch; ignoring.";
  std::size_t count = 0;
  for (std::size_t at = log.find(text); at != std::string::npos; at = log.find(text, at + text.size())) {
    ++count;
  }
  check(count == 1, "a NuPatch the ray factory drops warns once per world: the text appears " + std::to_string(count) +
                        " times for two requests");
}

void testLightManagerOwnsItsLights() {
  auto& lights = gmanLightSourceMgr();
  const auto dropped = lights.add(gman::Owned<GMANLight>());
  check(dropped == (RtLightHandle)0, "an empty owner added to the light manager returns the null handle");
  check(lights.get(dropped) == nullptr, "the null handle resolves to no light");

  auto light = gman::make<GMANLight>(GMAN_LIGHT_POINT, GMANColor(1.0f, 1.0f, 1.0f), GMANPoint(), GMANVector());
  auto const* const added = light.get();
  const auto handle = lights.add(std::move(light));
  check(handle != (RtLightHandle)0 && lights.get(handle) == added,
        "a light built with gman::make resolves through the handle add returns");
}

} // namespace

int main() {
  testWorldDestroysWhatItOwns();
  testEmptyOwnerAddsNothing();
  testFactoriesReturnEmptyOwnerForNoFace();
  testFailedRequestAddsNothing();
  testNuPatchWarnsOncePerWorld();
  testLightManagerOwnsItsLights();

  return checkSummary("the world and the light manager own what they are given, a failed request adds nothing and a "
                      "dropped NuPatch warns once");
}
