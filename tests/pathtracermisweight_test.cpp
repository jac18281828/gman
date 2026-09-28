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
 * The path tracer's own multiple-importance-sampling weight computation,
 * called directly against hand-computed values, without rendering a
 * frame: next-event estimation's weight, an emitter hit's weight, the
 * light-choice probability the two share and the off switch. The three
 * functions under test carry external linkage in
 * renderers/pathtracer/gmanpathtracerenderer.cpp but no header declares
 * them; the prototypes below must match exactly.
 */

#include <cmath>
#include <cstddef>
#include <vector>

#include "check.h"
#include "gmanemitter.h"
#include "gmanlightsourcemgr.h"
#include "gmanlinearworldmanager.h"
#include "gmanmatrix4.h"
#include "gmanparameterlist.h"
#include "gmanpathtracerenderer.h"
#include "gmanpoint.h"
#include "gmanraysphere.h"
#include "gmantransform.h"
#include "maketransform.h"
#include "ri.h"

namespace gman {
RtFloat lightChoiceProbability(std::vector<gman::Emitter> const& emitters, GMANPoint const& p, std::size_t index);
RtFloat nextEventWeight(bool lightIsDelta, bool misEnabled, RtFloat pLight, RtFloat pBsdf);
RtFloat emitterHitWeight(bool rayEligibleForEmitterHit, bool misEnabled, RtFloat pBsdf, RtFloat pLight);
} // namespace gman

