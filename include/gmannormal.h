/* SPDX-License-Identifier: LGPL-2.1-or-later */

/* This is part of GMAN, a RenderMan-compatible renderer.
 *
 * Copyright (c) 2001, 2000, 1999  John Cairns
 *
 * Author: John Cairns <john@2ad.com>
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

#include "gman/export.h"
#include "gmanpoint.h"
#include "gmanreal.h"
#include "gmanvector.h"
#include "ri.h"

namespace gman {

// A surface normal over the real scalar T; GMANNormal names the RtFloat instance.
template <typename T> class GMAN_EXPORT Normal : public Vector<T> {
public:
  Normal();
  Normal(T x, T y, T z);
  Normal(Point<T> const& p);

  /* construct a normal from two points N = b - a */
  Normal(Point<T> const& a, Point<T> const& b) : Vector<T>(a, b) {}

  template <typename U> explicit Normal(Normal<U> const& n) : Vector<T>(n) {}

  ~Normal() {}
};

} // namespace gman

using GMANNormal = gman::Normal<RtFloat>;

extern template class gman::Normal<RtFloat>;
extern template class gman::Normal<gman::PartnerReal>;
