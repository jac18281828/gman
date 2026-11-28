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
 * GMANWorld owns the primitives it is given, in the order added. An empty
 * owner adds nothing. A frozen world refuses add and keeps what it holds.
 * Iteration needs no freeze and holds no state in the world, so walks nest.
 * ofType yields the primitives that are a given type, a derived class under
 * its base. A world moves and does not copy.
 */

#include <concepts>
#include <cstddef>
#include <iterator>
#include <ranges>
#include <set>
#include <type_traits>
#include <utility>
#include <vector>

#include "check.h"
#include "gmanerror.h"
#include "gmanowned.h"
#include "gmanprimitive.h"
#include "gmanworld.h"
#include "ri.h"

namespace {

int liveCount = 0;

class CountedPrimitive : public GMANPrimitive {
public:
  CountedPrimitive() { ++liveCount; }
  CountedPrimitive(CountedPrimitive const&) = delete;
  CountedPrimitive& operator=(CountedPrimitive const&) = delete;
  ~CountedPrimitive() override { --liveCount; }
};

class DerivedPrimitive : public CountedPrimitive {};

class UnrelatedPrimitive : public GMANPrimitive {};

static_assert(std::ranges::forward_range<GMANWorld>);
static_assert(std::ranges::forward_range<GMANWorld const>);
static_assert(std::same_as<std::ranges::range_reference_t<GMANWorld>, GMANPrimitive const&>);
static_assert(std::same_as<std::ranges::range_reference_t<GMANWorld const>, GMANPrimitive const&>);
static_assert(!std::is_copy_constructible_v<GMANWorld>);
static_assert(!std::is_copy_assignable_v<GMANWorld>);
static_assert(std::is_move_constructible_v<GMANWorld>);
static_assert(std::is_move_assignable_v<GMANWorld>);

using BaseView = decltype(std::declval<GMANWorld const&>().ofType<CountedPrimitive>());
static_assert(std::ranges::forward_range<BaseView>);
static_assert(std::same_as<std::ranges::range_reference_t<BaseView>, CountedPrimitive const&>);

std::vector<GMANPrimitive const*> addresses(GMANWorld const& world) {
  std::vector<GMANPrimitive const*> walked;
  for (GMANPrimitive const& primitive : world) {
    walked.push_back(&primitive);
  }
  return walked;
}

std::vector<GMANPrimitive const*> addCounted(GMANWorld& world, int count) {
  std::vector<GMANPrimitive const*> added;
  for (int i = 0; i < count; ++i) {
    auto primitive = gman::make<CountedPrimitive>();
    added.push_back(primitive.get());
    world.add(std::move(primitive));
  }
  return added;
}

void testOrder() {
  GMANWorld world;
  const auto added = addCounted(world, 3);
  check(addresses(world) == added, "a range-for yields the three primitives in the order added");
  check(liveCount == 3, "the world holds the three primitives added to it");
}

void testEmptyOwnerAddsNothing() {
  GMANWorld world;
  world.add(gman::Owned<GMANPrimitive>());
  auto primitive = gman::make<CountedPrimitive>();
  GMANPrimitive const* const added = primitive.get();
  world.add(std::move(primitive));
  world.add(gman::Owned<GMANPrimitive>());

  check(std::ranges::distance(world) == 1, "an empty owner adds nothing: the world holds one primitive");
  check(addresses(world) == std::vector<GMANPrimitive const*>{added}, "the one primitive is the one added");
}

void testOwnership() {
  {
    GMANWorld world;
    addCounted(world, 2);
    check(liveCount == 2, "an open world holds what it was given");
  }
  check(liveCount == 0, "an open world destroys its primitives when it leaves its scope");

  {
    GMANWorld world;
    addCounted(world, 2);
    world.freeze();
  }
  check(liveCount == 0, "a frozen world destroys its primitives when it leaves its scope");

  GMANWorld world;
  addCounted(world, 2);
  world.freeze();
  check(liveCount == 2, "a frozen world holds what it was given");
  world = GMANWorld();
  check(liveCount == 0, "assigning a fresh world over a frozen one destroys the old primitives at once");
  check(!world.frozen(), "the assigned world is open");
  const auto added = addCounted(world, 1);
  check(addresses(world) == added, "the assigned world accepts add");
  check(liveCount == 1, "the assigned world holds the one primitive it was given");
}

void testFreeze() {
  GMANWorld world;
  check(!world.frozen(), "a new world is open");
  const auto added = addCounted(world, 2);
  world.freeze();
  check(world.frozen(), "freeze closes the world");
  world.freeze();
  check(world.frozen() && addresses(world) == added, "a second freeze changes nothing");

  auto rejected = gman::make<CountedPrimitive>();
  bool threw = false;
  RtInt code = 0;
  RtInt severity = 0;
  try {
    world.add(std::move(rejected));
  } catch (GMANError const& error) {
    threw = true;
    code = error.getCode();
    severity = error.getSeverity();
  }
  check(threw, "add on a frozen world throws GMANError");
  check(code == RIE_ILLSTATE && severity == RIE_ERROR, "the error is RIE_ILLSTATE at RIE_ERROR");
  check(addresses(world) == added, "the frozen world keeps its earlier primitives in order");
  check(liveCount == 2, "the rejected primitive is destroyed");
}

void testConcurrentWalks() {
  GMANWorld world;
  addCounted(world, 4);
  std::set<std::pair<GMANPrimitive const*, GMANPrimitive const*>> pairs;
  for (GMANPrimitive const& outer : world) {
    for (GMANPrimitive const& inner : world) {
      pairs.emplace(&outer, &inner);
    }
  }
  check(pairs.size() == 16, "two nested walks over four primitives visit all 16 ordered pairs");
}

void testOfType() {
  GMANWorld world;
  auto base = gman::make<CountedPrimitive>();
  auto derived = gman::make<DerivedPrimitive>();
  auto unrelated = gman::make<UnrelatedPrimitive>();
  CountedPrimitive const* const baseAt = base.get();
  CountedPrimitive const* const derivedAt = derived.get();
  UnrelatedPrimitive const* const unrelatedAt = unrelated.get();
  world.add(std::move(base));
  world.add(std::move(derived));
  world.add(std::move(unrelated));
  world.add(gman::make<GMANPrimitive>());

  std::vector<CountedPrimitive const*> bases;
  for (CountedPrimitive const& primitive : world.ofType<CountedPrimitive>()) {
    bases.push_back(&primitive);
  }
  check(bases == std::vector<CountedPrimitive const*>{baseAt, derivedAt},
        "ofType of the base yields the base and the derived instance in order");

  std::vector<UnrelatedPrimitive const*> unrelateds;
  for (UnrelatedPrimitive const& primitive : world.ofType<UnrelatedPrimitive>()) {
    unrelateds.push_back(&primitive);
  }
  check(unrelateds == std::vector<UnrelatedPrimitive const*>{unrelatedAt}, "ofType of the unrelated class yields one");

  const GMANWorld empty;
  auto none = empty.ofType<CountedPrimitive>();
  check(std::ranges::empty(none), "ofType over an empty world is empty");
}

void testMoveOnly() {
  GMANWorld source;
  const auto added = addCounted(source, 2);
  const GMANWorld target = std::move(source);
  check(addresses(target) == added, "a moved world holds the primitives its source held");
  check(liveCount == 2, "a move creates and destroys no primitive");
}

} // namespace

int main() {
  testOrder();
  testEmptyOwnerAddsNothing();
  testOwnership();
  testFreeze();
  testConcurrentWalks();
  testOfType();
  testMoveOnly();

  return checkSummary("the world owns, orders and freezes its primitives, iterates them concurrently and filters them "
                      "by type");
}
