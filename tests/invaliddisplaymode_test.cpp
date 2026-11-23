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

/*
 * RiWorldEnd rejects a display mode it does not know with RIE_ILLSTATE,
 * "Invalid token passed for display mode.", and closes the world on the
 * error: a following RiFormat, legal only outside a world, reports nothing.
 * The test drives the impl directly, as rendermanpointers_test.cpp does.
 */

#include <string>

#include "check.h"
#include "gmanerror.h"
#include "gmanrendermanimpl.h"
#include "ri.h"

namespace {

struct Raised {
  bool raised = false;
  RtInt code = 0;
  RtInt severity = 0;
  std::string message;
};

template <typename Request> Raised raisedBy(Request request) {
  try {
    request();
  } catch (GMANError const& error) {
    return {true, error.getCode(), error.getSeverity(), error.getMessage()};
  }
  return {};
}

} // namespace

int main() {
  GMANRenderManImpl impl;
  impl.RiBegin(RI_NULL);
  impl.RiFormat(8, 8, 1);

  char displayName[] = "invaliddisplaymode.tif";
  impl.RiDisplayV(displayName, "file", "rgbq", 0, NULL, NULL);
  impl.RiWorldBegin();

  Raised const worldEnd = raisedBy([&impl] { impl.RiWorldEnd(); });
  check(worldEnd.raised, "RiWorldEnd() raises for an unknown display mode");
  check(worldEnd.code == RIE_ILLSTATE, "the code is RIE_ILLSTATE");
  check(worldEnd.severity == RIE_ERROR, "the severity is RIE_ERROR");
  check(worldEnd.message == "Invalid token passed for display mode.",
        "the message is exactly \"Invalid token passed for display mode.\"");

  check(!raisedBy([&impl] { impl.RiFormat(8, 8, 1); }).raised,
        "RiFormat after RiWorldEnd reports nothing, proving the world closed");
  check(!raisedBy([&impl] { impl.RiEnd(); }).raised, "RiEnd reports nothing");

  return checkSummary("RiWorldEnd closes the world when it rejects the display mode");
}
