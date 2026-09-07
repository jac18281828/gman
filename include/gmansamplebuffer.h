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


#ifndef __GMAN_GMANSAMPLEBUFFER_H
#define __GMAN_GMANSAMPLEBUFFER_H 1


/* Headers */

// the renderman interface
#include "ri.h"
// the color object
#include "gmancolor.h"

class GMAN_EXPORT GMANFrameBuffer;

/*
 * RenderMan API GMANSampleBuffer
 *
 * Holds colour and depth at sample resolution -- xsamples*ysamples
 * samples per output pixel -- and resolves them into a GMANFrameBuffer
 * through an RtFilterFunc. A renderer feeds it samples; it does not know
 * about polygons, scanlines or any particular rasterizer, so a REYES
 * micropolygon sampler can reuse it unchanged.
 *
 */

class GMAN_EXPORT GMANSampleBuffer {
public:
  // width/height are the output image's pixel resolution; xsamples/
  // ysamples are samples per pixel along each axis. Every sample starts
  // at background colour and RI_INFINITY depth, so an uncovered pixel
  // resolves to background rather than to indeterminate or black.
  GMANSampleBuffer(int width, int height, int xsamples, int ysamples,
                    const GMANColor &background);
  ~GMANSampleBuffer();

  GMANSampleBuffer(const GMANSampleBuffer &) = delete;
  GMANSampleBuffer &operator=(const GMANSampleBuffer &) = delete;
  GMANSampleBuffer(GMANSampleBuffer &&) = delete;
  GMANSampleBuffer &operator=(GMANSampleBuffer &&) = delete;

  int getSampleWidth(void) const { return sampleWidth; }
  int getSampleHeight(void) const { return sampleHeight; }

  // The per-sample visibility test: if depth is closer than the sample's
  // current depth, stores color and depth and returns true. sx/sy address
  // the full sample grid (pixel index * samples-per-pixel + local sample
  // index), not a single pixel's own samples.
  bool zTestAndSet(int sx, int sy, RtFloat depth, const GMANColor &color);

  // Filters every pixel's covering samples through filterfunc and writes
  // the result to frameBuffer. xwidth/ywidth are the filter's full support
  // width in pixel units, symmetric about the pixel centre being resolved
  // (the RISpec convention every kernel in gmanfilters.cpp already
  // follows). Also records, per pixel, the minimum depth among that
  // pixel's own samples -- retrieve with getResolvedDepth after this
  // returns.
  RtVoid resolve(GMANFrameBuffer *frameBuffer, RtFilterFunc filterfunc,
                 RtFloat xwidth, RtFloat ywidth);

  RtFloat getResolvedDepth(int x, int y) const;

private:
  int width, height;              // output image, pixel resolution
  int xsamples, ysamples;         // samples per pixel, per axis
  int sampleWidth, sampleHeight;  // width*xsamples, height*ysamples

  GMANColor *sampleColor; // sampleWidth*sampleHeight
  RtFloat   *sampleDepth; // sampleWidth*sampleHeight
  RtFloat   *resolvedDepth; // width*height, filled by resolve()

  inline int sampleIndex(int sx, int sy) const {
    return sy * sampleWidth + sx;
  }
  inline int pixelIndex(int x, int y) const {
    return y * width + x;
  }
};

#endif
