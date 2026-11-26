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

#include <cstddef>
#include <optional>
#include <vector>

#include "gman/rayexport.h"
#include "gmancolor.h"
#include "gmanlightsourcemgr.h"
#include "gmannormal.h"
#include "gmanpoint.h"
#include "gmanrayinterface.h"
#include "gmanvector.h"
#include "gmanworldmanager.h"
#include "ri.h"

/*
 * The renderer-facing emitter API: what radiates light, one interface a
 * path tracer's next-event estimation and a radiosity solver's own
 * shooting pass both read, public, installed and GMANRAY_EXPORT.
 *
 * Two queries only: sample draws a direction and its arriving radiance from
 * a receiving point, for next-event estimation; samplePoint draws a point
 * and its outgoing radiance on the emitter itself, for a shooting pass that
 * starts rays from the light instead. A delta light (point, spot or
 * distant) answers both through GMANLight's own sample(); an area emitter
 * -- a sphere, disk or polygon mesh carrying a pending, illuminated
 * RiAreaLightSource -- answers both by sampling its own shape.
 *
 * sample draws an area emitter uniformly in the solid angle it subtends from
 * the receiving point wherever that is the quieter draw, so a point near an
 * emitter never gathers its light from a few heavy draws: a polygon mesh by
 * spherical triangles at every distance, a full sphere in its cone at every
 * distance, and a disk or a partial sphere near it through a region that
 * holds it, keeping the draws that land on it. Beyond a distance scaled by
 * its size, a disk or a partial sphere draws uniformly by area, which is
 * quieter there.
 * lightSolidAnglePdf answers the density of whichever draw the same
 * receiving point selects. samplePoint draws every area emitter uniformly by
 * area.
 *
 * A polygon mesh, which every Polygon, GeneralPolygon, PointsPolygons and
 * PointsGeneralPolygons request builds, is one emitter. sample fans each face
 * into spherical triangles; samplePoint draws uniformly by area over its
 * faces, so its area Jacobian is 1. Each face emits one-sided along its plane
 * normal, the Newell normal of its outer loop; no RiSides, RiOrientation or
 * vertex normal changes that direction. Under a CTM of positive determinant a
 * face's vertices run clockwise seen from the side it lights: in the camera's
 * left-handed space with y up,
 * (-a, y, -b) (-a, y, b) (a, y, b) (a, y, -b) emits toward +y and the reverse
 * order toward -y. A mirroring CTM flips a polygon's emission and leaves a
 * disk's alone. A mesh with a hole, a concave face or a non-finite vertex is
 * ineligible.
 */
