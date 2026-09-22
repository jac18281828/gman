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
 * GMANRenderManImpl's renderer, objectManager, worldManager, viewingSystem
 * and output pointers are null from construction and stay null once RiEnd
 * releases what they own: a failed RiBegin leaves nothing for RiEnd to
 * delete, a failed RiWorldBegin leaves output null for RiWorldEnd's own
 * guard, and one RiEnd cannot leave viewingSystem dangling for the next
 * cycle's RiEnd to free again. Each case builds its own GMANRenderManImpl in
 * storage pre-filled with a non-zero byte pattern, so an unassigned member
 * cannot happen to read as the null a fresh, zeroed page would give it.
 */

#include <cstddef>
#include <cstring>
#include <new>

#include "check.h"
#include "gmanerror.h"
#include "gmanrendermanimpl.h"
#include "ri.h"

namespace {

// ASan's own malloc fill byte, not a zeroed page: a member `new` never
// assigns keeps this pattern instead of happening to read as null.
struct RawStorage {
  alignas(GMANRenderManImpl) unsigned char bytes[sizeof(GMANRenderManImpl)];

  RawStorage() { std::memset(bytes, 0xbe, sizeof(bytes)); }
};

// RiBegin's renderer load fails before `renderer` is assigned; RiEnd must
// not delete the stale pointer construction left there.
void checkFailedLoadCleansUp() {
  RawStorage storage;
  GMANRenderManImpl* impl = new (storage.bytes) GMANRenderManImpl();

  bool beginThrew = false;
  try {
    impl->RiBegin("gmannosuchrenderer");
  } catch (GMANError const& error) {
    beginThrew = true;
    check(error.getCode() == RIE_SYSTEM, "a failed renderer load raises RIE_SYSTEM");
  }
  check(beginThrew, "RiBegin(\"gmannosuchrenderer\") raises");

  bool endThrew = false;
  try {
    impl->RiEnd();
  } catch (GMANError const&) {
    endThrew = true;
  }
  check(!endThrew, "RiEnd after a failed RiBegin returns without raising");

  impl->~GMANRenderManImpl();
}

// RiWorldBegin fails on an unrecognized file extension after opening the
// world, leaving `output` unassigned; RiWorldEnd's own guard needs it null,
// not indeterminate.
void checkFailedWorldBeginCleansUp() {
  RawStorage storage;
  GMANRenderManImpl* impl = new (storage.bytes) GMANRenderManImpl();

  impl->RiBegin(RI_NULL);

  char displayName[] = "rendermanpointers.nosuchext";
  impl->RiDisplayV(displayName, "file", "rgba", 0, NULL, NULL);

  bool worldBeginThrew = false;
  try {
    impl->RiWorldBegin();
  } catch (GMANError const& error) {
    worldBeginThrew = true;
    check(error.getCode() == RIE_BADFILE, "an unrecognized file extension raises RIE_BADFILE");
  }
  check(worldBeginThrew, "RiWorldBegin() raises for an unrecognized file extension");

  bool worldEndThrew = false;
  try {
    impl->RiWorldEnd();
  } catch (GMANError const& error) {
    worldEndThrew = true;
    check(error.getCode() == RIE_ILLSTATE, "RiWorldEnd() raises the no-display-output error");
  }
  check(worldEndThrew, "RiWorldEnd() after a failed RiWorldBegin raises");

  bool endThrew = false;
  try {
    impl->RiEnd();
  } catch (GMANError const&) {
    endThrew = true;
  }
  check(!endThrew, "RiEnd() returns without raising");

  impl->~GMANRenderManImpl();
}

// A second RiBegin/RiEnd cycle on one GMANRenderManImpl: the first RiEnd
// must leave viewingSystem null, or the second frees it a second time --
// gman's sphere.rib-then-hider.rib crash, reproduced in one process.
void checkSecondCycleCleansUp() {
  RawStorage storage;
  GMANRenderManImpl* impl = new (storage.bytes) GMANRenderManImpl();

  impl->RiBegin(RI_NULL);
  impl->RiWorldBegin();

  bool firstWorldEndThrew = false;
  try {
    impl->RiWorldEnd();
  } catch (GMANError const&) {
    firstWorldEndThrew = true;
  }
  check(firstWorldEndThrew, "the first cycle's RiWorldEnd() raises, with no Display");

  bool firstEndThrew = false;
  try {
    impl->RiEnd();
  } catch (GMANError const&) {
    firstEndThrew = true;
  }
  check(!firstEndThrew, "the first RiEnd() returns without raising");

  impl->RiBegin(RI_NULL);

  bool secondEndThrew = false;
  try {
    impl->RiEnd();
  } catch (GMANError const&) {
    secondEndThrew = true;
  }
  check(!secondEndThrew, "the second RiEnd() returns without raising");

  impl->~GMANRenderManImpl();
}

} // namespace

int main() {
  checkFailedLoadCleansUp();
  checkFailedWorldBeginCleansUp();
  checkSecondCycleCleansUp();

  return checkSummary("GMANRenderManImpl's pointers stay null while they own nothing");
}
