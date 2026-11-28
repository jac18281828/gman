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
 * Comparisons for the geometry template tests, built on check.h. One call
 * issues one check. A failing check names the member under test, the instance
 * (RtFloat or gman::PartnerReal), the first component that differs, and the
 * expected and actual values, as in
 *
 *   Vector<RtFloat>::operator+(Vector) : x: expected 5, got 4
 *
 * expectExact compares with ==, for inputs whose results are exactly
 * representable. expectClose allows a number of ulps of the instance's own
 * epsilon, scaled by the larger of the expected magnitude and 1, so a result
 * that is only as accurate as float must still be as accurate as double in
 * the double instance. reciprocal<T, N> and the witness matrices give the
 * non-dyadic inputs on which float and double round differently.
 */

#pragma once

#include <algorithm>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <format>
#include <limits>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "check.h"
#include "gmanhpoint.h"
#include "gmanmatrix4.h"
#include "gmanpoint.h"
#include "gmanvector.h"
#include "gmanvector4.h"
#include "ri.h"

namespace geometrycheck {

template <typename T, int N> inline constexpr T reciprocal = T(1) / N;

template <typename T> std::string instanceName() {
  if constexpr (std::is_same_v<T, RtFloat>) {
    return "RtFloat";
  } else {
    return "gman::PartnerReal";
  }
}

template <typename T> std::string member(std::string const& type, std::string const& name) {
  return std::format("{}<{}>::{}", type, instanceName<T>(), name);
}

template <typename T> std::vector<T> partsOf(gman::Vector<T> const& v) { return {v.getX(), v.getY(), v.getZ()}; }

template <typename T> std::vector<T> partsOf(gman::Point<T> const& p) { return {p.getX(), p.getY(), p.getZ()}; }

template <typename T> std::vector<T> partsOf(gman::HPoint<T> const& p) {
  return {p.getX(), p.getY(), p.getZ(), p.getW()};
}

template <typename T> std::vector<T> partsOf(gman::Vector4<T> const& v) {
  return {v.getX(), v.getY(), v.getZ(), v.getW()};
}

template <typename T> std::vector<T> partsOf(gman::Matrix4<T> const& m) {
  std::vector<T> all;
  for (std::size_t i = 0; i < 4; ++i) {
    for (std::size_t j = 0; j < 4; ++j) {
      all.push_back(m[i][j]);
    }
  }
  return all;
}

template <typename T> using OtherInstance = std::conditional_t<std::is_same_v<T, RtFloat>, gman::PartnerReal, RtFloat>;

template <typename T> gman::Matrix4<T> sequentialMatrix() {
  gman::Matrix4<T> m;
  for (std::size_t i = 0; i < 4; ++i) {
    for (std::size_t j = 0; j < 4; ++j) {
      m[i][j] = static_cast<T>(4 * i + j + 1);
    }
  }
  return m;
}

template <typename T> gman::Matrix4<T> columnWitnessMatrix() {
  auto m = gman::Matrix4<T>();
  m[0][0] = reciprocal<T, 3>;
  m[1][1] = reciprocal<T, 7>;
  m[2][2] = reciprocal<T, 11>;
  m[0][3] = reciprocal<T, 5>;
  m[1][3] = reciprocal<T, 13>;
  m[2][3] = reciprocal<T, 17>;
  return m;
}

template <typename T> gman::Matrix4<T> rowWitnessMatrix() {
  auto m = gman::Matrix4<T>();
  m[0][0] = reciprocal<T, 3>;
  m[1][1] = reciprocal<T, 7>;
  m[2][2] = reciprocal<T, 11>;
  m[3][0] = reciprocal<T, 5>;
  m[3][1] = reciprocal<T, 13>;
  m[3][2] = reciprocal<T, 17>;
  return m;
}

template <typename T> std::vector<T> partsOf(std::vector<T> const& values) { return values; }

template <typename T> std::vector<T> partsOf(T const (&m)[4][4]) {
  std::vector<T> all;
  for (std::size_t i = 0; i < 4; ++i) {
    for (std::size_t j = 0; j < 4; ++j) {
      all.push_back(m[i][j]);
    }
  }
  return all;
}

template <typename G>
concept Geometry = !std::floating_point<G> && requires(G const& g) { partsOf(g); };

template <typename G> using ScalarOf = typename decltype(partsOf(std::declval<G const&>()))::value_type;

inline std::string componentName(std::size_t count, std::size_t index) {
  if (count == 16) {
    return std::format("[{}][{}]", index / 4, index % 4);
  }
  if (count == 3 || count == 4) {
    return std::string(1, "xyzw"[index]);
  }
  return std::format("[{}]", index);
}

template <std::floating_point T> T bound(T want, int ulps) {
  return static_cast<T>(ulps) * std::numeric_limits<T>::epsilon() * std::max(std::abs(want), static_cast<T>(1));
}

template <std::floating_point T> bool within(T got, T want, int ulps) {
  return got == want || std::abs(got - want) <= bound(want, ulps);
}

template <std::floating_point T> std::string expectation(T want, int ulps, T got) {
  if (ulps == 0) {
    return std::format("expected {}, got {}", want, got);
  }
  return std::format("expected {} within {}, got {}", want, bound(want, ulps), got);
}

template <std::floating_point T> void expectExact(std::string const& what, T got, std::type_identity_t<T> want) {
  check(got == want, std::format("{} : {}", what, expectation(want, 0, got)));
}

template <std::floating_point T>
void expectClose(std::string const& what, T got, std::type_identity_t<T> want, int ulps) {
  check(within(got, want, ulps), std::format("{} : {}", what, expectation(want, ulps, got)));
}

inline void expectBool(std::string const& what, bool got, bool want) {
  check(got == want, std::format("{} : expected {}, got {}", what, want, got));
}

template <typename T> std::string listOf(std::vector<T> const& values) {
  std::string joined;
  for (std::size_t i = 0; i < values.size(); ++i) {
    joined += std::format("{}{}", i == 0 ? "" : ", ", values[i]);
  }
  return joined;
}

template <Geometry G>
void expectParts(std::string const& what, G const& got, std::vector<ScalarOf<G>> const& want, int ulps) {
  const auto parts = partsOf(got);
  if (parts.size() != want.size()) {
    check(false, std::format("{} : expected {} components, got {}", what, want.size(), parts.size()));
    return;
  }
  for (std::size_t i = 0; i < parts.size(); ++i) {
    if (within(parts[i], want[i], ulps)) {
      continue;
    }
    const auto component = componentName(parts.size(), i);
    check(false, std::format("{} : {}: {}", what, component, expectation(want[i], ulps, parts[i])));
    return;
  }
  check(true, std::format("{} : {}", what, listOf(want)));
}

template <Geometry G> void expectExact(std::string const& what, G const& got, std::vector<ScalarOf<G>> const& want) {
  expectParts(what, got, want, 0);
}

template <Geometry G>
void expectClose(std::string const& what, G const& got, std::vector<ScalarOf<G>> const& want, int ulps) {
  expectParts(what, got, want, ulps);
}

template <typename E, typename F> void expectThrows(std::string const& what, F&& action) {
  bool thrown = false;
  try {
    action();
  } catch (E const&) {
    thrown = true;
  }
  check(thrown, std::format("{} : expected a throw", what));
}

} // namespace geometrycheck
