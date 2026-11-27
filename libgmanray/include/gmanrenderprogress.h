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

#include <chrono>
#include <string>
#include <string_view>

#include "gman/rayexport.h"
#include "gmanoptions.h"
#include "ri.h"

namespace gman {

GMANRAY_EXPORT bool progressRequested(GMANOptions const& options);

std::string formatDuration(std::chrono::seconds duration);

std::string progressLine(std::string_view name, RtInt done, RtInt total, std::chrono::seconds elapsed);

class GMANRAY_EXPORT RowProgress {
public:
  RowProgress(std::string name, RtInt total);

  void operator()(RtInt done);

private:
  std::string name;
  RtInt total;
  RtInt lastPercent = 0;
  std::chrono::steady_clock::time_point start;
};

} // namespace gman
