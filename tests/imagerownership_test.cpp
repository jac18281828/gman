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
 * GMANOptions::imagerModule is owned through the real C API
 * (RiBegin/RiImagerV/RiFrameBegin/RiFrameEnd/RiEnd), never a hand-copied
 * GMANOptions: a nested RiFrameBegin copies the options stack's top, so a
 * nested RiImagerV must share ownership of the outer scope's instance
 * rather than freeing it out from under the parent, and every instance
 * this test loads is freed exactly once, when its last owner releases it.
 * Proven through countingimager's own exported live-instance counter,
 * never by forcing an actual double free.
 */

#include <string>

#include <dlfcn.h>

#include "check.h"
#include "ri.h"

namespace {

RtInt lastCode = 0;
RtInt lastSeverity = 0;
std::string lastMessage;

RtVoid recordingErrorHandler(RtInt code, RtInt severity, char const* message) {
  lastCode = code;
  lastSeverity = severity;
  lastMessage = message;
}

// countingimager is a plugin, dlopened at runtime by GMANOptions::
// setImager, never linked into this binary, so its own live-count function
// is resolved the same way: dlopen (idempotent -- the plugin may already
// be loaded) then dlsym, once, memoized here.
int countingImagerLiveCount() {
  using LiveCountFn = int (*)();
  static LiveCountFn const fn = []() -> LiveCountFn {
    void* const handle = dlopen("libcountingimager.so", RTLD_LAZY);
    if (handle == nullptr) {
      return nullptr;
    }
    return reinterpret_cast<LiveCountFn>(dlsym(handle, "GMANCountingImagerLiveCount"));
  }();
  return fn ? fn() : -1;
}

// Only steps 4 and 6 discriminate a shallow-copied raw pointer from
// shared_ptr ownership: at step 4, a shallow copy frees the outer scope's
// instance when the nested RiImagerV replaces it; at step 6, a raw
// imagerModule with an empty destructor never frees the outer's own
// instance at all.
void checkNestedOwnership() {
  int const base = countingImagerLiveCount();

  RiBegin(RI_NULL);
  check(countingImagerLiveCount() == base, "nested: no imager is loaded before RiImagerV");

  RiImagerV("countingimager", 0, nullptr, nullptr);
  check(countingImagerLiveCount() == base + 1, "nested: the outer imager is loaded");

  RiFrameBegin(1);
  check(countingImagerLiveCount() == base + 1, "nested: entering the frame shares the outer's instance");

  RiImagerV("countingimager", 0, nullptr, nullptr);
  check(countingImagerLiveCount() == base + 2,
        "nested: the nested imager adds a second instance, the outer's own survives");

  RiFrameEnd();
  check(countingImagerLiveCount() == base + 1, "nested: leaving the frame frees only the nested instance");

  RiEnd();
  check(countingImagerLiveCount() == base, "nested: ending the context frees the outer's own instance");
}

// A nested call that fails to load resets only the copy's own reference,
// leaving the outer scope's instance alone.
void checkFailedNestedCallLeavesOuterAlone() {
  int const base = countingImagerLiveCount();

  RiBegin(RI_NULL);
  RiImagerV("countingimager", 0, nullptr, nullptr);
  check(countingImagerLiveCount() == base + 1, "failed nested: the outer imager is loaded");

  RiFrameBegin(1);
  check(countingImagerLiveCount() == base + 1, "failed nested: entering the frame shares the outer's instance");

  RiImagerV("gmanzbuffer", 0, nullptr, nullptr);
  check(countingImagerLiveCount() == base + 1, "failed nested: a failed load leaves the outer's instance alone");
  check(lastCode == RIE_NOSHADER, "failed nested: the failure reports RIE_NOSHADER");
  check(lastSeverity == RIE_ERROR, "failed nested: the failure reports RIE_ERROR, not RIE_SEVERE");
  check(lastMessage.find("Imager \"gmanzbuffer\"") != std::string::npos,
        "failed nested: the message names Imager \"gmanzbuffer\"");

  RiFrameEnd();
  check(countingImagerLiveCount() == base + 1, "failed nested: leaving the frame changes nothing further");

  RiEnd();
  check(countingImagerLiveCount() == base, "failed nested: ending the context frees the outer's own instance");
}

// No RiFrameBegin at all, so no raw pointer is ever aliased between two
// objects: replacing the imager in place is safe either way, but only a
// shared_ptr frees the second instance once its one owner is destroyed.
void checkReplacementNoNesting() {
  int const base = countingImagerLiveCount();

  RiBegin(RI_NULL);
  RiImagerV("countingimager", 0, nullptr, nullptr);
  check(countingImagerLiveCount() == base + 1, "replacement: the first imager is loaded");

  RiImagerV("countingimager", 0, nullptr, nullptr);
  check(countingImagerLiveCount() == base + 1, "replacement: replacing it in place nets to one live instance");

  RiEnd();
  check(countingImagerLiveCount() == base, "replacement: ending the context frees the second instance");
}

} // namespace

int main() {
  RiErrorHandler(recordingErrorHandler);

  checkNestedOwnership();
  checkFailedNestedCallLeavesOuterAlone();
  checkReplacementNoNesting();

  return checkSummary(
      "GMANOptions::imagerModule frees exactly one instance per owner, never one a parent frame still holds");
}
