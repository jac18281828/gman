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
 * An emitter cannot light a point on its own surface, so a vertex on an
 * emitter never chooses that emitter, as a next-event light or as the
 * light an emitter hit's weight reads its choice probability for. Two
 * scenes check it.
 *
 * A camera-facing emitting disk under one distant light: the disk's own
 * light-choice weight peaks on its own surface, so without the exclusion
 * almost every next-event draw there picks the disk, contributes nothing,
 * and the rare draw that reaches the distant light divides by that draw's
 * own tiny probability. The pooled residual mean matches the analytic
 * value and its standard error stays below a bound a tenth of the
 * exclusion-free error.
 *
 * A camera-facing emitting disk with a white matte surface, beside a
 * second emitting sphere that its own BSDF draws often reach. A BSDF draw
 * from a point on the disk that lands on the sphere weighs itself against
 * the probability next-event estimation would have chosen the sphere from
 * that same point, which excludes the disk. The paired per-pixel residual
 * between the render with multiple importance sampling on and off, which
 * share one seed, agrees with 0.
 */

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "check.h"
#include "gmanattributes.h"
#include "gmancolor.h"
#include "gmanframebuffer.h"
#include "gmanlightsourcemgr.h"
#include "gmanmatrix4.h"
#include "gmanoptions.h"
#include "gmanparameterlist.h"
#include "gmanpathtracerenderer.h"
#include "gmanpoint.h"
#include "gmanray.h"
#include "gmanraydisk.h"
#include "gmanraysphere.h"
#include "gmantransform.h"
#include "gmanvsperspective.h"
#include "maketransform.h"
#include "pathtracerarealightscene.h"
#include "pathtracerscene.h"
#include "ri.h"

