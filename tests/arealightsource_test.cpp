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
 * AreaLightSource parses through GMANRIBParse and builds a GMAN_LIGHT_AREA
 * light; RiIlluminate, AttributeBegin/AttributeEnd and an unrecognized
 * shader name scope and gate GMANAttributes' own pending area light exactly
 * as the equivalent built-in-light mechanics already do.
 *
 * GMANRIBParse::parseSphere dispatches straight to
 * GMANRenderManImpl::RiSphereV's counts-aware overload rather than through
 * GMANRenderMan's virtual interface (see its own comment, "mirrors
 * parseGeneralPolygon's"), so a GMANRenderManImpl subclass cannot observe a
 * Sphere request's own attribute state by overriding RiSphereV. The
 * attribute-scoping checks below drive GMANRenderManImpl's Ri* members
 * directly instead, reading GMANAttributes' own pending-area-light state
 * (RtLightHandle plus current illumination) at exactly the moments a Sphere
 * declared there would see -- the same combined condition that will resolve
 * a declared primitive's Appearance::areaLight.
 */

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "check.h"
#include "gmanlightsourcemgr.h"
#include "gmanlog.h"
#include "gmanrendermanimpl.h"
#include "gmanribparse.h"
#include "gmanshading.h"
#include "ri.h"

namespace {

constexpr RtFloat kIntensity = 10.0f;

// Exposes GMANGraphicState::getAttributes, protected through
// GMANRenderManImpl, and captures the handle each RiAreaLightSourceV call
// returns -- reachable through GMANRIBParse::parseAreaLightSource's own
// virtual call, unlike RiSphereV's.
class TestRenderMan : public GMANRenderManImpl {
public:
  RtLightHandle lastAreaLightHandle = 0;

  GMANAttributes& attributes() { return getAttributes(); }

