/* SPDX-License-Identifier: LGPL-2.1-or-later */

/*----------------------------------------------------------
  Copyright (C) Lionel Joseph Lacour 2000, 2001
  February 2001 First release
  ----------------------------------------------------------
  Sphere ray intersection code
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

class GMAN_EXPORT GMANRaySphere : public GMANRayInterface, public GMANSphere {
public:
  GMANRaySphere(RtFloat radius, RtFloat zmin, RtFloat zmax, RtFloat tmax, GMANParameterList pl)
      : GMANSphere(radius, zmin, zmax, tmax, pl) {}

  // Placed by transform's shutter-open matrix. A singular matrix (e.g.
  // Scale 1 1 0) cannot invert into an object space to intersect in, so
  // the sphere is built anyway and simply never hits.
  GMANRaySphere(RtFloat radius, RtFloat zmin, RtFloat zmax, RtFloat tmax, GMANParameterList pl,
                GMANTransform const& transform);

  bool intersect(const GMANRay& ray, GMANHit& hit) const;

  // The shutter-open matrix placing the sphere, for a renderer that needs
  // to reproduce its own placement (e.g. dicing it into camera space).
  GMANMatrix4 const& getObjectToCamera() const { return objectToCamera; }

private:
  // objectToCamera places the sphere; cameraToObject is its inverse, used
  // to bring a ray into the object space GMANSphere's parameters describe.
  // Both are the shutter-open matrix: motion blur is out of scope here.
  GMANMatrix4 objectToCamera;
  GMANMatrix4 cameraToObject;
  bool singular = false;
};
