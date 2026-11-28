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

#pragma once

#include <cstddef>
#include <iterator>
#include <ranges>
#include <vector>

#include "gman/export.h"
#include "gmanowned.h"
#include "gmanprimitive.h"

/*
 * GMANWorld
 *
 * Owns every primitive a scene holds, in the order added. The scope that
 * builds a scene owns its world and every other scope borrows it as a
 * GMANWorld const&.
 *
 * A world is open when constructed and accepts add. freeze() closes it for
 * good: add then throws GMANError RIE_ILLSTATE, and a caller wanting an open
 * world assigns a new GMANWorld. A world moves and does not copy.
 *
 * Iteration needs no freeze. begin() and end() are const, and the iterators
 * hold no state in the world, so any number of walks run at once:
 *
 *   for (GMANPrimitive const& primitive : world) { ... }
 *
 * ofType<T>() is the same walk filtered to the primitives that are a T:
 *
 *   for (GMANRayPrimitive const& primitive : world.ofType<GMANRayPrimitive>()) { ... }
 */
class GMAN_EXPORT GMANWorld {
private:
  using Storage = std::vector<gman::Owned<GMANPrimitive>>;

public:
  class Iterator {
  public:
    using value_type = GMANPrimitive;
    using difference_type = std::ptrdiff_t;
    using reference = GMANPrimitive const&;
    using pointer = GMANPrimitive const*;
    using iterator_category = std::forward_iterator_tag;
    using iterator_concept = std::forward_iterator_tag;

    Iterator() = default;

    reference operator*() const { return **position; }
    pointer operator->() const { return position->get(); }

    Iterator& operator++() {
      ++position;
      return *this;
    }

    Iterator operator++(int) {
      const auto before = *this;
      ++position;
      return before;
    }

    friend bool operator==(Iterator const&, Iterator const&) = default;

  private:
    friend class GMANWorld;

    explicit Iterator(Storage::const_iterator at) : position(at) {}

    Storage::const_iterator position;
  };

  GMANWorld() = default;
  GMANWorld(GMANWorld const&) = delete;
  GMANWorld& operator=(GMANWorld const&) = delete;
  GMANWorld(GMANWorld&&) noexcept = default;
  GMANWorld& operator=(GMANWorld&&) noexcept = default;
  ~GMANWorld() = default;

  // Appends primitive and takes ownership. An empty owner adds nothing; a
  // frozen world throws GMANError RIE_ILLSTATE and keeps what it holds.
  void add(gman::Owned<GMANPrimitive> primitive);

  // Closes the world to add. A frozen world stays frozen.
  void freeze();

  bool frozen() const { return isFrozen; }

  Iterator begin() const { return Iterator(primitives.begin()); }
  Iterator end() const { return Iterator(primitives.end()); }

  // The primitives that are a T, in the order added: a derived class appears
  // under its base.
  template <typename T> auto ofType() const {
    return std::ranges::subrange(begin(), end()) | std::views::filter([](GMANPrimitive const& primitive) {
             return dynamic_cast<T const*>(&primitive) != nullptr;
           }) |
           std::views::transform(
               [](GMANPrimitive const& primitive) -> T const& { return dynamic_cast<T const&>(primitive); });
  }

private:
  Storage primitives;
  bool isFrozen = false;
};
