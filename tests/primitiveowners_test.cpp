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
 * A world owns the primitives it is given. A GMANLinearWorldManager destroys
 * each primitive added to it when the world itself goes, and an empty
 * gman::Owned adds nothing.
 */

#include <cstddef>
#include <utility>
#include <vector>

#include "check.h"
#include "gmanlinearworldmanager.h"
#include "gmanowned.h"
#include "gmanprimitive.h"
#include "gmanworldmanager.h"

namespace {

int liveCount = 0;

class CountedPrimitive : public GMANPrimitive {
public:
  CountedPrimitive() { ++liveCount; }
  CountedPrimitive(CountedPrimitive const&) = delete;
  CountedPrimitive& operator=(CountedPrimitive const&) = delete;
  ~CountedPrimitive() override { --liveCount; }
};

std::vector<GMANPrimitive const*> walk(GMANWorldManager& world) {
  std::vector<GMANPrimitive const*> walked;
  for (GMANPrimitive const* primitive = world.getFirst(); primitive != nullptr; primitive = world.getNext()) {
    walked.push_back(primitive);
  }
  return walked;
}

void testWorldDestroysWhatItOwns() {
  std::vector<GMANPrimitive const*> expected;
  std::vector<GMANPrimitive const*> walked;
  int liveWithinScope = 0;
  {
    GMANLinearWorldManager world;
    for (int i = 0; i < 3; ++i) {
      auto primitive = gman::make<CountedPrimitive>();
      expected.push_back(primitive.get());
      world.add(std::move(primitive));
    }
    liveWithinScope = liveCount;
    walked = walk(world);
  }
  check(liveWithinScope == 3, "the world holds the three primitives added to it");
  check(walked == expected, "the world walk yields the three primitives in the order added");
  check(liveCount == 0, "the world destroys its three primitives when it goes");
}

void testEmptyOwnerAddsNothing() {
  GMANLinearWorldManager world;
  world.add(gman::Owned<GMANPrimitive>());
  auto primitive = gman::make<CountedPrimitive>();
  GMANPrimitive const* const added = primitive.get();
  world.add(std::move(primitive));

  std::vector<GMANPrimitive const*> const walked = walk(world);
  check(walked.size() == 1 && walked[0] == added, "an empty owner adds nothing: the walk yields only the primitive");
}

} // namespace

int main() {
  testWorldDestroysWhatItOwns();
  testEmptyOwnerAddsNothing();

  return checkSummary("the world owns what it is given");
}
