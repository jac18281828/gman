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

#include <memory>
#include <utility>

namespace gman {

/*
 * GMAN ownership
 *
 * The one owner a primitive or a light has. A factory returns an Owned, the
 * world or the light manager takes one, and the owned object dies with its
 * owner. An empty Owned stands for a request that built nothing.
 */

// An owner of one T, destroying it when the owner goes.
template <typename T> using Owned = std::unique_ptr<T>;

// Builds a T from args and returns its owner.
template <typename T, typename... Args> Owned<T> make(Args&&... args) {
  return std::make_unique<T>(std::forward<Args>(args)...);
}

} // namespace gman
