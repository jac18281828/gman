/* SPDX-License-Identifier: LGPL-2.1-or-later */

/*----------------------------------------------------------
  Copyright (C) Lionel Joseph Lacour 2000, 2001
  February 2001 First release
  ----------------------------------------------------------
  Basic raytracer interface
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

#include "gmanprimitives.h"
#include "gmanray.h"
#include "gmanshading.h"

/*
 * A primitive a ray can hit. intersect is const and touches no shared
 * mutable state -- the same contract GMANRayBVH::nearestHit and
 * GMANRayOccluder::transmission keep -- so a caller can invoke it from
 * concurrent gman::parallelFor workers.
 */
class GMAN_EXPORT GMANRayInterface : virtual public GMANPrimitive {
public:
  virtual bool intersect(const GMANRay& ray, GMANHit& hit) const;

  // The appearance this primitive was declared under -- its surface
  // shader, active lights and Cs/Os -- so the render loop can shade any
  // hit through gman::shade without knowing the primitive's concrete type.
  gman::Appearance const& getAppearance() const { return appearance; }
  void setAppearance(gman::Appearance const& declaredAppearance) { appearance = declaredAppearance; }

private:
  gman::Appearance appearance;
};