namespace {

constexpr double kPi = 3.14159265358979323846;

// C.1: no exemption at vertex 0 -- neither function takes a vertex index
// at all, so there is nowhere for a hidden "return 1 at vertex 0" to hide;
// C.3 below calls the identical emitter-hit case again, standing for a
// deeper vertex, and finds the same value.
void testNoExemptionAtFirstVertex() {
  RtFloat const nextEvent = gman::nextEventWeight(false, true, 3.0f, 2.0f);
  check(std::fabs(nextEvent - 9.0f / 13.0f) <= 1e-6f,
        "nextEventWeight: an area light's weight at pLight=3, pBsdf=2 is 9/13, not fixed at 1");

  RtFloat const emitterHit = gman::emitterHitWeight(false, true, 2.0f, 3.0f);
  check(std::fabs(emitterHit - 4.0f / 13.0f) <= 1e-6f,
        "emitterHitWeight: a non-delta departure's weight at pBsdf=2, pLight=3 is 4/13, not fixed at 1");
}

// C.2: the two true exemptions, independent of every pdf argument.
void testTrueExemptions() {
  check(gman::nextEventWeight(true, true, 3.0f, 2.0f) == 1.0f,
        "nextEventWeight: a delta light keeps weight 1 whatever pLight and pBsdf are");

  // rayEligibleForEmitterHit true is the renderer's own one boolean for
  // both true exemptions: no BSDF draw produced the ray at all (the
  // camera's own ray) and the most recent one was a delta lobe. Neither
  // case has a live next-event term to double, so both keep weight 1.
  check(gman::emitterHitWeight(true, true, 2.0f, 3.0f) == 1.0f,
        "emitterHitWeight: rayEligible true keeps weight 1 -- covers both the camera ray's own direct hit and a "
        "delta-lobe departure, whatever pBsdf and pLight are");
}

// C.3: the identical (pBsdf, pLight) pair, called again as if from a
// deeper vertex -- matching C.1's own value rules out a hidden
// vertex-0-only special case, since the function itself takes no vertex
// index to special-case.
void testSameWeightAtADeeperVertex() {
  RtFloat const deeper = gman::emitterHitWeight(false, true, 2.0f, 3.0f);
  check(std::fabs(deeper - 4.0f / 13.0f) <= 1e-6f,
        "emitterHitWeight: the same (pBsdf, pLight) pair gives the identical value at a deeper vertex");
}

// C.4: the light-choice probability, a hand-built world of one delta light
// and one area emitter, enumerated through gman::emitters as the renderer
// itself would.
void testLightChoiceProbabilityTwoLights() {
  GMANPoint const p(0.0f, 0.0f, 0.0f);

  GMANLinearWorldManager world;

  // The delta light: cl (1, 1, 1), at distance 1 from p, hosted on an
  // otherwise unrelated primitive, as gman::emitters requires.
  GMANRaySphere* host = new GMANRaySphere(1.0f, -1.0f, 1.0f, 360.0f, GMANParameterList());
  GMANLight const pointLight(GMAN_LIGHT_POINT, GMANColor(1.0f, 1.0f, 1.0f), GMANPoint(0.0f, 0.0f, -1.0f), GMANVector());
  gman::Appearance hostAppearance;
  hostAppearance.lights = {&pointLight};
  host->setAppearance(hostAppearance);
  world.add(host);

  // The area emitter: a rigidly placed sphere of radius 1, Le (2, 2, 2),
  // its centre at distance pi from p.
  GMANMatrix4 place;
  place.trans(0.0f, 0.0f, -(RtFloat)kPi);
  GMANTransform const transform = makeTransform(place);
  GMANRaySphere* areaSphere = new GMANRaySphere(1.0f, -1.0f, 1.0f, 360.0f, GMANParameterList(), transform);
  GMANLight const areaLight(GMAN_LIGHT_AREA, GMANColor(2.0f, 2.0f, 2.0f), GMANPoint(), GMANVector());
  gman::Appearance areaAppearance;
  areaAppearance.areaLight = &areaLight;
  areaSphere->setAppearance(areaAppearance);
  world.add(areaSphere);

  std::vector<gman::Emitter> const emitters = gman::emitters(world);
  check(emitters.size() == 2, "lightChoiceProbability: the hand-built world enumerates exactly two emitters");

  std::size_t deltaIndex = 0;
  std::size_t areaIndex = 0;
  for (std::size_t j = 0; j < emitters.size(); ++j) {
    if (emitters[j].shape == nullptr) {
      deltaIndex = j;
    } else {
      areaIndex = j;
    }
  }

  RtFloat const areaProbability = gman::lightChoiceProbability(emitters, p, areaIndex);
  RtFloat const deltaProbability = gman::lightChoiceProbability(emitters, p, deltaIndex);
  std::printf("lightChoiceProbability: area %.10f, delta %.10f\n", (double)areaProbability, (double)deltaProbability);
  check(std::fabs(areaProbability - 0.3889845296) <= 1e-6f,
        "lightChoiceProbability: the area emitter's own probability matches the hand computation within 1e-6");
  check(std::fabs(deltaProbability - 0.6110154704) <= 1e-6f,
        "lightChoiceProbability: the delta light's own probability matches the hand computation within 1e-6");
  check(std::fabs((double)(areaProbability + deltaProbability) - 1.0) <= 1e-6,
        "lightChoiceProbability: the two probabilities sum to 1");

  // Calling it again for the area emitter, once as if from next-event
  // estimation's own draw and once as if from an emitter-hit's arrival,
  // returns the identical value: one function, one probability.
  RtFloat const areaProbabilityAgain = gman::lightChoiceProbability(emitters, p, areaIndex);
  check(areaProbability == areaProbabilityAgain,
        "lightChoiceProbability: calling it twice for the area emitter at the same p returns the identical value");
}

// C.5: the off switch. multipleImportanceSamplingEnabled(false) fixes
// next-event estimation's weight at 1 and the emitter-hit's weighted
// branch at 0, adding nothing at all for a non-delta departure -- exactly
// unit 6's own corrected rule. Also exercises the renderer's own switch,
// the sole production caller of setMultipleImportanceSampling besides
// tests/pathtracermisveach_test.cpp.
void testOffSwitch() {
  GMANPathtraceRenderer renderer;
  renderer.setMultipleImportanceSampling(false);

  RtFloat const nextEvent = gman::nextEventWeight(/*lightIsDelta=*/false, /*misEnabled=*/false, 3.0f, 2.0f);
  check(nextEvent == 1.0f, "nextEventWeight: MIS off fixes an area light's weight at 1 regardless of pLight/pBsdf");

  RtFloat const emitterHit =
      gman::emitterHitWeight(/*rayEligibleForEmitterHit=*/false, /*misEnabled=*/false, 2.0f, 3.0f);
  check(emitterHit == 0.0f,
        "emitterHitWeight: MIS off adds nothing for a non-delta departure -- not weight 1, not the power-heuristic "
        "value");
}

} // namespace

int main() {
  testNoExemptionAtFirstVertex();
  testTrueExemptions();
  testSameWeightAtADeeperVertex();
  testLightChoiceProbabilityTwoLights();
  testOffSwitch();

  return checkSummary("the path tracer's own MIS weight computation matches its hand-derived values, at every "
                      "vertex and with the switch off");
}