namespace {

// The disk's own placement and radiance: large enough relative to the
// distant light that almost every next-event draw at a point on the
// disk's own surface would, with no exclusion, pick the disk itself.
constexpr RtFloat kDiskRadius = 10.0f;
constexpr RtFloat kDiskZ = 5.0f;
constexpr RtFloat kDiskLe = 100.0f;

// The distant light's own radiance and direction (light travels toward
// +z, +y): its own cosine to the disk's normal (0, 0, -1) is exactly
// 0.8, a fixed angle off head-on.
constexpr RtFloat kDistantCl = 1.0f;
constexpr RtFloat kCosTheta = 0.8f;

constexpr RtFloat kReflectance = 1.0f; // white matte, Kd 1, Cs (1, 1, 1)
constexpr RtFloat kFov = 90.0f;
constexpr RtInt kRes = 9;
constexpr std::uint32_t kSamples = 1024u;

// The pooled standard error's own bound: a flat disk under one distant
// light carries no per-pixel variance once self-selection cannot happen,
// so every measured pixel agrees exactly and the true value is 0; this
// bound stays comfortably above that and comfortably under a tenth of the
// same measurement with the exclusion disabled (the primitive argument
// ignored).
constexpr double kStderrBound = 0.0001;

// The disk-and-sphere pairing: the disk's own radiance and the sphere's,
// the sphere's placement (beside the camera's view of the disk, so no
// measured pixel's ray reaches it, yet close enough that many of the
// disk's own BSDF draws do) and the render's size.
constexpr RtFloat kPairDiskLe = 100.0f;
constexpr RtFloat kPairSphereLe = 10.0f;
constexpr RtFloat kPairSphereRadius = 3.0f;
constexpr RtFloat kPairSphereX = 7.0f;
constexpr RtFloat kPairSphereY = 0.0f;
constexpr RtFloat kPairSphereZ = 2.0f;
constexpr RtInt kPairRes = 9;
constexpr std::uint32_t kPairSamples = 1024u;

// Renders the disk scene and collects each measured pixel's residual
// against the analytic expectation Le + rho * Cl * cosTheta / pi, uniform
// over the whole disk since it is flat and rigidly lit.
void renderAndCollectResiduals(std::vector<double> residuals[3], std::vector<double> expectedByChannel[3],
                               std::size_t& measuredCount, std::size_t& droppedCount) {
  GMANOptions options;
  options.setFormat(kRes, kRes, 1.0f);
  options.setPixelSamples(1.0f, 1.0f);
  options.setPixelFilter(RiBoxFilter, 1.0f, 1.0f);
  options.setPathtracerSamples((RtInt)kSamples);

  GMANMatrix4 const identity;
  gman::VSPerspective viewingSys(kRes, kRes, squareScreenWindow(), identity, kFov, 0.5f, 50.0f);

  GMANPathtraceRenderer renderer;

  GMANMatrix4 place;
  place.trans(0.0f, 0.0f, kDiskZ);
  GMANTransform const transform = makeTransform(place);
  GMANRayDisk* disk = new GMANRayDisk(0.0f, kDiskRadius, 360.0f, GMANParameterList(), transform);

  GMANLight const areaLight(GMAN_LIGHT_AREA, GMANColor(kDiskLe, kDiskLe, kDiskLe), GMANPoint(), GMANVector());
  GMANVector const distantDirection(0.0f, 0.6f, 0.8f);
  GMANLight const distantLight(GMAN_LIGHT_DISTANT, GMANColor(kDistantCl, kDistantCl, kDistantCl), GMANPoint(),
                               distantDirection);

  gman::Appearance appearance;
  appearance.shader = loadShader("matte", matteParams(1.0f));
  appearance.Cs = GMANColor(1.0f, 1.0f, 1.0f);
  appearance.Os = GMANColor(1.0f, 1.0f, 1.0f);
  appearance.areaLight = &areaLight;
  appearance.lights = {&distantLight};
  disk->setAppearance(appearance);
  renderer.getWorldManager()->add(disk);

  GMANFrameBuffer frameBuffer(kRes, kRes, options.getBackground());
  GMANAttributes const attr;
  renderer.render(&frameBuffer, &viewingSys, options, attr);
  droppedCount = renderer.droppedPathCount();

  // A delta light's own Cl already carries the *pi that a Lambert lobe's
  // eval divides out, so the two cancel: no /pi here, matching
  // pathtracerscene.h's own analyticExpected.
  RtFloat const expected = kDiskLe + kReflectance * kDistantCl * kCosTheta;
  GMANColor const expectedColor(expected, expected, expected);

  measuredCount = 0;
  for (int py = 0; py < kRes; ++py) {
    for (int px = 0; px < kRes; ++px) {
      bool onDisk = true;
      for (int dy = 0; dy <= 1 && onDisk; ++dy) {
        for (int dx = 0; dx <= 1 && onDisk; ++dx) {
          GMANRay const corner = viewingSys.cameraRay((RtFloat)(px + dx), (RtFloat)(py + dy));
          GMANHit cornerHit;
          onDisk = disk->intersect(corner, cornerHit);
        }
      }
      if (!onDisk) {
        continue;
      }
      ++measuredCount;
      GMANColor const actual = frameBuffer.getPixel(px, py);
      for (int c = 0; c < 3; ++c) {
        residuals[c].push_back(channel(actual, c) - channel(expectedColor, c));
        expectedByChannel[c].push_back(channel(expectedColor, c));
      }
    }
  }
}

void testSelfExclusionResiduals() {
  std::vector<double> residuals[3];
  std::vector<double> expectedByChannel[3];
  std::size_t measuredCount = 0;
  std::size_t droppedCount = 0;
  renderAndCollectResiduals(residuals, expectedByChannel, measuredCount, droppedCount);

  check(measuredCount == (std::size_t)(kRes * kRes),
        "pathtracerselfexclusion: every pixel is measured, the disk fills the frame");
  check(droppedCount == 0, "pathtracerselfexclusion: no path dropped for a non-finite channel");

  checkResiduals(residuals, expectedByChannel, 1e-4, "pathtracerselfexclusion");

  for (int c = 0; c < 3; ++c) {
    GmanMeanStderr const stat = meanStderr(residuals[c]);
    std::printf("pathtracerselfexclusion channel %d: pooled stderr %.9f (bound %.6f)\n", c, stat.stderrOfMean,
                kStderrBound);
    check(stat.stderrOfMean < kStderrBound,
          "pathtracerselfexclusion: the pooled standard error stays below its own bound");
  }
}

// One render of the disk beside the sphere at the pairing's own seed and
// counts, and the pixels whose four corner rays all reach the disk before
// anything else.
struct PairRender {
  std::unique_ptr<GMANFrameBuffer> frameBuffer;
  std::vector<std::pair<int, int>> measured;
  std::size_t droppedCount = 0;
};

PairRender renderDiskAndSphere(bool misEnabled) {
  GMANOptions options;
  options.setFormat(kPairRes, kPairRes, 1.0f);
  options.setPixelSamples(1.0f, 1.0f);
  options.setPixelFilter(RiBoxFilter, 1.0f, 1.0f);
  options.setPathtracerSamples((RtInt)kPairSamples);

  GMANMatrix4 const identity;
  gman::VSPerspective viewingSys(kPairRes, kPairRes, squareScreenWindow(), identity, kFov, 0.5f, 50.0f);

  GMANPathtraceRenderer renderer;
  renderer.setMultipleImportanceSampling(misEnabled);

  GMANMatrix4 place;
  place.trans(0.0f, 0.0f, kDiskZ);
  GMANTransform const transform = makeTransform(place);
  GMANRayDisk* disk = new GMANRayDisk(0.0f, kDiskRadius, 360.0f, GMANParameterList(), transform);

  GMANLight const diskLight(GMAN_LIGHT_AREA, GMANColor(kPairDiskLe, kPairDiskLe, kPairDiskLe), GMANPoint(),
                            GMANVector());
  gman::Appearance appearance;
  appearance.shader = loadShader("matte", matteParams(1.0f));
  appearance.Cs = GMANColor(1.0f, 1.0f, 1.0f);
  appearance.Os = GMANColor(1.0f, 1.0f, 1.0f);
  appearance.areaLight = &diskLight;
  disk->setAppearance(appearance);
  renderer.getWorldManager()->add(disk);

  GMANLight const sphereLight(GMAN_LIGHT_AREA, GMANColor(kPairSphereLe, kPairSphereLe, kPairSphereLe), GMANPoint(),
                              GMANVector());
  GMANRaySphere* const sphere =
      addEmittingSphere(renderer, sphereLight, kPairSphereRadius, kPairSphereX, kPairSphereY, kPairSphereZ);

  PairRender result;
  for (int py = 0; py < kPairRes; ++py) {
    for (int px = 0; px < kPairRes; ++px) {
      bool onDisk = true;
      for (int dy = 0; dy <= 1 && onDisk; ++dy) {
        for (int dx = 0; dx <= 1 && onDisk; ++dx) {
          GMANRay const corner = viewingSys.cameraRay((RtFloat)(px + dx), (RtFloat)(py + dy));
          GMANHit hit;
          onDisk = disk->intersect(corner, hit) && !sphere->intersect(corner, hit);
        }
      }
      if (onDisk) {
        result.measured.emplace_back(px, py);
      }
    }
  }

  result.frameBuffer = std::make_unique<GMANFrameBuffer>(kPairRes, kPairRes, options.getBackground());
  GMANAttributes const attr;
  renderer.render(result.frameBuffer.get(), &viewingSys, options, attr);
  result.droppedCount = renderer.droppedPathCount();
  return result;
}

// MIS on and MIS off are each unbiased for the same radiance, so the paired
// per-pixel residual (one seed, one sample count, differing only in which
// weight formula applied) agrees with 0. A BSDF draw from the disk that
// lands on the sphere weighs itself against a light-choice probability
// that must exclude the disk, exactly as next-event estimation's own
// choice from that point does; a probability that includes the disk
// overweights those draws and the residual reads positive.
void testDiskAndSphereMisPairing() {
  PairRender const on = renderDiskAndSphere(true);
  PairRender const off = renderDiskAndSphere(false);

  check(on.measured.size() == (std::size_t)(kPairRes * kPairRes),
        "pathtracerselfexclusion pair: every pixel is measured, the disk fills the frame clear of the sphere");
  check(on.droppedCount == 0 && off.droppedCount == 0,
        "pathtracerselfexclusion pair: no path dropped for a non-finite channel");

  std::vector<double> paired[3];
  double onSum = 0.0;
  for (auto const& [px, py] : on.measured) {
    GMANColor const onPixel = on.frameBuffer->getPixel(px, py);
    GMANColor const offPixel = off.frameBuffer->getPixel(px, py);
    onSum += onPixel.getRed();
    for (int c = 0; c < 3; ++c) {
      paired[c].push_back(channel(onPixel, c) - channel(offPixel, c));
    }
  }

  // The sphere lights the disk: the mean sits above the disk's own radiance.
  double const onMean = onSum / (double)on.measured.size();
  std::printf("pathtracerselfexclusion pair: MIS-on mean red %.6f (disk Le %.1f)\n", onMean, (double)kPairDiskLe);
  check(onMean > (double)kPairDiskLe + 0.1, "pathtracerselfexclusion pair: the sphere's light reaches the disk");

  for (int c = 0; c < 3; ++c) {
    GmanMeanStderr const stat = meanStderr(paired[c]);
    std::printf("pathtracerselfexclusion pair channel %d: paired residual mean %.6f (%.3f sigma)\n", c, stat.mean,
                stat.stderrOfMean > 0.0 ? stat.mean / stat.stderrOfMean : 0.0);
    checkNear(stat.mean, 0.0, stat.stderrOfMean, 1e-4,
              "pathtracerselfexclusion pair: MIS-on and MIS-off agree within 5 sigma, channel " + std::to_string(c));
  }
}

} // namespace

int main() {
  testSelfExclusionResiduals();
  testDiskAndSphereMisPairing();

  return checkSummary("a point on an emitter's own surface never chooses that emitter, as a next-event light or "
                      "in an emitter hit's weight");
}
