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
 * Test-only indirect-light pass: answers black until prepare() has seen a
 * world holding at least one primitive, kConstantIndirect after. Its own
 * live-instance count, read back through GMANConstantIndirectLiveCount,
 * proves loadIndirectPass's deleter frees through this module rather than
 * through a plain delete. Never dlopened outside the test suite.
 */

#include "constantindirectconstants.h"
#include "gmanindirectpass.h"
#include "gmanloadable.h"

namespace {

int& liveCount() {
  static int count = 0;
  return count;
}

class ConstantIndirectPass : public gman::IndirectPass {
public:
  ConstantIndirectPass() { ++liveCount(); }
  ~ConstantIndirectPass() override { --liveCount(); }

  void prepare(GMANWorldManager& world, GMANRayOccluder const& /*occluder*/, GMANOptions const& /*options*/) override {
    ready_ = world.getFirst() != nullptr;
  }

  GMANColor irradiance(GMANHit const& /*hit*/, GMANVector const& /*I*/) const override {
    return ready_ ? GMANColor(kConstantIndirect, kConstantIndirect, kConstantIndirect) : GMANColor(0.0f, 0.0f, 0.0f);
  }

private:
  bool ready_ = false;
};

} // namespace

static GMANLoadableObjectInfo loadableInfo = {
    "Constant indirect-light probe (test-only)",
    "John Cairns <john@2ad.com>",
    "Test-only: answers black until prepare() has seen a primitive, kConstantIndirect after. Never dlopened "
    "outside the test suite.",
};

extern "C" GMAN_EXPORT GMANLoadableObjectInfo* GMANGetLoadableInfo(void) { return &loadableInfo; }

extern "C" GMAN_EXPORT gman::IndirectPass* GMANCreateIndirectPass(void) { return new ConstantIndirectPass(); }

extern "C" GMAN_EXPORT void GMANDestroyIndirectPass(gman::IndirectPass* pass) { delete pass; }

extern "C" GMAN_EXPORT int GMANConstantIndirectLiveCount() { return liveCount(); }
