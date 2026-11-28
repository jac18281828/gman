/* SPDX-License-Identifier: LGPL-2.1-or-later
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
 * Each world block is its own world: GMANRenderManImpl hands the renderer a
 * frozen world holding the primitives of that block alone, however many
 * blocks the session holds. A primitive requested outside a world block
 * reports RIE_ILLSTATE at RIE_WARNING, throws nothing and joins no world.
 * Driven through a probe renderer plugin that records, per render() call,
 * whether its world was frozen and how many primitives it held. A world block
 * whose RiWorldBegin fails stays open: a primitive still adds to its world.
 */

#include <string>
#include <vector>

#include <dlfcn.h>

#include "check.h"
#include "gmanerror.h"
#include "gmanrendermanimpl.h"
#include "ri.h"

namespace {

std::string const ignoredMessage = "Invalid mode for procedure; the request is ignored.";

struct RecordedError {
  RtInt code;
  RtInt severity;
  std::string message;
};

std::vector<RecordedError> recorded;

RtVoid recordingErrorHandler(RtInt code, RtInt severity, char const* message) {
  recorded.push_back({code, severity, message});
}

struct Probe {
  int (*renderCount)() = nullptr;
  int (*frozen)(int) = nullptr;
  int (*primitiveCount)(int) = nullptr;
};

Probe openProbe() {
  void* const handle = dlopen("libworldprobe.so", RTLD_LAZY);
  Probe probe;
  if (handle == nullptr) {
    return probe;
  }
  probe.renderCount = reinterpret_cast<int (*)()>(dlsym(handle, "GMANWorldProbeRenderCount"));
  probe.frozen = reinterpret_cast<int (*)(int)>(dlsym(handle, "GMANWorldProbeFrozen"));
  probe.primitiveCount = reinterpret_cast<int (*)(int)>(dlsym(handle, "GMANWorldProbePrimitiveCount"));
  return probe;
}

bool complete(Probe const& probe) {
  return probe.renderCount != nullptr && probe.frozen != nullptr && probe.primitiveCount != nullptr;
}

void beginSession(GMANRenderManImpl& renderMan, std::string displayName) {
  renderMan.RiBegin("worldprobe");
  renderMan.RiFormat(8, 8, 1.0f);
  renderMan.RiDisplayV(displayName.data(), RI_FILE, RI_RGBA, 0, nullptr, nullptr);
}

void sphere(GMANRenderManImpl& renderMan) { renderMan.RiSphereV(1.0f, -1.0f, 1.0f, 360.0f, 0, nullptr, nullptr); }

void worldBlockOfSpheres(GMANRenderManImpl& renderMan, int spheres) {
  renderMan.RiWorldBegin();
  for (int i = 0; i < spheres; ++i) {
    sphere(renderMan);
  }
  renderMan.RiWorldEnd();
}

void testEachBlockIsItsOwnWorld() {
  GMANRenderManImpl renderMan;
  beginSession(renderMan, "worldlifecycle_blocks.tif");
  const auto probe = openProbe();
  check(complete(probe), "the probe renderer exports its records");
  if (!complete(probe)) {
    return;
  }
  const auto first = probe.renderCount();

  renderMan.RiFrameBegin(1);
  worldBlockOfSpheres(renderMan, 2);
  renderMan.RiFrameEnd();
  check(probe.renderCount() == first + 1, "L1: a world block renders once");
  check(probe.frozen(first) == 1, "L1: the renderer receives a frozen world");
  check(probe.primitiveCount(first) == 2, "L1: the world holds the two spheres of the block");

  renderMan.RiFrameBegin(2);
  worldBlockOfSpheres(renderMan, 1);
  renderMan.RiFrameEnd();
  check(probe.renderCount() == first + 2, "L2: a second world block renders once more");
  check(probe.frozen(first + 1) == 1, "L2: the second block's world is frozen");
  check(probe.primitiveCount(first + 1) == 1, "L2: the second block's world holds its one sphere, not three");

  renderMan.RiEnd();
}

void testPrimitivesOutsideAWorldAreIgnored() {
  GMANRenderManImpl renderMan;
  beginSession(renderMan, "worldlifecycle_outside.tif");
  const auto probe = openProbe();
  check(complete(probe), "the probe renderer exports its records");
  if (!complete(probe)) {
    return;
  }
  const auto first = probe.renderCount();

  recorded.clear();
  renderMan.RiErrorHandler(recordingErrorHandler);
  auto threw = false;
  try {
    renderMan.RiObjectBegin();
    sphere(renderMan);
    renderMan.RiObjectEnd();

    worldBlockOfSpheres(renderMan, 1);

    RtFloat times[2] = {0.0f, 1.0f};
    renderMan.RiMotionBeginV(2, times);
    sphere(renderMan);
    sphere(renderMan);
    renderMan.RiMotionEnd();

    worldBlockOfSpheres(renderMan, 1);
  } catch (...) {
    threw = true;
  }
  renderMan.RiErrorHandler(RiErrorPrint);
  renderMan.RiEnd();

  check(!threw, "L3: a primitive outside a world block throws nothing");
  check(recorded.size() == 3, "L3: the handler sees exactly three reports, one per ignored sphere");
  auto expectedReports = true;
  for (RecordedError const& error : recorded) {
    expectedReports = expectedReports && error.code == RIE_ILLSTATE && error.severity == RIE_WARNING &&
                      error.message == ignoredMessage;
  }
  check(expectedReports, "L3: each report is RIE_ILLSTATE at RIE_WARNING and says the request is ignored");
  check(probe.renderCount() == first + 2, "L3: each of the two world blocks renders once");
  check(probe.primitiveCount(first) == 1, "L3: the first block's world holds only its own sphere");
  check(probe.primitiveCount(first + 1) == 1, "L3: the next block's world holds only its own sphere");
}

void testFailedWorldBeginLeavesTheWorldOpen() {
  GMANRenderManImpl renderMan;
  beginSession(renderMan, "worldlifecycle.nosuchext");

  auto worldBeginThrew = false;
  try {
    renderMan.RiWorldBegin();
  } catch (GMANError const&) {
    worldBeginThrew = true;
  }
  check(worldBeginThrew, "L4: RiWorldBegin throws for an unrecognized file extension");

  auto sphereThrew = false;
  try {
    sphere(renderMan);
  } catch (GMANError const&) {
    sphereThrew = true;
  }
  check(!sphereThrew, "L4: a primitive adds to the world a failed RiWorldBegin opened");

  renderMan.RiEnd();
}

} // namespace

int main() {
  testEachBlockIsItsOwnWorld();
  testPrimitivesOutsideAWorldAreIgnored();
  testFailedWorldBeginLeavesTheWorldOpen();

  return checkSummary("each world block renders its own frozen world and a primitive outside one is ignored with a "
                      "warning");
}
