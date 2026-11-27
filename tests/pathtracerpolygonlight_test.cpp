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
 * The path tracer's own polygon area light: a 2 x 2 panel over the shared
 * floor scene checked against Lambert's formula for a polygonal luminaire,
 * and a camera ray's direct view of panels wound toward and away from it,
 * one of them carrying vertex normals that disagree with its plane normal.
 */

#include <cmath>
#include <cstddef>
#include <numbers>
#include <utility>
#include <vector>

#include "check.h"
#include "gmanattributes.h"
#include "gmancolor.h"
#include "gmanframebuffer.h"
#include "gmanlightsourcemgr.h"
#include "gmanmath.h"
#include "gmanmatrix4.h"
#include "gmanoptions.h"
#include "gmanowned.h"
#include "gmanparameterlist.h"
#include "gmanpathtracerenderer.h"
#include "gmanpoint.h"
#include "gmanray.h"
#include "gmanraypolygon.h"
#include "gmanshading.h"
#include "gmanvector.h"
#include "gmanvsperspective.h"
#include "pathtracerarealightscene.h"
#include "pathtracerscene.h"
#include "ri.h"
#include "samplingstats.h"
#include "taggedmesh.h"

namespace {

constexpr RtInt kFloorRes = 81;
constexpr RtInt kFloorSamples = 64;
constexpr std::size_t kMinMeasuredPixels = 400;
constexpr double kResidualFloor = 1e-4;
constexpr RtFloat kReflectance = 0.5f;
constexpr int kMidGrid = 16;

constexpr RtFloat kPanelHalfWidth = 1.0f;
constexpr RtFloat kPanelY = -1.0f;
constexpr RtFloat kPanelZMin = 8.0f;
constexpr RtFloat kPanelZMax = 10.0f;

constexpr RtInt kDirectRes = 41;
constexpr RtInt kDirectSamples = 4;
constexpr RtFloat kDirectPanelZ = 8.0f;
constexpr RtFloat kDirectPanelHalfExtent = 1.0f;
constexpr RtFloat kTowardPanelX = -5.0f;
constexpr RtFloat kAwayPanelX = 0.0f;
constexpr RtFloat kTiltedPanelX = 5.0f;
constexpr RtFloat kTiltDegrees = 75.0f;
constexpr double kSelectionMargin = 0.05;

// A panel as getRSPolygonMesh builds one: a one-face mesh with the area light
// on the mesh and on its face, its own colour black so it reflects nothing.
// vertexNormals, when given, shade the face and leave the direction it emits
// unchanged.
void addPanel(GMANPathtraceRenderer& renderer, GMANLight const& light, std::vector<GMANPoint> const& loop,
              std::vector<GMANVector> const& vertexNormals) {
  gman::Appearance appearance;
  appearance.areaLight = &light;
  appearance.Cs = GMANColor(0.0f, 0.0f, 0.0f);
  appearance.Os = GMANColor(1.0f, 1.0f, 1.0f);

  auto mesh = taggedmesh::buildTaggedMesh({taggedmesh::FaceSpec{loop, {}, vertexNormals}}, appearance);
  renderer.getWorldManager()->add(std::move(mesh));
}

// The panel over the floor: 2 wide in x, 2 deep in z, at y = -1, wound
// (1, -1, 8) (1, -1, 10) (-1, -1, 10) (-1, -1, 8) so its plane normal is -y.
std::vector<GMANPoint> overheadPanel() {
  return {GMANPoint(kPanelHalfWidth, kPanelY, kPanelZMin), GMANPoint(kPanelHalfWidth, kPanelY, kPanelZMax),
          GMANPoint(-kPanelHalfWidth, kPanelY, kPanelZMax), GMANPoint(-kPanelHalfWidth, kPanelY, kPanelZMin)};
}

// Lambert's formula for the irradiance at (x, y, z) with normal +y from the
// uniform Lambertian polygon ring of radiance radiance, wholly above the
// point's horizon: (radiance / 2) |sum of theta_i (gamma_i . n)|, theta_i the
// angle between the directions to ring's i-th and next vertices and gamma_i
// the unit normal of the plane they span.
double polygonIrradiance(std::vector<GMANPoint> const& ring, double x, double y, double z, double radiance) {
  double sum = 0.0;
  for (std::size_t i = 0; i < ring.size(); ++i) {
    GMANPoint const& from = ring[i];
    GMANPoint const& to = ring[(i + 1) % ring.size()];
    double const ax = (double)from.getX() - x, ay = (double)from.getY() - y, az = (double)from.getZ() - z;
    double const bx = (double)to.getX() - x, by = (double)to.getY() - y, bz = (double)to.getZ() - z;
    double const cx = ay * bz - az * by, cy = az * bx - ax * bz, cz = ax * by - ay * bx;
    double const crossLength = std::sqrt(cx * cx + cy * cy + cz * cz);
    double const theta = std::atan2(crossLength, ax * bx + ay * by + az * bz);
    sum += theta * cy / crossLength;
  }
  return 0.5 * radiance * std::fabs(sum);
}

// The mean, over a 16 x 16 midpoint grid of pixel (px, py)'s cell, of the
// floor's radiance under the panel: (rho / pi) times polygonIrradiance, rho =
// 0.5 (Kd 1, Cs 0.5).
GMANColor analyticPanelExpected(gman::VSPerspective& viewingSys, int px, int py, std::vector<GMANPoint> const& ring) {
  double sum = 0.0;
  for (int sy = 0; sy < kMidGrid; ++sy) {
    for (int sx = 0; sx < kMidGrid; ++sx) {
      RtFloat const rx = (RtFloat)px + ((RtFloat)sx + 0.5f) / (RtFloat)kMidGrid;
      RtFloat const ry = (RtFloat)py + ((RtFloat)sy + 0.5f) / (RtFloat)kMidGrid;
      double x, z;
      if (!intersectFloorPlane(viewingSys.cameraRay(rx, ry), x, z)) {
        continue;
      }
      sum += (double)kReflectance / std::numbers::pi * polygonIrradiance(ring, x, (double)kFloorY, z, (double)kAreaLe);
    }
  }
  RtFloat const value = (RtFloat)(sum / (double)(kMidGrid * kMidGrid));
  return GMANColor(value, value, value);
}

// The floor under the panel, which hangs at y = -1 over z in [8, 10]. A camera
// ray from the origin to a floor point (x, -4, z) with z <= 17 crosses y = -1
// a quarter of the way along, at z <= 4.25, short of the panel's z >= 8, so no
// measured pixel's ray meets the panel and it reaches the floor only as light.
// The panel's own colour is black, so nothing the floor reflects returns to it.
void testPanelOverFloor() {
  GMANOptions options;
  options.setFormat(kFloorRes, kFloorRes, 1.0f);
  options.setPixelSamples(1.0f, 1.0f);
  options.setPixelFilter(RiBoxFilter, 1.0f, 1.0f);
  options.setPathtracerSamples(kFloorSamples);
  options.setBackground(GMANColor(0.0f, 0.0f, 0.0f));

  GMANMatrix4 const identity;
  gman::VSPerspective viewingSys(kFloorRes, kFloorRes, squareScreenWindow(), identity, 90.0f, 0.5f, 50.0f);

  GMANPathtraceRenderer renderer;
  auto floor = buildFloor();
  gman::Appearance floorAppearance;
  floorAppearance.shader = loadShader("matte", matteParams(1.0f));
  floorAppearance.Cs = GMANColor(kReflectance, kReflectance, kReflectance);
  floorAppearance.Os = GMANColor(1.0f, 1.0f, 1.0f);
  floor->setAppearance(floorAppearance);
  renderer.getWorldManager()->add(std::move(floor));

  GMANLight const areaLight(GMAN_LIGHT_AREA, GMANColor(kAreaLe, kAreaLe, kAreaLe), GMANPoint(), GMANVector());
  std::vector<GMANPoint> const panel = overheadPanel();
  addPanel(renderer, areaLight, panel, {});

  GMANFrameBuffer frameBuffer(kFloorRes, kFloorRes, options.getBackground());
  GMANAttributes const attr;
  renderer.render(&frameBuffer, &viewingSys, options, attr);
  check(renderer.droppedPathCount() == 0, "panel over floor: droppedPathCount() is 0");

  std::vector<double> residuals[3];
  std::vector<double> expectedByChannel[3];
  std::size_t measuredCount = 0;
  for (int py = 0; py < kFloorRes; ++py) {
    for (int px = 0; px < kFloorRes; ++px) {
      if (!pixelMeasured(viewingSys, px, py)) {
        continue;
      }
      ++measuredCount;
      GMANColor const expected = analyticPanelExpected(viewingSys, px, py, panel);
      GMANColor const actual = frameBuffer.getPixel(px, py);
      for (int c = 0; c < 3; ++c) {
        residuals[c].push_back(channel(actual, c) - channel(expected, c));
        expectedByChannel[c].push_back(channel(expected, c));
      }
    }
  }
  check(measuredCount >= kMinMeasuredPixels, "panel over floor: at least 400 measured pixels");
  checkResiduals(residuals, expectedByChannel, kResidualFloor, "panel over floor");
}

// A square panel of half-extent kDirectPanelHalfExtent in the plane z =
// kDirectPanelZ centred on x = centreX, wound (-h, -h) (-h, h) (h, h) (h, -h)
// when toward is true, so its plane normal is -z, toward the camera at the
// origin, and the reverse when it is false.
std::vector<GMANPoint> directPanel(RtFloat centreX, bool toward) {
  RtFloat const h = kDirectPanelHalfExtent;
  std::vector<GMANPoint> loop = {GMANPoint(centreX - h, -h, kDirectPanelZ), GMANPoint(centreX - h, h, kDirectPanelZ),
                                 GMANPoint(centreX + h, h, kDirectPanelZ), GMANPoint(centreX + h, -h, kDirectPanelZ)};
  if (!toward) {
    loop = std::vector<GMANPoint>(loop.rbegin(), loop.rend());
  }
  return loop;
}

// The panel's own vertex normals: tilted kTiltDegrees from the plane normal
// (0, 0, -1) toward +x.
std::vector<GMANVector> tiltedNormals() {
  RtFloat const tilt = GMANRadians(kTiltDegrees);
  return std::vector<GMANVector>(4, GMANVector(std::sin(tilt), 0.0f, -std::cos(tilt)));
}

// Where the camera ray from the origin through direction meets the plane z =
// kDirectPanelZ, as an offset from the panel centred on centreX.
bool onPanelPlane(GMANVector const& direction, RtFloat centreX, double& offsetX, double& offsetY) {
  if (!(direction.getZ() > 0.0f)) {
    return false;
  }
  double const t = (double)kDirectPanelZ / (double)direction.getZ();
  offsetX = t * (double)direction.getX() - (double)centreX;
  offsetY = t * (double)direction.getY();
  return true;
}

// Whether all four corner rays of pixel (px, py) land inside the panel
// centred on centreX, shrunk by kSelectionMargin, or, when inside is false,
// outside it grown by kSelectionMargin.
bool cornersAllOnOrOff(gman::VSPerspective& viewingSys, int px, int py, RtFloat centreX, bool inside) {
  double const margin = inside ? -kSelectionMargin : kSelectionMargin;
  double const reach = (double)kDirectPanelHalfExtent * (1.0 + margin);
  for (int dy = 0; dy <= 1; ++dy) {
    for (int dx = 0; dx <= 1; ++dx) {
      GMANRay const corner = viewingSys.cameraRay((RtFloat)(px + dx), (RtFloat)(py + dy));
      double offsetX, offsetY;
      if (!onPanelPlane(corner.getDirection(), centreX, offsetX, offsetY)) {
        return false;
      }
      bool const within = std::fabs(offsetX) <= reach && std::fabs(offsetY) <= reach;
      if (within != inside) {
        return false;
      }
    }
  }
  return true;
}

// Whether every corner ray of pixel (px, py) meets the tilted panel's
// shading normal from behind and its plane normal from the front:
// -I . N_shading < 0 < -I . N_geometric.
bool cornersMeetTiltedPanelBehindShadingNormal(gman::VSPerspective& viewingSys, int px, int py) {
  GMANVector const shading = tiltedNormals()[0];
  GMANVector const geometric(0.0f, 0.0f, -1.0f);
  for (int dy = 0; dy <= 1; ++dy) {
    for (int dx = 0; dx <= 1; ++dx) {
      GMANVector const incoming = viewingSys.cameraRay((RtFloat)(px + dx), (RtFloat)(py + dy)).getDirection();
      if (!(-incoming.dot(shading) < 0.0f && 0.0f < -incoming.dot(geometric))) {
        return false;
      }
    }
  }
  return true;
}

bool readsLe(GMANColor const& actual) {
  return std::fabs(actual.getRed() - kAreaLe) <= 1e-4 && std::fabs(actual.getGreen() - kAreaLe) <= 1e-4 &&
         std::fabs(actual.getBlue() - kAreaLe) <= 1e-4;
}

bool readsBlack(GMANColor const& actual) {
  return actual.getRed() == 0.0f && actual.getGreen() == 0.0f && actual.getBlue() == 0.0f;
}

void testDirectHit() {
  GMANOptions options;
  options.setFormat(kDirectRes, kDirectRes, 1.0f);
  options.setPixelSamples(1.0f, 1.0f);
  options.setPixelFilter(RiBoxFilter, 1.0f, 1.0f);
  options.setPathtracerSamples(kDirectSamples);
  options.setBackground(GMANColor(0.0f, 0.0f, 0.0f));

  GMANMatrix4 const identity;
  gman::VSPerspective viewingSys(kDirectRes, kDirectRes, squareScreenWindow(), identity, 90.0f, 0.5f, 50.0f);

  GMANPathtraceRenderer renderer;
  GMANLight const areaLight(GMAN_LIGHT_AREA, GMANColor(kAreaLe, kAreaLe, kAreaLe), GMANPoint(), GMANVector());
  addPanel(renderer, areaLight, directPanel(kTowardPanelX, true), {});
  addPanel(renderer, areaLight, directPanel(kAwayPanelX, false), {});
  addPanel(renderer, areaLight, directPanel(kTiltedPanelX, true), tiltedNormals());

  GMANFrameBuffer frameBuffer(kDirectRes, kDirectRes, options.getBackground());
  GMANAttributes const attr;
  renderer.render(&frameBuffer, &viewingSys, options, attr);
  check(renderer.droppedPathCount() == 0, "direct hit: droppedPathCount() is 0");

  std::size_t towardCount = 0, awayCount = 0, tiltedCount = 0, offCount = 0;
  bool towardExact = true, awayExact = true, tiltedExact = true, offExact = true;
  bool tiltedPremise = true;
  for (int py = 0; py < kDirectRes; ++py) {
    for (int px = 0; px < kDirectRes; ++px) {
      GMANColor const actual = frameBuffer.getPixel(px, py);
      if (cornersAllOnOrOff(viewingSys, px, py, kTowardPanelX, true)) {
        ++towardCount;
        towardExact = towardExact && readsLe(actual);
      } else if (cornersAllOnOrOff(viewingSys, px, py, kAwayPanelX, true)) {
        ++awayCount;
        awayExact = awayExact && readsBlack(actual);
      } else if (cornersAllOnOrOff(viewingSys, px, py, kTiltedPanelX, true)) {
        ++tiltedCount;
        tiltedExact = tiltedExact && readsLe(actual);
        tiltedPremise = tiltedPremise && cornersMeetTiltedPanelBehindShadingNormal(viewingSys, px, py);
      } else if (cornersAllOnOrOff(viewingSys, px, py, kTowardPanelX, false) &&
                 cornersAllOnOrOff(viewingSys, px, py, kAwayPanelX, false) &&
                 cornersAllOnOrOff(viewingSys, px, py, kTiltedPanelX, false)) {
        ++offCount;
        offExact = offExact && readsBlack(actual);
      }
    }
  }
  check(towardCount >= 4, "direct hit: at least 4 pixels squarely on the panel wound toward the camera");
  check(awayCount >= 4, "direct hit: at least 4 pixels squarely on the panel wound away from the camera");
  check(tiltedCount >= 4, "direct hit: at least 4 pixels squarely on the panel with tilted vertex normals");
  check(offCount >= 4, "direct hit: at least 4 pixels squarely off every panel");
  check(tiltedPremise, "direct hit: every tilted-panel pixel's rays meet -I . N_shading < 0 < -I . N_geometric");
  check(towardExact, "direct hit: every pixel on the panel wound toward the camera reads Le exactly");
  check(awayExact, "direct hit: every pixel on the panel wound away from the camera reads black exactly");
  check(tiltedExact, "direct hit: every pixel on the tilted-normal panel reads Le exactly");
  check(offExact, "direct hit: every pixel off every panel reads black exactly");
}

} // namespace

int main() {
  testPanelOverFloor();
  testDirectHit();

  return checkSummary("the path tracer's polygon panel lights the floor to Lambert's formula and shows Le to a camera "
                      "ray along its plane normal, black behind it");
}
