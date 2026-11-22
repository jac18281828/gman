/* SPDX-License-Identifier: LGPL-2.1-or-later */

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

#include <optional>

#include "gmanerror.h"
#include "gmanmatrix4.h"
#include "gmanpoint.h"
#include "gmanvector.h"
#include "ri.h"

namespace gman {

// A placement and its inverse, computed once and carried together.
// backward() is forward()'s inverse; of returns empty for a matrix with
// no inverse, so a held pair is always usable. A matrix applies as a row
// vector, p * m (AGENTS.md's CTM convention).
class InvertibleMatrix {
public:
  // The identity, its own inverse.
  InvertibleMatrix() = default;

  // Inverts forward once. Empty when GMANMatrix4::invert throws, that is,
  // when forward's determinant is exactly 0. Throws nothing.
  static std::optional<InvertibleMatrix> of(GMANMatrix4 const& forward) {
    GMANMatrix4 backward = forward;
    try {
      backward.invert();
    } catch (GMANError const&) {
      return std::nullopt;
    }
    return InvertibleMatrix(forward, backward);
  }

  GMANMatrix4 const& forward() const { return forwardMatrix; }
  GMANMatrix4 const& backward() const { return backwardMatrix; }

  // The same pair with its halves swapped: no arithmetic.
  InvertibleMatrix inverse() const { return InvertibleMatrix(backwardMatrix, forwardMatrix); }

private:
  InvertibleMatrix(GMANMatrix4 const& forwardHalf, GMANMatrix4 const& backwardHalf)
      : forwardMatrix(forwardHalf), backwardMatrix(backwardHalf) {}

  GMANMatrix4 forwardMatrix;
  GMANMatrix4 backwardMatrix;
};

// m.forward() applied to a point. A point moving the other way passes
// m.backward() to the GMANMatrix4 form of transformPoint.
inline GMANPoint transformPoint(InvertibleMatrix const& m, GMANPoint const& p) {
  return transformPoint(m.forward(), p);
}

// m.forward() applied to a direction. A direction moving the other way
// passes m.backward() to the GMANMatrix4 form of transformDirection.
inline GMANVector transformDirection(InvertibleMatrix const& m, GMANVector const& v) {
  return transformDirection(m.forward(), v);
}

// A normal transforms by the inverse transpose of the point transform:
// n by m.forward()'s inverse transpose, which is m.backward()'s transpose.
// A normal moving the other way passes m.inverse().
inline GMANVector transformNormal(InvertibleMatrix const& m, GMANVector const& n) {
  GMANMatrix4 const& b = m.backward();
  RtFloat const x = n.getX(), y = n.getY(), z = n.getZ();
  return GMANVector(b[0][0] * x + b[0][1] * y + b[0][2] * z, b[1][0] * x + b[1][1] * y + b[1][2] * z,
                    b[2][0] * x + b[2][1] * y + b[2][2] * z);
}

} // namespace gman
