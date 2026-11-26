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
#pragma once

#include <list>
#include <memory>

#include "gmanrenderman.h"
#include "ri.h"

namespace gman {

class Context {
private:
  std::list<std::unique_ptr<GMANRenderMan>> chl;
  GMANRenderMan* active;

  // Loads the RIB-writer plugin and makes it current; clears current and
  // rethrows if the load fails.
  RtVoid activateRibWriter(RtVoid);

public:
  Context();

  RtVoid addContext(RtToken name);
  // Adds a context, makes it current and begins it. When the begin raises,
  // the context is freed, none is current and the error propagates; the
  // context current before the call stays listed for switchTo.
  RtVoid beginContext(RtToken name);
  RtContextHandle getContext(RtVoid);
  GMANRenderMan& current(RtVoid);
  RtVoid switchTo(RtContextHandle);
  RtVoid removeCurrent(RtVoid);
};

} // namespace gman
