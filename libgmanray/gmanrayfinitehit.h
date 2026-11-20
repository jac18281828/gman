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

#include <cmath>

#include "gmanpoint.h"
#include "gmanray.h"
#include "gmanvector.h"
#include "ri.h"

namespace gman {

inline bool isFiniteHit(GMANHit const& hit) {
  RtFloat const fields[] = {hit.t,
                            hit.point.getX(),
                            hit.point.getY(),
                            hit.point.getZ(),
                            hit.normal.getX(),
                            hit.normal.getY(),
                            hit.normal.getZ(),
                            hit.u,
                            hit.v,
                            hit.dPdu.getX(),
                            hit.dPdu.getY(),
                            hit.dPdu.getZ(),
                            hit.dPdv.getX(),
                            hit.dPdv.getY(),
                            hit.dPdv.getZ()};
  for (RtFloat const field : fields) {
    if (!std::isfinite(field)) {
      return false;
    }
  }
  return true;
}

// Copies candidate into hit only when every field the intersector fills in is
// finite; otherwise leaves hit alone and returns false.
inline bool acceptFiniteHit(GMANHit const& candidate, GMANHit& hit) {
  if (!isFiniteHit(candidate)) {
    return false;
  }
  hit = candidate;
  return true;
}

} // namespace gman
