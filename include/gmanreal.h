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

#include <limits>
#include <type_traits>

#include "ri.h"

namespace gman {

// The real scalar a geometry template instantiates beside RtFloat: double, or
// float when RtFloat is double. Each template over a real scalar is
// instantiated for RtFloat and PartnerReal, so a build with any RtFloat
// compiles and no type is instantiated twice.
using PartnerReal = std::conditional_t<std::is_same_v<RtFloat, double>, float, double>;

// The wider of RtFloat and double. Code that needs more precision than RtFloat
// computes in WideReal, never in a spelled double: under an RtFloat wider than
// double, a double computation would be a bug. WideReal is RtFloat or double,
// so it is always one of the two instantiated scalars.
using WideReal =
    std::conditional_t<(std::numeric_limits<RtFloat>::digits >= std::numeric_limits<double>::digits), RtFloat, double>;

} // namespace gman
