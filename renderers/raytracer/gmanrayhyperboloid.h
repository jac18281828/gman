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

#include "gmanrayinterface.h"
#include "gmantransform.h"

class GMAN_EXPORT GMANRayHyperboloid : public GMANRayInterface, public GMANHyperboloid {
public:
  GMANRayHyperboloid(RtPoint point1, RtPoint point2, RtFloat thetamax, GMANParameterList pl)
      : GMANHyperboloid(point1, point2, thetamax, pl) {}

  // Placed by transform's shutter-open matrix, GMANRaySphere's own pattern:
  // a singular matrix cannot invert into an object space to intersect in,
  // so the hyperboloid is built anyway and simply never hits.
  GMANRayHyperboloid(RtPoint point1, RtPoint point2, RtFloat thetamax, GMANParameterList pl,
                     GMANTransform const& transform);

  bool intersect(const GMANRay& ray, GMANHit& hit) const;

private:
  GMANMatrix4 objectToCamera;
  GMANMatrix4 cameraToObject;
  bool singular = false;
};
