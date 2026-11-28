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
 * Test-only renderer that draws nothing and records, for each render() call,
 * whether the world it received was frozen and how many primitives it held.
 * GMANWorldProbeRenderCount, GMANWorldProbeFrozen and
 * GMANWorldProbePrimitiveCount read the records back by call index.
 */

#include <cstddef>
#include <iterator>
#include <ranges>
#include <vector>

#include "gmanloadable.h"
#include "gmanrayprimitivefactory.h"
#include "gmanrenderer.h"
#include "gmanworld.h"
#include "ri.h"

namespace {

struct RenderRecord {
  bool frozen;
  std::size_t primitives;
};

std::vector<RenderRecord>& records() {
  static std::vector<RenderRecord> recorded;
  return recorded;
}

class WorldProbeRenderer : public GMANRenderer {
public:
  RtVoid illuminance(RtInt /*i*/, GMANPoint const& /*p*/, GMANVector const& /*axis*/, RtFloat /*angle*/) override {}
  RtVoid illuminate(RtInt /*i*/, GMANPoint const& /*p*/, GMANVector const& /*axis*/, RtFloat /*angle*/) override {}
  RtVoid solar(RtInt /*i*/, GMANVector const& /*axis*/, RtFloat /*angle*/) override {}

  RtFloat getDepth(int /*xs*/, int /*ys*/) const override { return RI_INFINITY; }

  RtVoid render(GMANWorld const& world, GMANFrameBuffer* /*frameBuffer*/, GMANViewingSystem* /*viewingSys*/,
                GMANOptions const& /*options*/, GMANAttributes const& /*attributes*/) override {
    records().push_back({world.frozen(), static_cast<std::size_t>(std::ranges::distance(world))});
  }

  GMANPrimitiveFactory* getPrimitiveFactory(RtVoid) override { return &primitiveFactory; }

private:
  GMANRayPrimitiveFactory primitiveFactory;
};

GMANLoadableObjectInfo loadableInfo = {
    "World probe renderer (test-only)",
    "John Cairns <john@2ad.com>",
    "Test-only: records whether each world it renders is frozen and how many primitives it holds.",
};

WorldProbeRenderer renderer;

bool recordedAt(int call) { return call >= 0 && static_cast<std::size_t>(call) < records().size(); }

} // namespace

extern "C" GMAN_EXPORT GMANLoadableObjectInfo* GMANGetLoadableInfo(void) { return &loadableInfo; }

extern "C" GMAN_EXPORT GMANRenderer* GMANLoadRenderer(void) { return &renderer; }

extern "C" GMAN_EXPORT int GMANWorldProbeRenderCount() { return static_cast<int>(records().size()); }

extern "C" GMAN_EXPORT int GMANWorldProbeFrozen(int call) {
  if (!recordedAt(call)) {
    return -1;
  }
  return records()[static_cast<std::size_t>(call)].frozen ? 1 : 0;
}

extern "C" GMAN_EXPORT int GMANWorldProbePrimitiveCount(int call) {
  if (!recordedAt(call)) {
    return -1;
  }
  return static_cast<int>(records()[static_cast<std::size_t>(call)].primitives);
}
