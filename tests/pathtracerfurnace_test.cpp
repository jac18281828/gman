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
 * The sphere furnace: a point light at the centre of a closed diffuse
 * sphere converges to the analytic radiance ρI / (R²(1 − ρ)), and the
 * estimate's standard deviation over pixels falls as more paths average
 * per slot.
 */

#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

#include "check.h"
#include "gmanattributes.h"
#include "gmanframebuffer.h"
#include "gmanlightsourcemgr.h"
#include "gmanoptions.h"
#include "gmanparameterlist.h"
#include "gmanpathtracerenderer.h"
#include "gmanpoint.h"
#include "gmanraysphere.h"
#include "gmanshaderenvironment.h"
#include "gmansurfaceshader.h"
#include "gmanvector.h"
#include "gmanvsperspective.h"
#include "ri.h"
#include "samplingstats.h"

namespace {

constexpr RtInt kRes = 16;
constexpr RtFloat kSphereRadius = 10.0f;
constexpr RtFloat kReflectance = 0.75f;
constexpr RtFloat kIntensity = 100.0f;
constexpr double kExpectedRadiance = 3.0; // rho * I / (R^2 * (1 - rho))
constexpr double kFloor = 3e-4;

class LambertShader : public GMANSurfaceShader {
public:
  explicit LambertShader(GMANColor const& reflectance) : reflectance(reflectance) {}
  GMANColor computeCi(GMANSurfaceEnv const&) const override { return GMANColor(0.0f, 0.0f, 0.0f); }
  GMANColor computeOi(GMANSurfaceEnv const& se) const override { return se.Os; }
  gman::BSDF bsdf(GMANSurfaceEnv const& se) const override {
    gman::BSDF closure(se.N);
    closure.addLambert(reflectance);
    return closure;
  }

private:
  GMANColor reflectance;
};

GMANOptions::ScreenWindowStruct squareScreenWindow() {
  GMANOptions::ScreenWindowStruct sw;
  sw.left = -1.0f;
  sw.right = 1.0f;
  sw.bottom = -1.0f;
  sw.top = 1.0f;
  return sw;
}

// Renders the furnace at samples paths per slot and appends every
// pixel's colour into outChannels, red first, one vector per channel.
void renderFurnace(RtInt samples, std::vector<double> outChannels[3], std::size_t& droppedOut) {
  GMANOptions options;
  options.setFormat(kRes, kRes, 1.0f);
  options.setPixelSamples(1.0f, 1.0f);
  options.setPixelFilter(RiBoxFilter, 1.0f, 1.0f);
  options.setPathtracerSamples(samples);

  GMANMatrix4 const identity;
  gman::VSPerspective viewingSys(kRes, kRes, squareScreenWindow(), identity, 90.0f, 0.5f, 50.0f);

  GMANPathtraceRenderer renderer;
  LambertShader const shader(GMANColor(kReflectance, kReflectance, kReflectance));

  GMANRaySphere* sphere = new GMANRaySphere(kSphereRadius, -kSphereRadius, kSphereRadius, 360.0f, GMANParameterList());
  GMANLight const light(GMAN_LIGHT_POINT, GMANColor(kIntensity, kIntensity, kIntensity), GMANPoint(0.0f, 0.0f, 0.0f),
                        GMANVector());
  gman::Appearance appearance;
  appearance.shader = std::shared_ptr<GMANSurfaceShader const>(&shader, [](GMANSurfaceShader const*) {});
  appearance.Os = GMANColor(1.0f, 1.0f, 1.0f);
  appearance.lights = {&light};
  sphere->setAppearance(appearance);
  renderer.getWorldManager()->add(sphere);

  GMANFrameBuffer frameBuffer(kRes, kRes, options.getBackground());
  GMANAttributes const attr;
  renderer.render(&frameBuffer, &viewingSys, options, attr);

  for (int y = 0; y < kRes; ++y) {
    for (int x = 0; x < kRes; ++x) {
      GMANColor const p = frameBuffer.getPixel(x, y);
      outChannels[0].push_back(p.getRed());
      outChannels[1].push_back(p.getGreen());
      outChannels[2].push_back(p.getBlue());
      GMANAlpha const a = frameBuffer.getAlpha(x, y);
      check(a.getRed() == 1.0f && a.getGreen() == 1.0f && a.getBlue() == 1.0f,
            "furnace: every alpha is exactly 1, the camera never escaping the closed shell");
    }
  }
  droppedOut = renderer.droppedPathCount();
}

// ---- check 1: the furnace's mean converges within 5 sigma of 3 ----
void testFurnaceMean() {
  std::vector<double> byChannel[3];
  std::size_t dropped = 0;
  renderFurnace(16, byChannel, dropped);

  check(dropped == 0, "furnace: droppedPathCount() is 0");

  char const* const channelName[3] = {"red", "green", "blue"};
  for (int c = 0; c < 3; ++c) {
    GmanMeanStderr const stat = meanStderr(byChannel[c]);
    std::printf("furnace %s channel: mean %.6f, stderr %.6f\n", channelName[c], stat.mean, stat.stderrOfMean);
    checkNear(stat.mean, kExpectedRadiance, stat.stderrOfMean, kFloor,
              std::string("furnace: the ") + channelName[c] + " channel's mean is within 5 sigma of 3");
  }
}

// ---- check 2: convergence -- N = 4 and N = 64 both converge, and
// stdev(64) <= stdev(4) / 3 ----
void testFurnaceConvergence() {
  std::vector<double> low[3];
  std::vector<double> high[3];
  std::size_t droppedLow = 0;
  std::size_t droppedHigh = 0;
  renderFurnace(4, low, droppedLow);
  renderFurnace(64, high, droppedHigh);

  GmanMeanStderr const lowStat = meanStderr(low[0]);
  GmanMeanStderr const highStat = meanStderr(high[0]);
  double const lowStdev = lowStat.stderrOfMean * std::sqrt((double)low[0].size());
  double const highStdev = highStat.stderrOfMean * std::sqrt((double)high[0].size());

  std::printf("furnace convergence: N=4 mean %.6f stdev %.6f; N=64 mean %.6f stdev %.6f\n", lowStat.mean, lowStdev,
              highStat.mean, highStdev);

  checkNear(lowStat.mean, kExpectedRadiance, lowStat.stderrOfMean, kFloor,
            "furnace convergence: N = 4's mean is within 5 sigma of 3");
  checkNear(highStat.mean, kExpectedRadiance, highStat.stderrOfMean, kFloor,
            "furnace convergence: N = 64's mean is within 5 sigma of 3");
  check(highStdev <= lowStdev / 3.0, "furnace convergence: N = 64's standard deviation is at most a third of N = 4's");
}

} // namespace

int main() {
  testFurnaceMean();
  testFurnaceConvergence();

  return checkSummary("the sphere furnace converges to rho * I / (R^2 * (1 - rho)) and tightens as samples grow");
}