namespace gman {

// One thing that radiates light: a delta light (shape null) or a sphere, disk
// or polygon mesh primitive carrying an area light (shape non-null). light is
// never null -- the enumerated GMANLight itself for a delta emitter, the
// arealight's own GMANLight for an area one.
struct GMANRAY_EXPORT Emitter {
  GMANLight const* light;
  GMANRayInterface const* shape;
  // Phi, pi * mean(Le) * A_cam, A_cam the emitter's own placed area: exact
  // for a disk, whose area Jacobian is constant over its own plane, and for a
  // polygon mesh, whose area is taken in camera space with Jacobian 1; an
  // estimate for a sphere under a non-uniform placement, whose Jacobian
  // varies pointwise. power feeds only the light-choice weight, where any
  // positive weight keeps the estimator unbiased, so the estimate costs
  // variance, never correctness. 0 for a delta emitter, which has no
  // finite one.
  RtFloat power;
};

// One next-event-estimation draw: the direction toward the emitter, its
// true distance to the sampled point (RI_INFINITY for a distant light),
// the shadow walk's own end point for an area draw (off the emitter's own
// surface, toward the query point; unset and never read for a delta
// draw), the radiance arriving along wi already scaled for a delta
// emitter's own pi convention, and the density that direction was drawn
// with, per unit solid angle at the query point -- 1, by convention, for
// a delta emitter's own discrete draw. An area draw by solid angle reports
// the same density at every point it can land on under a rigid or uniformly
// scaled placement; one by area reports d^2 / (A J cos), larger the nearer
// and the more oblique the point. A draw that places nothing has pdf 0 and
// every other field zero, and counts as a zero in an estimate that divides
// by the number of draws.
struct GMANRAY_EXPORT EmitterSample {
  GMANVector wi;
  RtFloat distance;
  GMANPoint shadowTarget;
  GMANColor Cl;
  RtFloat pdf;
  bool isDelta;
};

// One shooting-pass draw: a point on the emitter, its outward normal
// (undefined for a delta emitter, which has no surface), the radiance it
// emits there (undoubled -- no pi scale, unlike EmitterSample's Cl), and
// the density that point was drawn with, per unit area for an area
// emitter.
struct GMANRAY_EXPORT EmitterPoint {
  GMANPoint point;
  GMANNormal normal;
  GMANColor Le;
  RtFloat pdf;
  bool isDelta;
};

// The union of every primitive's own Appearance::lights in world,
// deduplicated by pointer in first-seen order, ambient excluded, each
// becoming one delta Emitter; plus one area Emitter per primitive whose
// Appearance::areaLight is non-null and whose shape is an eligible,
// supported (sphere, disk or polygon mesh) primitive. A polygon request's
// mesh is one emitter; a polygon outside a mesh is unsupported, and a mesh
// holding a face with a hole, a reflex vertex or a non-finite vertex, or
// with no area, is ineligible. Counts and warns once, naming the
// count, when an Appearance::areaLight-tagged primitive is ineligible or
// unsupported. ambientCount, when non-null, receives the same walk's own
// count of distinct ambient lights -- a caller that also needs that count
// takes it from this one pass over world rather than walking it again.
GMANRAY_EXPORT std::vector<Emitter> emitters(GMANWorldManager& world, std::size_t* ambientCount = nullptr);

// The index in emitters of the first emitter a ray that hit hitPrimitive has
// met: the one whose shape is hitPrimitive itself or, when hitPrimitive is a
// polygon of a mesh, that mesh. Empty when none matches, and for a null
// hitPrimitive, which no area emitter's shape names.
GMANRAY_EXPORT std::optional<std::size_t> emitterIndex(std::vector<Emitter> const& emitters,
                                                       GMANRayInterface const* hitPrimitive);

// A next-event-estimation draw toward emitter from p. A delta emitter
// ignores u1/u2 and forwards to its own light->sample(). An area emitter
// draws as follows; a draw by solid angle lands only where the emitter faces
// p and reports Cl as its light's radiance.
//
// - A polygon mesh draws uniformly in the solid angle its emitting faces
//   subtend from p: u1 picks a face, then one of its fan triangles (v0,
//   v[i], v[i + 1]), each in proportion to its solid angle, counting only
//   triangles whose face p lies strictly in front of, and the rescaled u1 and
//   u2 draw a direction in that triangle's spherical triangle (Arvo 1995).
//   The drawn point is where that ray meets the triangle's plane, and pdf is
//   1 / the mesh's solid angle.
// - A disk whose object-space distance from p to its sector's bounding
//   rectangle is below 0.75 times the radius of a full disk of the sector's
//   area draws that rectangle, placed, as a two-triangle mesh, keeps a point
//   that maps back inside the sector and reports pdf 1 / the rectangle's
//   solid angle. Beyond that distance it draws uniformly by area.
// - A sphere, full or a zone nearer its full sphere than 1.5 times its
//   radius, draws uniformly in the cone of directions from p that meet the
//   full sphere, cos theta = 1 - u1 (1 - cos theta_max) and phi = 2 pi u2,
//   at the point where that direction first meets it; a zone keeps a point
//   within its z and theta bounds. pdf is |L w|^3 / (|det L| * the cone's
//   solid angle), L the linear part of the placement and w the object-space
//   direction, 1 / the cone's solid angle under a rigid placement. A zone
//   beyond that distance draws uniformly by area.
// - A draw by area draws a point on the shape uniformly by area, places it by
//   the shape's own object-to-camera transform, reports pdf d^2 / (A J cos)
//   and reports Cl black when that placed point emits away from p.
//
// The draw answers pdf 0 with wi, distance, shadowTarget and Cl all zero and
// isDelta false for a shape whose placement is singular, as under
// Scale 1 1 0, a polygon mesh with no area or one that subtends no solid
// angle from p, as from behind or within every face's plane, a disk seen
// from on or behind its plane, a sphere seen from on or inside it, a draw
// rejected outside a disk's sector or a zone, and a draw that is not finite.
GMANRAY_EXPORT EmitterSample sample(Emitter const& emitter, GMANPoint const& p, RtFloat u1, RtFloat u2);

// The solid-angle density sample() reports in EmitterSample::pdf for a
// freshly drawn point on emitter, generalized to a point already known --
// a BSDF-sampled bounce's own hit, which sample() never drew. p is the
// receiving point, hitPoint the known point on emitter and hitNormal its
// outward normal there (a polygon face's plane normal), all camera space.
// It answers the density of the draw sample() selects at the same p, from
// p and the emitter alone, so the two agree at every p: for a polygon mesh 1
// / the solid angle its emitting faces subtend from p, whichever face
// hitPoint lies on; for a disk near p 1 / the solid angle of its bounding
// rectangle; for a sphere drawn in its cone |L w|^3 / (|det L| * the cone's
// solid angle), w the object-space direction from p to hitPoint; and for a
// draw by area d^2 / (A J cos) at hitPoint. 0 when hitNormal's cosine to the
// direction from hitPoint back to p is at or below 0 (emitter's own
// non-emitting side) or when p and hitPoint coincide -- unlike
// EmitterSample::pdf, which a back-facing draw by area still reports
// nonzero, since that draw's own Cl already goes black on that side and no
// weight computed from either pdf ever multiplies it. 0 too when emitter's
// shape has a singular placement, as under Scale 1 1 0, is a polygon mesh
// with no area or subtending no solid angle from p, a disk seen from on or
// behind its plane or a sphere seen from on or inside it. Precondition:
// emitter.shape != nullptr (an area emitter, which alone has a surface to
// place a known point against); checked by assert.
GMANRAY_EXPORT RtFloat lightSolidAnglePdf(Emitter const& emitter, GMANPoint const& p, GMANPoint const& hitPoint,
                                          GMANNormal const& hitNormal);

// A shooting-pass draw on emitter itself, ignoring u1/u2 for a delta
// emitter; a polygon mesh draws uniformly by area with its face's plane
// normal and pdf 1 / area. An area emitter whose shape's placement is
// singular, as under Scale 1 1 0, or whose polygon mesh has no area, answers
// pdf 0 with point, normal and Le all zero and isDelta false.
GMANRAY_EXPORT EmitterPoint samplePoint(Emitter const& emitter, RtFloat u1, RtFloat u2);

// emitter's own weight for next-event estimation's light choice at p: a
// delta emitter's fixed preview draw of its own sample(); an area emitter's
// power divided by four times the greater of its placed centre's squared
// distance from p and its own squared bounding radius -- strictly positive
// wherever it contributes, unlike a fixed preview draw whose own
// object-space point may face away from p; a polygon mesh's centre and
// radius are those of its camera-space bounding box's centre and half its
// diagonal. 0 for an area emitter whose shape's placement is singular, as
// under Scale 1 1 0.
GMANRAY_EXPORT RtFloat lightChoiceWeight(Emitter const& emitter, GMANPoint const& p);

} // namespace gman
