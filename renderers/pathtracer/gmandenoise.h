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

#pragma once

#include <optional>
#include <vector>

#include "gmancolor.h"
#include "gmannormal.h"
#include "ri.h"

/*
 * The path tracer's post-sampling stage: a filter reads the resolved image
 * and the guides the path tracer writes beside it, and returns the filtered
 * colour. A filter reads a GuidedImage as const, so it writes neither guide,
 * and never touches alpha or depth.
 *
 * atrousDenoise is the edge-avoiding a-trous wavelet filter of H. Dammertz,
 * D. Sewtz, J. Hanika and H. P. A. Lensch, "Edge-Avoiding A-Trous Wavelet
 * Transform for fast Global Illumination Filtering", High Performance
 * Graphics 2010, with first-hit albedo standing in for the paper's
 * world-space position guide. Albedo is scale-free in [0, 1], where a
 * position's sigma would depend on scene units.
 */

namespace gman {

// A resolved image and its guides. Pixel (x, y) is element y * width + x of
// each vector.
struct GuidedImage {
  int width = 0;
  int height = 0;
  // Resolved radiance, linear, before exposure.
  std::vector<GMANColor> color;
  // First-hit diffuse reflectance.
  std::vector<GMANColor> albedo;
  // First-hit normal, camera space, facing the camera.
  std::vector<GMANNormal> normal;
};

// The filter's parameters, named as the paper names them.
struct AtrousParameters {
  // The level count N: level i runs with a tap step of 2^i. A level whose
  // step reaches the larger image side leaves the image unchanged, so any
  // count is safe. At least 1.
  int iterations = 5;
  // sigma_rt, the colour weight's sigma at level 0; level i uses 2^-i times
  // it. Absent, the largest Euclidean norm among the image's finite pixels,
  // so the first level stops on the guides alone. Finite and positive when
  // given.
  std::optional<RtFloat> sigmaColor;
  // sigma_n, the normal weight's sigma. Finite and positive.
  RtFloat sigmaNormal = 0.5f;
  // sigma_x re-purposed: the albedo weight's sigma. Finite and positive.
  RtFloat sigmaAlbedo = 0.2f;
};

// The colour sigma atrousDenoise uses for image under parameters: the given
// sigmaColor, else the largest Euclidean norm among the finite pixels,
// saturating at the largest finite RtFloat. 0 when every finite pixel is
// black, which makes atrousDenoise return the colour unchanged. Throws what
// atrousDenoise throws for the same arguments.
RtFloat atrousColorSigma(GuidedImage const& image, AtrousParameters const& parameters);

// Filters image.color and returns the result, one element per pixel. Each
// level i = 0 ... iterations - 1 replaces every pixel by the average of its
// 25 taps, spaced 2^i pixels apart, weighted by the B3 spline
// (1/16, 1/4, 3/8, 1/4, 1/16) in each axis and by three Gaussians of squared
// distance: the colours of the current level over sigmaColor / 2^i, the
// normals over sigmaNormal and the albedos over sigmaAlbedo. A tap outside
// the image contributes nothing and the weights renormalize over the taps
// that remain. The result is a convex combination of the input colours: it
// neither clamps nor scales radiance, and a constant image is a fixed point.
//
// A pixel with a non-finite channel in its colour, albedo or normal is
// excluded: it contributes to no neighbour and its output is its input
// colour. When the colour sigma resolves to 0, the colour returns unchanged.
//
// Each level reads one buffer and writes another through gman::parallelFor
// with the given workers, so the result is bitwise identical at every worker
// count. Throws GMANError(RIE_RANGE, RIE_ERROR, ...) for fewer than 1
// iteration or a sigma that is not finite and positive, and
// GMANError(RIE_CONSISTENCY, RIE_ERROR, ...) when a vector's size differs
// from width * height.
std::vector<GMANColor> atrousDenoise(GuidedImage const& image, AtrousParameters const& parameters, RtInt workers);

} // namespace gman
