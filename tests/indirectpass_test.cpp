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
 * gman::loadIndirectPass: a real module answers through its own
 * GMANCreateIndirectPass/GMANDestroyIndirectPass, a missing module, one
 * lacking either entry point, or one whose create function itself returns
 * null all answer null and warn, and the deleter frees through the module
 * rather than through a plain delete.
 */

#include <fstream>
#include <sstream>
#include <string>

#include <dlfcn.h>

#include "check.h"
#include "constantindirectconstants.h"
#include "gmanindirectpass.h"
#include "gmanlinearworldmanager.h"
#include "gmanlog.h"
#include "gmanparameterlist.h"
#include "gmanraybvh.h"
#include "gmanrayoccluder.h"
#include "gmanraysphere.h"
#include "gmantransform.h"
#include "ri.h"

namespace {

// constantindirect is a plugin, dlopened at runtime by
// gman::loadIndirectPass, never linked into this binary, so its own
// live-count function is resolved the same way: dlopen (idempotent -- the
// plugin may already be loaded) then dlsym, once, memoized here.
int constantIndirectLiveCount() {
  using LiveCountFn = int (*)();
  static LiveCountFn const fn = []() -> LiveCountFn {
    void* const handle = dlopen("libconstantindirect.so", RTLD_LAZY);
    if (handle == nullptr) {
      return nullptr;
    }
    return reinterpret_cast<LiveCountFn>(dlsym(handle, "GMANConstantIndirectLiveCount"));
  }();
  return fn ? fn() : -1;
}

bool isBlack(GMANColor const& c) { return c.getRed() == 0.0f && c.getGreen() == 0.0f && c.getBlue() == 0.0f; }

bool isConstant(GMANColor const& c) {
  return c.getRed() == kConstantIndirect && c.getGreen() == kConstantIndirect && c.getBlue() == kConstantIndirect;
}

// Adds the one sphere prepare() needs to see before constantindirect stops
// answering black.
void buildOneSphereWorld(GMANLinearWorldManager& world) {
  GMANMatrix4 place;
  GMANOneMatrix storage(place);
  GMANTransform const transform(storage);
  world.add(new GMANRaySphere(1.0f, -1.0f, 1.0f, 360.0f, GMANParameterList(), transform));
}

GMANHit centreHit() {
  GMANHit hit;
  hit.point = GMANPoint(0.0f, 0.0f, 0.0f);
  hit.normal = GMANVector(0.0f, 0.0f, 1.0f);
  return hit;
}

void checkLoadAndPrepare() {
  check(constantIndirectLiveCount() == 0, "constantindirect's live count starts at 0");

  auto pass = gman::loadIndirectPass("constantindirect");
  check(pass != nullptr, "loadIndirectPass(\"constantindirect\") returns a pass");
  check(constantIndirectLiveCount() == 1, "the live count is 1 once loaded");
  if (pass == nullptr) {
    return;
  }

  GMANHit const hit = centreHit();
  GMANVector const I(0.0f, 0.0f, -1.0f);
  check(isBlack(pass->irradiance(hit, I)), "before prepare(): irradiance() answers black");

  GMANLinearWorldManager world;
  buildOneSphereWorld(world);
  GMANRayBVH bvh;
  bvh.build(world);
  GMANRayOccluder const occluder(bvh);
  GMANOptions const options;
  pass->prepare(world, occluder, options);

  check(isConstant(pass->irradiance(hit, I)), "after prepare() over a world holding a sphere: irradiance() answers "
                                              "kConstantIndirect");

  pass.reset();
  check(constantIndirectLiveCount() == 0, "the live count returns to 0 once the pass is released");
}

void checkMissingAndWrongModule() {
  check(gman::loadIndirectPass("nosuchpass") == nullptr, "loadIndirectPass(\"nosuchpass\") returns null");
  check(gman::loadIndirectPass("matte") == nullptr,
        "loadIndirectPass(\"matte\") returns null: a module lacking the entry points");
}

// nullindirect's own GMANCreateIndirectPass always returns null. The
// warning is captured through setLogFile with the screen silenced, then
// the log's previous destination is restored so later checks log to the
// screen as before.
void checkCreateReturningNullWarns() {
  std::string const logPath = "indirectpass_nullcreate.log";
  std::remove(logPath.c_str());
  setLogFile(logPath.c_str());
  setScreenOutput(false);

  auto const pass = gman::loadIndirectPass("nullindirect");
  check(pass == nullptr, "loadIndirectPass(\"nullindirect\") returns null: its own create function returns null");

  setLogFile("/dev/null");
  setScreenOutput(true);

  std::ifstream in(logPath, std::ios::binary);
  std::ostringstream contents;
  contents << in.rdbuf();
  std::string const log = contents.str();
  check(log.find("libnullindirect.so") != std::string::npos, "the warning names libnullindirect.so");
  check(log.find("GMANCreateIndirectPass returned null") != std::string::npos,
        "the warning says the create function returned null");
}

// Loaded, released, loaded again: each cycle answers black before its own
// prepare() and kConstantIndirect after, and the live count returns to 1,
// not 2 -- proof the first instance was actually freed, not merely
// forgotten.
void checkReloadAfterRelease() {
  auto first = gman::loadIndirectPass("constantindirect");
  check(first != nullptr, "reload setup: the first load succeeds");
  first.reset();
  check(constantIndirectLiveCount() == 0, "reload setup: releasing the first instance drops the count to 0");

  auto second = gman::loadIndirectPass("constantindirect");
  check(second != nullptr, "reload: the second load succeeds");
  check(constantIndirectLiveCount() == 1, "reload: the live count reads 1 again, not 2");
  if (second == nullptr) {
    return;
  }

  GMANHit const hit = centreHit();
  GMANVector const I(0.0f, 0.0f, -1.0f);
  check(isBlack(second->irradiance(hit, I)), "reload: the second instance answers black before its own prepare()");

  GMANLinearWorldManager world;
  buildOneSphereWorld(world);
  GMANRayBVH bvh;
  bvh.build(world);
  GMANRayOccluder const occluder(bvh);
  GMANOptions const options;
  second->prepare(world, occluder, options);
  check(isConstant(second->irradiance(hit, I)), "reload: the second instance answers kConstantIndirect after its "
                                                "own prepare()");

  second.reset();
  check(constantIndirectLiveCount() == 0, "reload: releasing the second instance drops the count to 0");
}

} // namespace

int main() {
  checkLoadAndPrepare();
  checkMissingAndWrongModule();
  checkCreateReturningNullWarns();
  checkReloadAfterRelease();

  return checkSummary("gman::loadIndirectPass: a real module answers through its own entry points, a missing "
                      "module, a wrong module, and a module whose create function returns null all answer null, "
                      "and the deleter frees through the module");
}
