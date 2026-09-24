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

class GMAN_EXPORT GMANRayDisk : public GMANRayInterface, public GMANDisk {
public:
  GMANRayDisk(RtFloat height, RtFloat radius, RtFloat thetamax, GMANParameterList pl)
      : GMANDisk(height, radius, thetamax, pl) {}

  // Placed by transform's shutter-open matrix, GMANRaySphere's own pattern:
  // a singular matrix cannot invert into an object space to intersect in,
  // so the disk is built anyway and simply never hits.
  GMANRayDisk(RtFloat height, RtFloat radius, RtFloat thetamax, GMANParameterList pl, GMANTransform const& transform);

  bool intersect(const GMANRay& ray, GMANHit& hit) const;

  // The shutter-open matrix placing the disk, for a renderer that needs
  // to reproduce its own placement (e.g. dicing it into camera space).
  GMANMatrix4 const& getObjectToCamera() const { return objectToCamera; }

private:
  GMANMatrix4 objectToCamera;
  GMANMatrix4 cameraToObject;
  bool singular = false;
};
