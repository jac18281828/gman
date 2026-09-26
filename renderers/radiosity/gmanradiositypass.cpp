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
#include <vector>

#include "gmanerror.h"
#include "gmanlog.h"
#include "gmanmath.h"
#include "gmanradiositypass.h"
#include "gmanraybbox.h"
#include "gmanrayinterface.h"
#include "gmanshading.h"
#include "ri.h"

namespace {

// The default element size divides the world's own largest bounding-box
// side by this many divisions -- a scene-relative default, since an
// absolute size would dice a large scene up to the mesh's own division
// cap and the solve's cost grows with the square of the element count.
// Eight gives a box wall about 8 x 8 elements.
constexpr std::size_t kDefaultElementDivisions = 8;

bool isFiniteBBoxCoordinate(RtFloat coord) { return std::fabs(coord) < RI_INFINITY; }

// True when none of bbox's six coordinates sits at +/-RI_INFINITY: the
// world's own default element size only counts a primitive whose extent
// is real.
bool isFiniteBBox(GMANBBox const& bbox) {
  GMANPoint const lo = bbox.getMin();
  GMANPoint const hi = bbox.getMax();
  RtFloat const coords[6] = {lo.getX(), lo.getY(), lo.getZ(), hi.getX(), hi.getY(), hi.getZ()};
  for (RtFloat const coord : coords) {
    if (!isFiniteBBoxCoordinate(coord)) {
      return false;
    }
  }
  return true;
}

// The largest side of the box bounding every finite-bounded primitive in
// world, divided by kDefaultElementDivisions; 0 with no such primitive.
RtFloat defaultElementSize(GMANWorldManager& world) {
  bool any = false;
  GMANPoint boxMin;
  GMANPoint boxMax;
  for (GMANPrimitive* primitive = world.getFirst(); primitive != nullptr; primitive = world.getNext()) {
    auto const* rayPrimitive = dynamic_cast<GMANRayInterface const*>(primitive);
    if (rayPrimitive == nullptr || !isFiniteBBox(rayPrimitive->getBBox())) {
      continue;
    }
    GMANPoint const lo = rayPrimitive->getBBox().getMin();
    GMANPoint const hi = rayPrimitive->getBBox().getMax();
    if (!any) {
      boxMin = lo;
      boxMax = hi;
      any = true;
    } else {
      boxMin = gman::pointMin(boxMin, lo);
      boxMax = gman::pointMax(boxMax, hi);
    }
  }
  if (!any) {
    return 0;
  }
  RtFloat const dx = boxMax.getX() - boxMin.getX();
  RtFloat const dy = boxMax.getY() - boxMin.getY();
  RtFloat const dz = boxMax.getZ() - boxMin.getZ();
  RtFloat const largest = GMANMax(GMANMax(dx, dy), dz);
  return largest / (RtFloat)kDefaultElementDivisions;
}

// options' own element size when set, defaultElementSize(world)
// otherwise -- 0 either from an empty world or one whose every primitive
// has an infinite bbox.
RtFloat resolveElementSize(GMANWorldManager& world, GMANOptions const& options) {
  RtFloat const configured = options.getRadiosityElementSize();
  return configured > (RtFloat)0 ? configured : defaultElementSize(world);
}

// The SurfacePoint the ray tracer would shade at element's own receiver:
// a cast toward gman::radiosityReceiver's own point, then toward each of
// the element's form-factor samples in turn, then a synthetic hit at the
// receiver with u = v = 0 when every cast misses. Each cast is tested
// against the element's own primitive alone, through its own intersect,
// along -N from just off the surface on N's side.
gman::SurfacePoint elementSurfacePoint(GMANRadiosityMesh const& mesh, std::size_t element) {
  GMANRayInterface const* primitive = mesh.getElementPrimitive(element);
  gman::RadiosityReceiver const receiver = gman::radiosityReceiver(mesh, element);

  GMANRay const receiverRay(gman::offsetOrigin(receiver.P, receiver.N, receiver.N, receiver.surfaceMagnitude),
                            -receiver.N);
  GMANHit hit;
  if (primitive->intersect(receiverRay, hit)) {
    return gman::hitSurfacePoint(receiverRay, hit);
  }

  RtFloat const area = gman::radiosityElementArea(mesh, element, GMANRadiositySolver::kDefaultSamples);
  std::vector<gman::RadiositySample> const samples =
      gman::placeRadiositySamples(mesh, element, area, GMANRadiositySolver::kDefaultSamples);
  for (gman::RadiositySample const& sample : samples) {
    GMANRay const sampleRay(gman::offsetOrigin(sample.P, sample.N, sample.N, receiver.surfaceMagnitude), -sample.N);
    if (primitive->intersect(sampleRay, hit)) {
      return gman::hitSurfacePoint(sampleRay, hit);
    }
  }

  GMANHit fallback;
  fallback.point = receiver.P;
  fallback.normal = receiver.N;
  fallback.primitive = primitive;
  return gman::hitSurfacePoint(receiverRay, fallback);
}

// Every element's own reflectance: gman::albedo at elementSurfacePoint,
// through the element primitive's own appearance, unclamped -- the
// albedo contract's own bound, so a shader breaking it surfaces through
// GMANRadiositySolver::solve's own reflectance check rather than being
// silently clipped here.
std::vector<GMANColor> elementReflectance(GMANRadiosityMesh const& mesh, GMANMatrix4 const& cameraToWorld) {
  std::vector<GMANColor> reflectance(mesh.getElementCount());
  for (std::size_t element = 0; element < reflectance.size(); ++element) {
    gman::Appearance const& appearance = mesh.getElementPrimitive(element)->getAppearance();
    gman::SurfacePoint const point = elementSurfacePoint(mesh, element);
    reflectance[element] = gman::albedo(appearance, point, cameraToWorld);
  }
  return reflectance;
}

} // namespace

