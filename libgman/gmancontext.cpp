/* SPDX-License-Identifier: LGPL-2.1-or-later */

/*----------------------------------------------------------
  Copyright (C) Lionel Joseph Lacour 2000, 2001
  2000/08/06  First release
  ----------------------------------------------------------
  This class implements the RiContext feature of the
  RiSpec V3.2
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
#include <string_view>

#include "gmancontext.h"
#include "gmanloadablerenderman.h"
#include "gmanrendermanimpl.h"

namespace gman {

namespace {

const char* const kRibSuffix = ".rib";
const char* const kRibWriterPlugin = "libgmanrib.so";

// A name ending in ".rib" selects the RIB-writer plugin; any other name,
// RI_NULL included, keeps the renderer.
bool namesRibFile(RtToken name) {
  if (name == nullptr) {
    return false;
  }
  return std::string_view(name).ends_with(kRibSuffix);
}

} // namespace

Context::Context() { active = (GMANRenderMan*)RI_NULL; }

RtVoid Context::addContext(RtToken name) {
  if (namesRibFile(name)) {
    // Mirrors the renderer branch below: a context is always pushed and
    // made current, even when the plugin fails to load, so the caller's
    // RiGetContext and RiEnd operate on this context, not whichever one was
    // active before RiBegin. RiBegin(RI_NULL) loads the always-built default
    // renderer, so RiEnd's delete on this fallback has a real object to
    // delete instead of GMANRenderManImpl's never-set renderer pointer.
    try {
      active = loadRenderMan(kRibWriterPlugin);
    } catch (GMANError&) {
      GMANRenderManImpl* fallback = new GMANRenderManImpl;
      fallback->RiBegin(RI_NULL);
      active = fallback;
      chl.push_back(active);
      throw;
    }
  } else {
    active = new GMANRenderManImpl;
  }
  chl.push_back(active);
}

RtContextHandle Context::getContext() { return (RtContextHandle)active; }

GMANRenderMan& Context::current() {
  if (active == ((GMANRenderMan*)RI_NULL)) {
    GMANError error(RIE_NOTSTARTED, RIE_SEVERE, "No active context");
    throw error;
  }
  return *active;
}
RtVoid Context::switchTo(RtContextHandle ch) {
  std::list<GMANRenderMan*>::iterator first = chl.begin();
  std::list<GMANRenderMan*>::iterator last = chl.end();
  GMANRenderMan* r = (GMANRenderMan*)ch;
  for (; first != last; first++) {
    if (*first == r) {
      active = r;
      return;
    }
  }
  GMANError error(RIE_NESTING, RIE_SEVERE, "invalid Context Handle");
  throw(error);
}
RtVoid Context::removeCurrent(RtVoid) {
  std::list<GMANRenderMan*>::iterator first = chl.begin();
  std::list<GMANRenderMan*>::iterator last = chl.end();
  for (; first != last; first++) {
    if (*first == active) {
      delete *first;
      chl.erase(first);
      active = (GMANRenderMan*)RI_NULL;
      return;
    }
  }
}

} // namespace gman
