/* SPDX-License-Identifier: LGPL-2.1-or-later */

/* This is part of GMAN, a RenderMan-compatible renderer.
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

#include <cmath>
#include <cstddef>

/* Local Headers */
#include "ri.h"      /* RenderMan Interface */
#include "gmanerror.h"
#include "gmanmath.h"
#include "gmanframebuffer.h"
#include "gmansamplebuffer.h" /* Declaration Header */

/*
 * RenderMan API GMANSampleBuffer
 *
 */

namespace {

// RiPixelSamples clamps each axis to [1,16], but that bounds neither
// dimension nor the frame's own resolution -- a 4096x2160 Format at
// 16x16 samples/pixel is 2,264,924,160 samples, past INT_MAX for the
// sample-space width*height multiply, and past any sane allocation at
// sizeof(GMANColor)+sizeof(RtFloat) = 16 bytes/sample (33.8GB there).
// 256Mi samples (4GB) is a generous ceiling for this renderer's scale --
// well past any realistic Format/PixelSamples combination that isn't
// already a mistake -- while still catching one.
constexpr std::size_t kMaxSamples = std::size_t(1) << 28;

} // namespace

GMANSampleBuffer::GMANSampleBuffer(int w, int h, int xs, int ys,
                                   const GMANColor &background)
  : width(w), height(h), xsamples(xs), ysamples(ys),
    sampleWidth(w * xs), sampleHeight(h * ys) {

  // size_t, not int: sampleWidth*sampleHeight overflows a signed int well
  // within realistic Format/PixelSamples inputs (see kMaxSamples' comment
  // above), and an overflowed, wrapped-negative count turned a too-large
  // request into a too-small allocation that render() then wrote past.
  const std::size_t nSamples =
      (std::size_t) sampleWidth * (std::size_t) sampleHeight;
  if (nSamples > kMaxSamples) {
    throw(GMANError(RIE_LIMIT, RIE_SEVERE,
                     "PixelSamples/Format requests more samples than this "
                     "renderer will allocate; reduce Format's resolution "
                     "or PixelSamples."));
  }
  sampleColor = new GMANColor[nSamples];
  sampleDepth = new RtFloat[nSamples];
  for (std::size_t i = 0; i < nSamples; i++) {
    sampleColor[i] = background;
    sampleDepth[i] = RI_INFINITY;
  }

  const std::size_t nPixels = (std::size_t) width * (std::size_t) height;
  resolvedDepth = new RtFloat[nPixels];
  for (std::size_t i = 0; i < nPixels; i++) {
    resolvedDepth[i] = RI_INFINITY;
  }
}

GMANSampleBuffer::~GMANSampleBuffer() {
  delete [] sampleColor;
  delete [] sampleDepth;
  delete [] resolvedDepth;
}

bool GMANSampleBuffer::zTestAndSet(int sx, int sy, RtFloat depth,
                                   const GMANColor &color) {
  const int idx = sampleIndex(sx, sy);
  if (depth < sampleDepth[idx]) {
    sampleDepth[idx] = depth;
    sampleColor[idx] = color;
    return true;
  }
  return false;
}

RtVoid GMANSampleBuffer::resolve(GMANFrameBuffer *frameBuffer,
                                 RtFilterFunc filterfunc,
                                 RtFloat xwidth, RtFloat ywidth) {
  const RtFloat xhalf = xwidth / 2.0;
  const RtFloat yhalf = ywidth / 2.0;

  for (int py = 0; py < height; py++) {
    for (int px = 0; px < width; px++) {

      // Pixel centre, in continuous pixel-space units (pixel px spans
      // [px, px+1)).
      const RtFloat cx = px + 0.5;
      const RtFloat cy = py + 0.5;

      // The sample-index range that can fall within the support box,
      // clamped to the sample grid that actually exists -- a sample past
      // the frame edge is never written, so the weighted sum below
      // renormalizes over whatever support survives there rather than
      // assuming a full box every filter width implies.
      int gxMin = (int) floor((cx - xhalf) * xsamples);
      int gxMax = (int) ceil((cx + xhalf) * xsamples) - 1;
      int gyMin = (int) floor((cy - yhalf) * ysamples);
      int gyMax = (int) ceil((cy + yhalf) * ysamples) - 1;
      gxMin = GMANMax(gxMin, 0);
      gxMax = GMANMin(gxMax, sampleWidth - 1);
      gyMin = GMANMax(gyMin, 0);
      gyMax = GMANMin(gyMax, sampleHeight - 1);

      GMANColor sum;
      RtFloat weightSum = 0.0;

      for (int gy = gyMin; gy <= gyMax; gy++) {
        // Sample j of n along an axis sits at (j+0.5)/n within its pixel;
        // gy is that sample's index in the full sample grid, so this is
        // its position in the same continuous pixel-space units as cx/cy
        // above, independent of which pixel it belongs to.
        const RtFloat sy = (gy + 0.5) / (RtFloat) ysamples;
        const RtFloat dy = sy - cy;
        if (dy < -yhalf || dy > yhalf) continue;

        for (int gx = gxMin; gx <= gxMax; gx++) {
          const RtFloat sx = (gx + 0.5) / (RtFloat) xsamples;
          const RtFloat dx = sx - cx;
          if (dx < -xhalf || dx > xhalf) continue;

          // The kernel has no notion of its own support (RiBoxFilter
          // returns 1.0 unconditionally) -- the dx/dy bounds above are
          // what give every filter a support region, not the call below.
          const RtFloat weight = filterfunc(dx, dy, xwidth, ywidth);
          GMANColor weighted = sampleColor[sampleIndex(gx, gy)];
          weighted.scale(weight);
          sum += weighted;
          weightSum += weight;
        }
      }

      if (weightSum > RI_EPSILON || weightSum < -RI_EPSILON) {
        sum /= weightSum;
      }

      // Linear interpolation and filter convolution can both overshoot
      // [0,1] (a negative-lobe kernel such as sinc, or accumulated float
      // error); the eventual float->byte conversion has no clamp of its
      // own, so an unclamped value here is UB, not just a wrong pixel.
      GMANColor clamped(GMANClamp<GMANColorSample>(sum.getRed(), 0.0, 1.0),
                        GMANClamp<GMANColorSample>(sum.getGreen(), 0.0, 1.0),
                        GMANClamp<GMANColorSample>(sum.getBlue(), 0.0, 1.0));
      frameBuffer->setPixel(px, py, clamped);

      // The resolved depth stands in for the pixel's z for
      // GMANRenderer::getDepth's existing contract -- the minimum among
      // the pixel's own samples, not the wider filter support a
      // width-2-and-up kernel reads for colour.
      RtFloat pixelMinDepth = RI_INFINITY;
      for (int j = 0; j < ysamples; j++) {
        for (int i = 0; i < xsamples; i++) {
          const int sxIdx = px * xsamples + i;
          const int syIdx = py * ysamples + j;
          pixelMinDepth = GMANMin(pixelMinDepth,
                                  sampleDepth[sampleIndex(sxIdx, syIdx)]);
        }
      }
      resolvedDepth[pixelIndex(px, py)] = pixelMinDepth;
    }
  }
}

RtFloat GMANSampleBuffer::getResolvedDepth(int x, int y) const {
  return resolvedDepth[pixelIndex(x, y)];
}
