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

/*
 * Render progress: one log line per whole percent of rows finished, for a
 * long render that would otherwise say nothing between its first line and
 * its last. A renderer builds a RowProgress with its own name and the row
 * total, then calls it with the number of rows finished so far. The calls
 * must come from one thread at a time, as gman::parallelFor's onIndexDone
 * guarantees.
 *
 * Each line reads "<name>: <p>% (<done>/<total> rows), <elapsed> elapsed,
 * ~<left> left" and goes out through gman::info. The time left is elapsed
 * * (total - done) / done: approximate where later rows cost more or less
 * than earlier ones, while the percent and the row counts are exact. The
 * line for the last row, at 100%, has no time left. Option "statistics"
 * "integer progress" [1] asks for them; absent or 0 is off.
 */
namespace gman {

// Whether options carry Option "statistics" "integer progress" with a nonzero
// first value.
GMANRAY_EXPORT bool progressRequested(GMANOptions const& options);

// "1h12m", "3m05s" or "42s": hours and minutes from an hour up, minutes and
// seconds from a minute up, whole seconds below.
GMANRAY_EXPORT std::string formatDuration(std::chrono::seconds duration);

// The line for done of total rows finished after elapsed, as the class
// above logs it.
GMANRAY_EXPORT std::string progressLine(std::string_view name, RtInt done, RtInt total, std::chrono::seconds elapsed);

class GMANRAY_EXPORT RowProgress {
public:
  RowProgress(std::string name, RtInt total);

  // Logs a line when done reaches a whole percent no earlier call has.
  void operator()(RtInt done);

private:
  std::string name;
  RtInt total;
  RtInt lastPercent = 0;
  std::chrono::steady_clock::time_point start;
};

} // namespace gman
