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
 * A world block with no display output -- no Display request, or one naming
 * "framebuffer" -- reports RIE_ILLSTATE, "No display output to render
 * into.", at RiWorldEnd instead of rendering into gman::OutputX11's 0x0
 * stub. The world closes on the error: a following RiFormat, legal only
 * outside a world, reports nothing.
 */

#include <string>
#include <vector>

#include "check.h"
#include "ri.h"

namespace {

struct RecordedError {
  RtInt code;
  RtInt severity;
  std::string message;
};

std::vector<RecordedError> recorded;

RtVoid recordingErrorHandler(RtInt code, RtInt severity, char const* message) {
  recorded.push_back({code, severity, message});
}

// Runs RiWorldBegin/RiWorldEnd with no display output declared -- either no
// Display at all, or an explicit framebuffer one -- inside its own
// RiBegin/RiEnd cycle, checking the reported sequence at each step.
void checkNoDisplayWorld(char* rendererName, bool declareFramebuffer) {
  recorded.clear();
  RiBegin(rendererName);
  if (declareFramebuffer) {
    char displayName[] = "worldnodisplay";
    RiDisplay(displayName, RI_FRAMEBUFFER, RI_RGB, RI_NULL);
  }
  RiWorldBegin();
  RiWorldEnd();
  check(recorded.size() == 1, "a world with no display output reports exactly one error");
  if (recorded.size() == 1) {
    check(recorded[0].code == RIE_ILLSTATE, "the code is RIE_ILLSTATE");
    check(recorded[0].severity == RIE_ERROR, "the severity is RIE_ERROR");
    check(recorded[0].message == "No display output to render into.",
          "the message is exactly \"No display output to render into.\"");
  }

  recorded.clear();
  RiFormat(8, 8, 1);
  check(recorded.empty(), "RiFormat after RiWorldEnd reports nothing, proving the world closed");

  recorded.clear();
  RiEnd();
  check(recorded.empty(), "RiEnd reports nothing");
}

} // namespace

int main(int argc, char* argv[]) {
  check(argc == 2, "argv[1] names the renderer RiBegin loads");
  if (argc != 2) {
    return checkSummary("worldnodisplay reports RIE_ILLSTATE instead of rendering");
  }

  RiErrorHandler(recordingErrorHandler);

  checkNoDisplayWorld(argv[1], false);
  checkNoDisplayWorld(argv[1], true);

  return checkSummary("worldnodisplay reports RIE_ILLSTATE instead of rendering");
}
