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
 * Veach's plates: a sharp GGX lobe under a large area light, the scene
 * multiple importance sampling exists for. Light-only sampling (MIS off)
 * and MIS-on are each an unbiased estimator of the same direct-lighting
 * integral at the plate's own first vertex, so a high-sample MIS-on render
 * is their common reference; both must converge to it, and MIS-on's own
 * residual variance must fall well below light-only's.
 */

#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

#include "check.h"
#include "gmanattributes.h"
#include "gmanbsdf.h"
#include "gmancolor.h"
#include "gmanframebuffer.h"
#include "gmanlightsourcemgr.h"
#include "gmanmath.h"
#include "gmanmatrix4.h"
#include "gmanoptions.h"
#include "gmanparameterlist.h"
#include "gmanpathtracerenderer.h"
#include "gmanpoint.h"
#include "gmanray.h"
#include "gmanraypolygon.h"
#include "gmanraysphere.h"
#include "gmanshaderenvironment.h"
#include "gmansurfaceshader.h"
#include "gmantransform.h"
#include "gmanvector.h"
#include "gmanvsperspective.h"
#include "maketransform.h"
#include "pathtracerscene.h"
#include "ri.h"
#include "samplingstats.h"

namespace {

// The plate's own closure: one addGGX lobe, alpha 0.02 (a sharp but
// genuine GGX lobe, safely above BSDF::kMinGGXAlpha), weight (0.9, 0.9,
// 0.9), no other lobe. Fixed, not read from Cs or a RIB parameter: this
// scene's whole point is exactly this lobe under a large light.
class veachPlateShader : public GMANSurfaceShader {
public:
  GMANColor computeCi(GMANSurfaceEnv const& /*se*/) const override { return GMANColor(0.0f, 0.0f, 0.0f); }
  GMANColor computeOi(GMANSurfaceEnv const& se) const override { return se.Os; }
  gman::BSDF bsdf(GMANSurfaceEnv const& se) const override {
    gman::BSDF closure(se.N);
    closure.addGGX(GMANColor(0.9f, 0.9f, 0.9f), 0.02f);
    return closure;
  }
};

constexpr RtInt kRes = 16;
constexpr RtFloat kFovDegrees = 40.0f;
// The measured block: an 8x8 patch centred on the image, so every one of
// its pixels' four corner camera rays lands on the plate with margin.
constexpr int kBlockMin = 4;
constexpr int kBlockMax = 12; // exclusive
constexpr std::size_t kMinMeasuredPixels = 64;

// The plate: a flat square tilted 45 degrees about the x-axis, its own
// local frame u = (1, 0, 0), v = (0, cos45, sin45), centred at (0, 0,
// kPlateZ) -- placing its own mirror-reflection direction for a boresight
// camera ray at exactly (0, 1, 0), well clear of both the camera and the
// plate's own extent.
constexpr RtFloat kPlateZ = 6.0f;
constexpr RtFloat kPlateHalfWidth = 4.0f;

constexpr RtFloat kLightRadius = 4.0f;
constexpr RtFloat kAreaLe = 80.0f;
// The light's own centre lies along the boresight's own reflection
// direction from the plate, at this distance from the hit point.
constexpr RtFloat kLightDistance = 20.0f;
constexpr RtFloat kMaxAlignmentDegrees = 5.0;

std::vector<GMANPoint> platePoints(GMANVector const& u, GMANVector const& v, GMANPoint const& centre) {
  GMANVector const hu = u * kPlateHalfWidth;
  GMANVector const hv = v * kPlateHalfWidth;
  return {GMANPoint(centre.getX() - hu.getX() - hv.getX(), centre.getY() - hu.getY() - hv.getY(),
                    centre.getZ() - hu.getZ() - hv.getZ()),
          GMANPoint(centre.getX() - hu.getX() + hv.getX(), centre.getY() - hu.getY() + hv.getY(),
                    centre.getZ() - hu.getZ() + hv.getZ()),
          GMANPoint(centre.getX() + hu.getX() + hv.getX(), centre.getY() + hu.getY() + hv.getY(),
                    centre.getZ() + hu.getZ() + hv.getZ()),
          GMANPoint(centre.getX() + hu.getX() - hv.getX(), centre.getY() + hu.getY() - hv.getY(),
                    centre.getZ() + hu.getZ() - hv.getZ())};
}

GMANOptions::ScreenWindowStruct squareWindow() { return squareScreenWindow(); }

// A pixel is measured when its cell's four corner rays all hit the plate,
// found by the same ray-plane-then-bounds test gmanraypolygon.cpp's own
// intersection performs, in double.
struct PlatePlane {
  GMANVector normal;
  GMANVector u, v;
  GMANPoint centre;
};

bool intersectPlate(PlatePlane const& plate, GMANVector const& dir, GMANPoint& hit) {
  double const denom = (double)plate.normal.dot(dir);
  if (std::fabs(denom) < 1e-12) {
    return false;
  }
  GMANVector const toCentre(GMANPoint(0.0f, 0.0f, 0.0f), plate.centre);
  double const t = (double)plate.normal.dot(toCentre) / denom;
  if (!(t > 0.0)) {
    return false;
  }
  GMANPoint const p((RtFloat)(dir.getX() * t), (RtFloat)(dir.getY() * t), (RtFloat)(dir.getZ() * t));
  GMANVector const rel(plate.centre, p);
  double const lu = (double)plate.u.dot(rel);
  double const lv = (double)plate.v.dot(rel);
  if (std::fabs(lu) > (double)kPlateHalfWidth || std::fabs(lv) > (double)kPlateHalfWidth) {
    return false;
  }
  hit = p;
  return true;
}

bool pixelMeasured(gman::VSPerspective& viewingSys, PlatePlane const& plate, int px, int py) {
  for (int dy = 0; dy <= 1; ++dy) {
    for (int dx = 0; dx <= 1; ++dx) {
      GMANRay const corner = viewingSys.cameraRay((RtFloat)(px + dx), (RtFloat)(py + dy));
      GMANPoint hit;
      if (!intersectPlate(plate, corner.getDirection(), hit)) {
        return false;
      }
    }
  }
  return true;
}

// Builds the scene once, shared by every render: the plate (veachPlateShader)
// and the single spherical area light. Returns the plate's own plane, for
// pixelMeasured and the alignment check, and the light's own centre.
struct VeachScene {
  PlatePlane plate;
  GMANPoint lightCentre;
};

VeachScene buildVeachScene(GMANPathtraceRenderer& renderer, GMANLight const& areaLight) {
  double const phi = M_PI / 4.0; // 45 degrees
  GMANVector const u(1.0f, 0.0f, 0.0f);
  GMANVector const v(0.0f, (RtFloat)std::cos(phi), (RtFloat)std::sin(phi));
  GMANPoint const centre(0.0f, 0.0f, kPlateZ);

  GMANRayPolygon* plate = new GMANRayPolygon(platePoints(u, v, centre), GMANParameterList());
  gman::Appearance plateAppearance;
  plateAppearance.shader = std::make_shared<veachPlateShader const>();
  plateAppearance.Cs = GMANColor(1.0f, 1.0f, 1.0f);
  plateAppearance.Os = GMANColor(1.0f, 1.0f, 1.0f);
  plate->setAppearance(plateAppearance);
  renderer.getWorldManager()->add(plate);

  // The boresight's own reflection direction: I = (0, 0, 1); the plate's
  // own normal is v cross u (matching buildFloor's own winding
  // convention, tests/pathtracerscene.h).
  GMANVector normal = v.cross(u);
  normal.normalize();
  GMANVector const boresight(0.0f, 0.0f, 1.0f);
  RtFloat const cosIn = boresight.dot(normal);
  GMANVector const reflected = boresight - normal * ((RtFloat)2.0 * cosIn);

  GMANMatrix4 place;
  place.trans(centre.getX() + reflected.getX() * kLightDistance, centre.getY() + reflected.getY() * kLightDistance,
              centre.getZ() + reflected.getZ() * kLightDistance);
  GMANTransform const transform = makeTransform(place);
  GMANRaySphere* lightSphere =
      new GMANRaySphere(kLightRadius, -kLightRadius, kLightRadius, 360.0f, GMANParameterList(), transform);
  gman::Appearance lightAppearance;
  lightAppearance.areaLight = &areaLight;
  lightAppearance.Cs = GMANColor(0.0f, 0.0f, 0.0f);
  lightAppearance.Os = GMANColor(1.0f, 1.0f, 1.0f);
  lightSphere->setAppearance(lightAppearance);
  renderer.getWorldManager()->add(lightSphere);

  PlatePlane plane{normal, u, v, centre};
  GMANPoint const lightCentre(centre.getX() + reflected.getX() * kLightDistance,
                              centre.getY() + reflected.getY() * kLightDistance,
                              centre.getZ() + reflected.getZ() * kLightDistance);
  return {plane, lightCentre};
}

// Renders the scene once at (misEnabled, N), returning the frame buffer and
// the plate's own plane (identical across renders; returned once for the
// caller's own convenience).
std::unique_ptr<GMANFrameBuffer> renderVeach(bool misEnabled, std::uint32_t N, PlatePlane& planeOut,
                                             GMANPoint& lightCentreOut) {
  GMANOptions options;
  options.setFormat(kRes, kRes, 1.0f);
  options.setPixelSamples(1.0f, 1.0f);
  options.setPixelFilter(RiBoxFilter, 1.0f, 1.0f);
  options.setPathtracerSamples((RtInt)N);
  options.setBackground(GMANColor(0.0f, 0.0f, 0.0f));

  GMANMatrix4 const identity;
  gman::VSPerspective viewingSys(kRes, kRes, squareWindow(), identity, kFovDegrees, 0.5f, 50.0f);

  auto renderer = std::make_unique<GMANPathtraceRenderer>();
  renderer->setMultipleImportanceSampling(misEnabled);
  GMANLight const areaLight(GMAN_LIGHT_AREA, GMANColor(kAreaLe, kAreaLe, kAreaLe), GMANPoint(), GMANVector());
  VeachScene const scene = buildVeachScene(*renderer, areaLight);
  planeOut = scene.plate;
  lightCentreOut = scene.lightCentre;

  auto frameBuffer = std::make_unique<GMANFrameBuffer>(kRes, kRes, options.getBackground());
  GMANAttributes const attr;
  renderer->render(frameBuffer.get(), &viewingSys, options, attr);
  check(renderer->droppedPathCount() == 0, "Veach's plates: droppedPathCount() is 0");
  return frameBuffer;
}

void testVeachPlatesMisReducesVariance() {
  constexpr std::uint32_t kReferenceN = 2048u;
  constexpr std::uint32_t kTestN = 32u;

  PlatePlane plane1;
  GMANPoint lightCentre1;
  std::unique_ptr<GMANFrameBuffer> const reference = renderVeach(true, kReferenceN, plane1, lightCentre1);

  PlatePlane plane2;
  GMANPoint lightCentre2;
  std::unique_ptr<GMANFrameBuffer> const misOn = renderVeach(true, kTestN, plane2, lightCentre2);

  PlatePlane plane3;
  GMANPoint lightCentre3;
  std::unique_ptr<GMANFrameBuffer> const lightOnly = renderVeach(false, kTestN, plane3, lightCentre3);

  // The alignment contract: the boresight's own reflection direction
  // points within kMaxAlignmentDegrees of the light's true centre.
  GMANMatrix4 const identity;
  gman::VSPerspective viewingSys(kRes, kRes, squareWindow(), identity, kFovDegrees, 0.5f, 50.0f);
  GMANRay const boresightRay = viewingSys.cameraRay((RtFloat)kRes / 2.0f, (RtFloat)kRes / 2.0f);
  GMANPoint hit;
  bool const boresightHits = intersectPlate(plane1, boresightRay.getDirection(), hit);
  check(boresightHits, "Veach's plates: the boresight camera ray hits the plate");

  GMANVector boresightDir = boresightRay.getDirection();
  boresightDir.normalize();
  RtFloat const cosIn = boresightDir.dot(plane1.normal);
  GMANVector reflected = boresightDir - plane1.normal * ((RtFloat)2.0 * cosIn);
  reflected.normalize();
  GMANVector toLight(hit, lightCentre1);
  toLight.normalize();
  double const cosAngle = GMANMax((RtFloat)-1.0, GMANMin((RtFloat)1.0, reflected.dot(toLight)));
  double const angleDegrees = std::acos(cosAngle) * 180.0 / M_PI;
  std::printf("Veach's plates: alignment angle %.4f degrees\n", angleDegrees);
  check(angleDegrees <= kMaxAlignmentDegrees,
        "Veach's plates: the light's centre lies within 5 degrees of the plate's own reflection direction");

  std::vector<double> residualsMisOn;
  std::vector<double> residualsLightOnly;
  std::vector<double> paired;
  std::size_t measuredCount = 0;
  for (int py = kBlockMin; py < kBlockMax; ++py) {
    for (int px = kBlockMin; px < kBlockMax; ++px) {
      if (!pixelMeasured(viewingSys, plane1, px, py)) {
        continue;
      }
      ++measuredCount;
      double const refValue = (double)reference->getPixel(px, py).getRed();
      double const onValue = (double)misOn->getPixel(px, py).getRed();
      double const offValue = (double)lightOnly->getPixel(px, py).getRed();
      residualsMisOn.push_back(onValue - refValue);
      residualsLightOnly.push_back(offValue - refValue);
      paired.push_back(onValue - offValue);
    }
  }
  std::printf("Veach's plates: %zu of %d measured pixels\n", measuredCount,
              (kBlockMax - kBlockMin) * (kBlockMax - kBlockMin));
  check(measuredCount >= kMinMeasuredPixels, "Veach's plates: at least 8x8 measured pixels");

  GmanMeanStderr const onStat = meanStderr(residualsMisOn);
  GmanMeanStderr const offStat = meanStderr(residualsLightOnly);
  GmanMeanStderr const pairedStat = meanStderr(paired);
  std::printf("Veach's plates: MIS-on residual mean %.6f (stderr %.6f), light-only residual mean %.6f (stderr "
              "%.6f), paired residual mean %.6f (stderr %.6f)\n",
              onStat.mean, onStat.stderrOfMean, offStat.mean, offStat.stderrOfMean, pairedStat.mean,
              pairedStat.stderrOfMean);

  checkNear(onStat.mean, 0.0, onStat.stderrOfMean, 1e-6,
            "Veach's plates: MIS-on's own mean matches the reference "
            "within 5 sigma");
  checkNear(offStat.mean, 0.0, offStat.stderrOfMean, 1e-6,
            "Veach's plates: light-only's own mean matches the reference within 5 sigma");
  checkNear(pairedStat.mean, 0.0, pairedStat.stderrOfMean, 1e-6,
            "Veach's plates: the paired residual (MIS-on minus light-only) matches 0 within 5 sigma");

  double const count = (double)measuredCount;
  double const onVariance = onStat.stderrOfMean * onStat.stderrOfMean * count;
  double const offVariance = offStat.stderrOfMean * offStat.stderrOfMean * count;
  std::printf("Veach's plates: MIS-on residual variance %.8e, light-only residual variance %.8e, ratio %.3f\n",
              onVariance, offVariance, (onVariance > 0.0) ? offVariance / onVariance : 0.0);
  check(offVariance >= 4.0 * onVariance, "Veach's plates: light-only's residual variance is at least 4x MIS-on's");
}

} // namespace

int main() {
  testVeachPlatesMisReducesVariance();

  return checkSummary("MIS-on and light-only sampling converge to the same mean on Veach's plates, and MIS-on's "
                      "own residual variance falls well below light-only's");
}
