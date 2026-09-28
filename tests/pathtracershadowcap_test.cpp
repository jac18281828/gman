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
 * The shadow walk's own composite-layer cap: gman::kMaxCompositeLayers
 * black, zero-opacity blockers stacked 0.1 apart still pass light, and one
 * more reads opaque. PATHTRACER_SHADOW_CAP_COUNT is the offset from
 * gman::kMaxCompositeLayers this build stacks: 0 or 1. Built twice into the
 * pathtracershadowcap16 and pathtracershadowcap17 executables, so a change
 * to gman::kMaxCompositeLayers moves both blocker counts with it.
 */

#ifndef PATHTRACER_SHADOW_CAP_COUNT
#error "PATHTRACER_SHADOW_CAP_COUNT must name this build's own offset, 0 or 1"
#endif

#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "check.h"
#include "gmanattributes.h"
#include "gmanframebuffer.h"
#include "gmanpoint.h"
#include "gmanrayoccluder.h"
#include "gmansurfaceshader.h"
#include "gmanvsperspective.h"
#include "pathtracershadowscene.h"
#include "ri.h"
#include "samplingstats.h"

namespace {

constexpr RtFloat kBlockerSpacing = 0.1f;

// This build's own blocker count: gman::kMaxCompositeLayers at offset 0,
// one past it at offset 1.
constexpr int kBlockerCount = gman::kMaxCompositeLayers + PATHTRACER_SHADOW_CAP_COUNT;

// Stacks kBlockerCount black, zero-opacity blockers above the floor,
// renders once under the distant light, and reports the shadowed pixels'
// mean plus the dropped-path count.
void renderCap(std::vector<double> shadowed[3], std::size_t& shadowedCount, std::size_t& droppedOut) {
  std::shared_ptr<GMANSurfaceShader const> const blockerShader = loadShader("matte", matteParams(1.0f));
  gman::Appearance blockerAppearance;
  blockerAppearance.shader = blockerShader;
  blockerAppearance.Cs = GMANColor(0.0f, 0.0f, 0.0f);
  blockerAppearance.Os = GMANColor(0.0f, 0.0f, 0.0f);

  std::vector<gman::Appearance> const appearances(kBlockerCount, blockerAppearance);
  std::vector<std::vector<GMANPoint>> verts;
  verts.reserve(kBlockerCount);
  for (int i = 0; i < kBlockerCount; ++i) {
    verts.push_back(rectAt(kBlockerY + (RtFloat)i * kBlockerSpacing));
  }

  std::unique_ptr<GMANFrameBuffer> frameBuffer;
  std::unique_ptr<gman::VSPerspective> viewingSys;
  renderScene(appearances, verts, frameBuffer, viewingSys, droppedOut);

  shadowedCount = 0;
  for (int py = 0; py < kRes; ++py) {
    for (int px = 0; px < kRes; ++px) {
      if (!pixelShadowed(*viewingSys, px, py)) {
        continue;
      }
      ++shadowedCount;
      GMANColor const p = frameBuffer->getPixel(px, py);
      shadowed[0].push_back(p.getRed());
      shadowed[1].push_back(p.getGreen());
      shadowed[2].push_back(p.getBlue());
    }
  }
}

void testCap() {
  std::vector<double> shadowed[3];
  std::size_t shadowedCount = 0;
  std::size_t dropped = 0;
  renderCap(shadowed, shadowedCount, dropped);
  check(dropped == 0, "cap: droppedPathCount() is 0");
  check(shadowedCount >= kMinShadowedPixels, "cap: at least 100 shadowed pixels");

  if (kBlockerCount <= gman::kMaxCompositeLayers) {
    char const* const channelName[3] = {"red", "green", "blue"};
    for (int c = 0; c < 3; ++c) {
      GmanMeanStderr const stat = meanStderr(shadowed[c]);
      std::printf("cap %s: mean %.6f, expected %.6f\n", channelName[c], stat.mean, kExpectedLit);
      checkNear(stat.mean, kExpectedLit, stat.stderrOfMean, 1e-4 * kExpectedLit,
                "cap: the " + std::string(channelName[c]) + " channel's mean is within 5 sigma of " +
                    std::to_string(kExpectedLit));
    }
  } else {
    for (int c = 0; c < 3; ++c) {
      GmanMeanStderr const stat = meanStderr(shadowed[c]);
      std::printf("cap channel %d: mean %.8f\n", c, stat.mean);
      check(std::fabs(stat.mean) <= 1e-6, "cap: shadowed pixels read 0 within 1e-6");
    }
  }
}

} // namespace

int main() {
  testCap();

  std::string const summary =
      "the shadow walk's composite-layer cap: " + std::to_string(kBlockerCount) + " blockers " +
      (kBlockerCount <= gman::kMaxCompositeLayers ? "still pass light" : "read opaque, past gman::kMaxCompositeLayers");
  return checkSummary(summary.c_str());
}
