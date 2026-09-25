/* SPDX-License-Identifier: LGPL-2.1-or-later */

/* This is part of GMAN, a RenderMan-compatible renderer.
 *
 * Copyright (c) 2001, 2000, 1999 John Cairns
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

#include <vector>

#include <stdarg.h>

#include "ri.h"

namespace gman {

// An ellipsis Ri* call's optional token/value pairs, owned by value. n
// equals tokens.size() (== parms.size()).
struct Arguments {
  RtInt n;
  std::vector<RtToken> tokens;
  std::vector<RtPointer> parms;
};

// Walks args exactly once, reading RtToken/RtPointer pairs until RI_NULL.
// The caller's va_list is consumed by this single pass; it must not be read
// again.
extern Arguments getArguments(va_list args);

} // namespace gman
