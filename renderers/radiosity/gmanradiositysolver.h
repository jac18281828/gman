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

#include <array>
#include <cstddef>
#include <vector>

#include "gmancolor.h"
#include "gmanpoint.h"
#include "gmanradiositymesh.h"
#include "gmanrayoccluder.h"
#include "gmanvector.h"

// An element's two faces: front along its normal, back against it.
enum class GMANRadiositySide : std::size_t { front = 0, back = 1 };

namespace gman {

// Where an element receives light: surfacePoint at its cell's centre, or,
// for a clipped polygon cell whose centre lies outside, the element's
// centre and plane normal. N agrees in sign with the element's normal;
// surfaceMagnitude is gman::primitiveMagnitude of its primitive.
struct RadiosityReceiver {
  GMANPoint P;
  GMANVector N;
  RtFloat surfaceMagnitude = 0;
};

// One form-factor sample of a shooter: a true-surface point, its normal
// (sign-agreed with the element's) and the share of the element's true
// area it stands for.
struct RadiositySample {
  GMANPoint P;
  GMANVector N;
  RtFloat weight = 0;
};

RadiosityReceiver radiosityReceiver(GMANRadiosityMesh const& mesh, std::size_t element);

// An element's true area A_i: a polygon element's clipped area, or a
// quadric element's mean surfacePoint density over the centres of a
// sqrt(samples) x sqrt(samples) subgrid of its cell, the midpoint rule.
// samples must be a perfect square with an even root; GMANError
// otherwise.
RtFloat radiosityElementArea(GMANRadiosityMesh const& mesh, std::size_t element, std::size_t samples);

// A shooter's samples at the same subgrid centres, those on the surface
// sharing area in proportion to their densities. With none on the
// surface, the element's receiver point stands alone with all of area.
std::vector<RadiositySample> placeRadiositySamples(GMANRadiosityMesh const& mesh, std::size_t element, RtFloat area,
                                                   std::size_t samples);

// F(receiver <- shooter), per channel, indexed by the receiver side each
// sample arrives on (GMANRadiositySide). Only samples whose segment to
// the receiver leaves the shooter on shootingSide contribute.
std::array<GMANColor, 2> radiosityFormFactors(GMANRayOccluder const& occluder, RadiosityReceiver const& receiver,
                                              std::vector<RadiositySample> const& shooter,
                                              GMANRadiositySide shootingSide);

} // namespace gman

// A solve's answer, per element side and per node side, in the shading
// units GMANRadiositySolver documents.
class GMANRadiositySolution {
public:
  std::size_t getElementCount() const { return areas.size(); }
  std::size_t getNodeCount() const { return nodeIndirect.size() / 2; }

  // A_i, the element's true area.
  RtFloat getArea(std::size_t element) const { return areas[element]; }

  // H_d, H and H_ind = H - H_d.
  GMANColor const& getDirect(std::size_t element, GMANRadiositySide side) const {
    return direct[sideIndex(element, side)];
  }
  GMANColor const& getIrradiance(std::size_t element, GMANRadiositySide side) const {
    return irradiance[sideIndex(element, side)];
  }
  GMANColor const& getIndirect(std::size_t element, GMANRadiositySide side) const {
    return indirect[sideIndex(element, side)];
  }

  // The A_i-weighted mean H_ind of that side over every element touching
  // any node in the node's sameNode group.
  GMANColor const& getNodeIndirect(std::size_t node, GMANRadiositySide side) const {
    return nodeIndirect[sideIndex(node, side)];
  }

  std::size_t getShotCount() const { return shotCount; }

  // True when the shot cap, not convergence, ended the solve.
  bool reachedShotCap() const { return shotCapReached; }

private:
  friend class GMANRadiositySolver;

  static std::size_t sideIndex(std::size_t index, GMANRadiositySide side) {
    return 2 * index + static_cast<std::size_t>(side);
  }

  std::vector<RtFloat> areas;
  std::vector<GMANColor> direct;
  std::vector<GMANColor> irradiance;
  std::vector<GMANColor> indirect;
  std::vector<GMANColor> nodeIndirect;
  std::size_t shotCount = 0;
  bool shotCapReached = false;
};

