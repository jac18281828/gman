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

namespace gman {

// Solves a*t^4 + b*t^3 + c*t^2 + d*t + e == 0 for real t, a != 0 a
// precondition -- a zero-length ray direction is the only way a caller
// reaches a == 0, and should check for it before calling. Closed form
// (Ferrari, through the resolvent cubic) in double; the resolvent's own
// root is itself Newton-refined against its cubic, avoiding the
// catastrophic cancellation a small root suffers when read off the
// depressed cubic's solution directly. Returns the count of real roots
// (0-4) and writes them ascending in roots; a repeated root may be
// reported once or twice, and the caller treats both the same.
int solveQuartic(double a, double b, double c, double d, double e, double roots[4]);

} // namespace gman
