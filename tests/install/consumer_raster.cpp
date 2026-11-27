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
 * Proves an installed gman_raster links from its own CMake package with no
 * other gman target on the link line: <gman/gmanpatchpolyobjectmanager.h>
 * resolves through gman::gman_raster's include path, and
 * GMANPatchPolyObjectManager::getRSPoints, defined in libgman_raster, answers
 * a request that builds nothing with an empty owner, while a GMANObject
 * answers through libgman's GMANPrimitive interface. The include directory,
 * the core link and the C++ standard all reach this program through
 * gman::gman_raster's public dependency on gman::gman.
 */

#include <gman/gmanattributes.h>
#include <gman/gmanobject.h>
#include <gman/gmanoptions.h>
#include <gman/gmanowned.h>
#include <gman/gmanparameterlist.h>
#include <gman/gmanpatchpolyobjectmanager.h>
#include <gman/gmanprimitive.h>
#include <gman/gmantransform.h>

int main() {
  GMANPatchPolyObjectManager manager;
  GMANOptions options;
  GMANAttributes attributes;
  GMANTransform transform;
  if (manager.getRSPoints(0, GMANParameterList(), &options, &attributes, &transform) != nullptr) {
    return 1;
  }

  gman::Owned<GMANPrimitive> const primitive = gman::make<GMANObject>();
  GMANObject* const object = dynamic_cast<GMANObject*>(primitive.get());
  const bool isEmptyObject = object != nullptr && object->getBody() == nullptr && object->getNext() == nullptr;
  return isEmptyObject ? 0 : 1;
}
