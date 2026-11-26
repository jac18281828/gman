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
#include <algorithm>
#include <list>
#include <memory>
#include <string_view>
#include <utility>

#include "gmancontext.h"
#include "gmanerror.h"
#include "gmanloadablerenderman.h"
#include "gmanrenderman.h"
#include "gmanrendermanimpl.h"
#include "ri.h"

namespace gman {

namespace {

char const* const kRibSuffix = ".rib";
char const* const kRibWriterPlugin = "libgmanrib.so";

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

// A failed load leaves no context current: RiGetContext and RiEnd see
// RI_NULL until RiBegin or RiContext selects one. The context active before
// this call is untouched here, so it survives for RiContext to return to;
// nothing falls back to rendering.
RtVoid Context::activateRibWriter() {
  std::unique_ptr<GMANRenderMan> loaded;
  try {
    loaded.reset(loadRenderMan(kRibWriterPlugin));
  } catch (GMANError&) {
    active = (GMANRenderMan*)RI_NULL;
    throw;
  }
  active = loaded.get();
  chl.push_back(std::move(loaded));
}

RtVoid Context::addContext(RtToken name) {
  if (namesRibFile(name)) {
    activateRibWriter();
    return;
  }
  auto renderer = std::make_unique<GMANRenderManImpl>();
  active = renderer.get();
  chl.push_back(std::move(renderer));
}

RtVoid Context::beginContext(RtToken name) {
  addContext(name);
  try {
    active->RiBegin(name);
  } catch (...) {
    removeCurrent();
    throw;
  }
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
  GMANRenderMan* r = (GMANRenderMan*)ch;
  for (auto const& owned : chl) {
    if (owned.get() == r) {
      active = r;
      return;
    }
  }
  GMANError error(RIE_NESTING, RIE_SEVERE, "invalid Context Handle");
  throw(error);
}
RtVoid Context::removeCurrent(RtVoid) {
  auto const owned = std::ranges::find_if(chl, [this](auto const& context) { return context.get() == active; });
  if (owned == chl.end()) {
    return;
  }
  chl.erase(owned);
  active = (GMANRenderMan*)RI_NULL;
}

} // namespace gman