  RtLightHandle RiAreaLightSourceV(RtToken name, RtInt n, RtToken tokens[], RtPointer parms[]) override {
    lastAreaLightHandle = GMANRenderManImpl::RiAreaLightSourceV(name, n, tokens, parms);
    return lastAreaLightHandle;
  }
};

// True when a primitive declared under attr would carry a non-null
// Appearance::areaLight, through the exported production function itself.
bool wouldTagPrimitive(GMANAttributes const& attr) { return gman::appearanceOf(attr).areaLight != nullptr; }

// Ends a minimal, otherwise-untouched world so RiEnd leaves no renderer or
// pending parameter buffers behind; no test here inspects the frame this
// writes.
void closeWorld(TestRenderMan& renderMan) {
  renderMan.RiWorldEnd();
  renderMan.RiEnd();
}

void openWorld(TestRenderMan& renderMan, char* displayName) {
  renderMan.RiBegin("gmanzbuffer");
  renderMan.RiFormat(4, 4, 1.0f);
  renderMan.RiDisplayV(displayName, RI_FILE, RI_RGB, 0, nullptr, nullptr);
  renderMan.RiProjectionV(RI_ORTHOGRAPHIC, 0, nullptr, nullptr);
  renderMan.RiWorldBegin();
}

// Builds "float intensity" [kIntensity] "color lightcolor" [1 1 1], the
// fixture's own arealight parameters.
void areaLightParams(RtToken tokens[2], RtPointer parms[2], RtFloat& intensity, RtFloat color[3]) {
  intensity = kIntensity;
  color[0] = color[1] = color[2] = 1.0f;
  tokens[0] = RI_INTENSITY;
  tokens[1] = RI_LIGHTCOLOR;
  parms[0] = (RtPointer)&intensity;
  parms[1] = (RtPointer)color;
}

// The "builds and parses" half of the fixture requirement:
// tests/rib/arealight_sphere.rib parses cleanly through GMANRIBParse and its
// AreaLightSource request builds one GMAN_LIGHT_AREA light whose cl is
// (10, 10, 10).
void testFixtureBuildsAreaLight(std::string const& ribDir) {
  TestRenderMan renderMan;
  std::string const rib = ribDir + "/arealight_sphere.rib";
  {
    GMANRIBParse parser(renderMan, rib.c_str());
    parser.parse();
  }

  check(renderMan.lastAreaLightHandle != 0, "fixture: AreaLightSource returns a non-null handle");
  GMANLight const* light = gmanLightSourceMgr().get(renderMan.lastAreaLightHandle);
  check(light != nullptr, "fixture: the handle resolves to a registered light");
  if (light != nullptr) {
    check(light->getType() == GMAN_LIGHT_AREA, "fixture: the light's type is GMAN_LIGHT_AREA");
    GMANColor const& cl = light->getCl();
    check(cl.getRed() == 10.0f && cl.getGreen() == 10.0f && cl.getBlue() == 10.0f,
          "fixture: the light's cl is (10, 10, 10)");
  }
}

// Surface/Color and a nested block do not clear the pending area light;
// the matching AttributeEnd does.
void testAttributeScoping() {
  TestRenderMan renderMan;
  char displayName[] = "arealightsource_scoping.tif";
  openWorld(renderMan, displayName);

  renderMan.RiAttributeBegin();

  RtToken tokens[2];
  RtPointer parms[2];
  RtFloat intensity, color[3];
  areaLightParams(tokens, parms, intensity, color);
  RtLightHandle const handle = renderMan.RiAreaLightSourceV("arealight", 2, tokens, parms);
  check(handle != 0, "scoping: a recognized arealight name returns a non-null handle");
  check(wouldTagPrimitive(renderMan.attributes()), "scoping: a primitive declared here would carry the area light");

  // RiSurface/RiColor between AreaLightSource and the primitive leave the
  // pending area light untouched.
  renderMan.RiSurfaceV("matte", 0, nullptr, nullptr);
  RtColor const grey = {0.5f, 0.5f, 0.5f};
  renderMan.RiColor(const_cast<RtFloat*>(grey));
  check(wouldTagPrimitive(renderMan.attributes()),
        "scoping: RiSurface/RiColor between AreaLightSource and a primitive do not clear it");

  // A nested AttributeBegin/AttributeEnd, entered and exited before the
  // primitive, does not clear the outer frame's pending area light -- the
  // inner frame is its own copy, discarded on its own AttributeEnd.
  renderMan.RiAttributeBegin();
  check(wouldTagPrimitive(renderMan.attributes()), "scoping: a nested block inherits the pending area light");
  renderMan.RiAttributeEnd();
  check(wouldTagPrimitive(renderMan.attributes()), "scoping: exiting the nested block leaves the outer one tagged");

  // The matching AttributeEnd clears it -- the pending area light lived
  // only on the frame that popped.
  renderMan.RiAttributeEnd();
  check(!wouldTagPrimitive(renderMan.attributes()), "scoping: AttributeEnd clears the pending area light");

  closeWorld(renderMan);
}

// Illuminate <seq> 0 immediately after AreaLightSource leaves the
// handle pending but not illuminated, so a primitive declared there would
// not be tagged.
void testIlluminateOffLeavesUntagged() {
  TestRenderMan renderMan;
  char displayName[] = "arealightsource_illuminate.tif";
  openWorld(renderMan, displayName);

  renderMan.RiAttributeBegin();

  RtToken tokens[2];
  RtPointer parms[2];
  RtFloat intensity, color[3];
  areaLightParams(tokens, parms, intensity, color);
  RtLightHandle const handle = renderMan.RiAreaLightSourceV("arealight", 2, tokens, parms);
  check(wouldTagPrimitive(renderMan.attributes()), "illuminate: on declaration, a primitive would be tagged");

  renderMan.RiIlluminate(handle, RI_FALSE);
  check(renderMan.attributes().getAreaLight() == handle, "illuminate: the handle stays pending after Illuminate 0");
  check(!wouldTagPrimitive(renderMan.attributes()), "illuminate: Illuminate 0 leaves a primitive untagged");

  renderMan.RiAttributeEnd();
  closeWorld(renderMan);
}

// An unrecognized area light shader name logs one warning naming it,
// returns a null handle, and leaves a previously active area light, built
// by hand, current.
void testUnknownNameLeavesPriorLightCurrent() {
  std::string const logPath = "arealightsource_unknown.log";
  std::remove(logPath.c_str());
  setLogFile(logPath.c_str());
  setScreenOutput(false);

  TestRenderMan renderMan;
  char displayName[] = "arealightsource_unknown.tif";
  openWorld(renderMan, displayName);
  renderMan.RiAttributeBegin();

  GMANLight* priorLight = new GMANLight(GMAN_LIGHT_AREA, GMANColor(5.0f, 5.0f, 5.0f), GMANPoint(), GMANVector());
  RtLightHandle const priorHandle = gmanLightSourceMgr().add(priorLight);
  renderMan.attributes().setIlluminate(priorHandle, RI_TRUE);
  renderMan.attributes().setAreaLight(priorHandle);

  RtLightHandle const badHandle = renderMan.RiAreaLightSourceV("nosuchlight", 0, nullptr, nullptr);
  check(badHandle == 0, "unknown name: returns a null handle");
  check(renderMan.attributes().getAreaLight() == priorHandle, "unknown name: the prior area light stays current");

  renderMan.RiAttributeEnd();
  closeWorld(renderMan);

  setLogFile("/dev/null");
  setScreenOutput(true);

  std::ifstream in(logPath, std::ios::binary);
  std::ostringstream contents;
  contents << in.rdbuf();
  std::string const log = contents.str();
  check(log.find("nosuchlight") != std::string::npos, "unknown name: the warning names the unrecognized shader");
}

} // namespace

int main(int argc, char* argv[]) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: %s <tests/rib dir>\n", argv[0]);
    return 2;
  }
  std::string const ribDir = argv[1];

  testFixtureBuildsAreaLight(ribDir);
  testAttributeScoping();
  testIlluminateOffLeavesUntagged();
  testUnknownNameLeavesPriorLightCurrent();

  return checkSummary("AreaLightSource parses and its pending area light scopes like Illuminate's own light list");
}