/*
 * Diffuse interreflection over a GMANRadiosityMesh by progressive
 * refinement (Cohen, Chen, Wallace & Greenberg 1988), reporting only the
 * indirect light, since the ray tracer computes direct light itself.
 *
 * Units are the ray tracer's shading units. H, a side's incident shading
 * irradiance, is what GMANSurfaceEnv::diffuse returns and a shader scales
 * by Kd * Cs; rho is a side's diffuse reflectance per channel; B = rho * H
 * is its radiosity. A surface of radiosity B adds B * F to a receiver's
 * H, F the point-to-area form factor. In a closed enclosure of uniform rho
 * and direct H_d, H = H_d / (1 - rho).
 *
 * Every element is two-sided: front along its normal, back against it,
 * each with its own H_d, H, B and unshot radiosity. A side's H_d is
 * diffuse(+N) or diffuse(-N) at its receiver point, through the element
 * primitive's own lights and the occluder, exactly the direct term the
 * ray tracer computes there. Receiver points and form-factor samples lie
 * on the true surface, never inside a curved primitive's facet.
 *
 * F(receiver <- shooter) sums, over the shooter's samples, a_k |cos_r|
 * |cos_s| T / (pi r^2 + a_k) (Wallace, Elmquist & Haines 1989, the disc
 * approximation), a_k the sample's share of the shooter's true area A_i.
 * A sample counts only when its segment leaves the shooter on the
 * shooting side, and it deposits on whichever receiver side the segment
 * arrives at. A quadric element also shoots to itself; a polygon element
 * does not. Form factors are recomputed at every shot: nothing is kept
 * between shots.
 *
 * T is GMANRayOccluder::transmission from the receiver point toward the
 * sample, so every surface crossed attenuates by 1 - Os per channel. The
 * walk stops short of the shooter's own surface by max(1e-4 r, 3e-5
 * max|P|), the relative term keeping a flat shooter's plane out of it and
 * the floor matching the occluder's own self-shadow offset ceiling.
 *
 * Each shot takes the side of largest unshot power, max-channel(unshot) *
 * A_i, ties to the lowest element and then front, zeroes its unshot and
 * adds U * F to every receiver side's H and rho * U * F to its B and
 * unshot. The solve stops once total unshot power falls to kConvergence
 * of the total direct power, sum max-channel(rho H_d) A_i, which in a
 * closed enclosure bounds the relative error left in H_ind; or after
 * kMaxShotsPerSide shots per side in total, reported by reachedShotCap.
 * With no direct power it shoots nothing, and every H_ind is 0.
 *
 * The result, a GMANRadiositySolution, holds each element's A_i, H_d, H
 * and H_ind = H - H_d per side, and each node's H_ind per side: the
 * A_i-weighted mean over every element touching the node's sameNode
 * group, so a closed quadric's seam and poles carry one value while
 * separate primitives, and separate faces of one mesh, keep their own.
 *
 * The solve is serial and deterministic: it depends only on the mesh's
 * element and node order. GMANError on a reflectance count other than the
 * mesh's element count, a channel outside [0, 1], or a sample count that
 * is not a perfect square with an even root.
 */
class GMANRadiositySolver {
public:
  // An even root keeps every sample off the receiver point at its cell's
  // centre, so a self ray never has zero length.
  static constexpr std::size_t kDefaultSamples = 16;
  static constexpr double kConvergence = 1e-3;
  static constexpr std::size_t kMaxShotsPerSide = 64;

  GMANRadiositySolution solve(GMANRadiosityMesh const& mesh, GMANRayOccluder const& occluder,
                              std::vector<GMANColor> const& reflectance, std::size_t samples = kDefaultSamples) const;

private:
  // A fresh result sized for elementCount elements and nodeCount nodes,
  // every accumulator zeroed.
  static GMANRadiositySolution makeEmptySolution(std::size_t elementCount, std::size_t nodeCount);

  // Every element's receiver point, area and self-shoot flag, and each
  // side's direct irradiance from the ray tracer's own diffuse(). Tracks
  // the largest surfaceMagnitude seen, for the node-grouping search's own
  // coordinate extent.
  static void initializeReceivers(GMANRadiosityMesh const& mesh, GMANRayOccluder const& occluder, std::size_t samples,
                                  GMANRadiositySolution& solution, std::vector<gman::RadiosityReceiver>& receivers,
                                  std::vector<bool>& shootsItself, double& largestMagnitude);

  // Fires shooterSide's shot at every receiver element (the shooter itself
  // included when it shoots to itself), adding dH to each receiver side's
  // irradiance and rho * dH to its unshot.
  static void fireShot(GMANRadiosityMesh const& mesh, GMANRayOccluder const& occluder,
                       std::vector<gman::RadiosityReceiver> const& receivers, std::vector<bool> const& shootsItself,
                       std::vector<GMANColor> const& reflectance, std::size_t samples, std::size_t shooterSide,
                       GMANColor const& shot, GMANRadiositySolution& solution, std::vector<GMANColor>& unshot);

  // H_ind = H - H_d per side, and each node side's A_i-weighted mean
  // H_ind, once the solve is complete.
  static void finalizeIndirect(GMANRadiosityMesh const& mesh, double largestMagnitude, GMANRadiositySolution& solution);
};
