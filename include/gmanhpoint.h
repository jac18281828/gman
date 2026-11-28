/* SPDX-License-Identifier: LGPL-2.1-or-later */

/*---------------------------------------------------------
  Copyright (C) Lionel Joseph Lacour 2000, 2001
  February 2001  First release
  ---------------------------------------------------------
  Homogeneous coordinate point.
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

#include <type_traits>

#include "gman/export.h"
#include "gmanmatrix4.h"
#include "gmanpoint.h"
#include "gmanreal.h"
#include "ri.h"

namespace gman {

// A homogeneous point over the real scalar T; GMANHPoint names the instance for the RenderMan scalar.
template <typename T> class GMAN_EXPORT HPoint : public Point<T> {
protected:
  T w;

public:
  HPoint() : Point<T>(0, 0, 0), w(1.0) {}

  HPoint(T x, T y, T z, T wv) : Point<T>(x, y, z), w(wv) {}

  template <typename U>
    requires std::is_arithmetic_v<U>
  explicit HPoint(U const* f) : Point<T>(f), w(static_cast<T>(f[3])) {}

  template <typename U> explicit HPoint(HPoint<U> const& p) : Point<T>(p), w(static_cast<T>(p.getW())) {}

  T getW() const { return w; };
  void setW(T wv) { w = wv; };

  void wToOne();

  HPoint operator+(HPoint const& p) const;
  HPoint operator*(T f) const;
  HPoint operator*(Matrix4<T> const& m) const;

  HPoint& operator+=(HPoint const& p);
  HPoint& operator*=(T f);
  HPoint& operator*=(Matrix4<T> const& m);
};

} // namespace gman

using GMANHPoint = gman::HPoint<RtFloat>;

extern template class gman::HPoint<RtFloat>;
extern template class gman::HPoint<gman::PartnerReal>;