void GMANRadiosityPass::prepare(GMANWorldManager& world, GMANRayOccluder const& occluder, GMANOptions const& options) {
  mesh = GMANRadiosityMesh();
  solution = GMANRadiositySolution();
  ready = false;

  RtFloat const elementSize = resolveElementSize(world, options);
  if (elementSize <= (RtFloat)0) {
    return;
  }

  try {
    mesh.build(world, elementSize);
    std::vector<GMANColor> const reflectance = elementReflectance(mesh, options.getCameraToWorld());
    solution = GMANRadiositySolver().solve(mesh, occluder, reflectance);
    ready = true;
  } catch (GMANError const& error) {
    warning("Radiosity: {}; the frame renders with no indirect light.", error.getMessage());
    return;
  }

  if (mesh.getSkippedCount() > 0) {
    warning("Radiosity: {} primitive(s) could not be diced and contribute no indirect light.", mesh.getSkippedCount());
  }
  if (mesh.getCappedCount() > 0) {
    warning("Radiosity: Option \"radiosity\" \"float elementsize\" reached the mesh's division cap for {} "
            "primitive(s); the finest resolution available was used instead.",
            mesh.getCappedCount());
  }
  if (solution.reachedShotCap()) {
    warning("Radiosity: the solve reached its shot cap before converging; the indirect answer may be inaccurate.");
  }
}

GMANColor GMANRadiosityPass::irradiance(GMANHit const& hit, GMANVector const& I) const {
  GMANColor const black;
  if (!ready) {
    return black;
  }

  GMANRadiosityLocation location;
  if (!mesh.locate(hit, location)) {
    return black;
  }

  RtFloat const facing = mesh.getElement(location.element).normal.dot(I);
  GMANRadiositySide const side = facing <= (RtFloat)0 ? GMANRadiositySide::front : GMANRadiositySide::back;

  GMANColor sum;
  for (std::size_t corner = 0; corner < location.corners.size(); ++corner) {
    GMANColor term = solution.getNodeIndirect(location.corners[corner], side);
    term.scale(location.weights[corner]);
    sum += term;
  }
  return sum;
}
