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
 * The six geometry templates, each instantiated for RtFloat and
 * gman::PartnerReal: the two precision aliases, the precision the wider
 * instance buys, the explicit conversions between instances, copies,
 * indexing, the protected components, the RenderMan bridge that only the
 * RtFloat instance carries, and the out-of-line members of both instances.
 *
 * Revert check: deleting a gman::PartnerReal explicit instantiation from a
 * libgman source leaves an out-of-line member of that type undefined, so the
 * link of this test fails; dropping `explicit` from a converting constructor
 * fails the is_convertible assertions; defining gman::WideReal as RtFloat
 * fails the alias assertions.
 */

#include <cmath>
#include <cstddef>
#include <format>
#include <limits>
#include <numbers>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "check.h"
#include "gmanhpoint.h"
#include "gmanmatrix4.h"
#include "gmannormal.h"
#include "gmanpoint.h"
#include "gmanreal.h"
#include "gmanvector.h"
#include "gmanvector4.h"
#include "ri.h"

namespace {

template <typename R> using StandInPartnerReal = std::conditional_t<std::is_same_v<R, double>, float, double>;

template <typename R>
using StandInWideReal =
    std::conditional_t<(std::numeric_limits<R>::digits >= std::numeric_limits<double>::digits), R, double>;

static_assert(std::is_same_v<StandInPartnerReal<float>, double>);
static_assert(std::is_same_v<StandInPartnerReal<double>, float>);
static_assert(std::is_same_v<StandInWideReal<float>, double>);
static_assert(std::is_same_v<StandInWideReal<double>, double>);

static_assert(std::is_same_v<gman::PartnerReal, StandInPartnerReal<RtFloat>>);
static_assert(std::is_same_v<gman::WideReal, StandInWideReal<RtFloat>>);
static_assert(!std::is_same_v<RtFloat, gman::PartnerReal>);
static_assert(std::is_same_v<gman::WideReal, RtFloat> || std::is_same_v<gman::WideReal, gman::PartnerReal>);

using Real = RtFloat;
using Partner = gman::PartnerReal;
constexpr bool partnerIsWider = std::numeric_limits<Partner>::digits > std::numeric_limits<Real>::digits;
using Narrow = std::conditional_t<partnerIsWider, Real, Partner>;
using Wide = std::conditional_t<partnerIsWider, Partner, Real>;

template <typename V>
concept HasX = requires(V v) { v.x; };
template <typename V>
concept HasY = requires(V v) { v.y; };
template <typename V>
concept HasZ = requires(V v) { v.z; };

template <typename V> void assertComponentsProtected() {
  static_assert(!HasX<V>);
  static_assert(!HasY<V>);
  static_assert(!HasZ<V>);
}

struct VectorKind {
  template <typename T> using Type = gman::Vector<T>;
  template <typename T> static Type<T> make(T a, T b, T c, T) { return Type<T>(a, b, c); }
  template <typename T> static std::vector<T> parts(Type<T> const& v) { return {v.getX(), v.getY(), v.getZ()}; }
};

struct PointKind {
  template <typename T> using Type = gman::Point<T>;
  template <typename T> static Type<T> make(T a, T b, T c, T) { return Type<T>(a, b, c); }
  template <typename T> static std::vector<T> parts(Type<T> const& v) { return {v.getX(), v.getY(), v.getZ()}; }
};

struct NormalKind {
  template <typename T> using Type = gman::Normal<T>;
  template <typename T> static Type<T> make(T a, T b, T c, T) { return Type<T>(a, b, c); }
  template <typename T> static std::vector<T> parts(Type<T> const& v) { return {v.getX(), v.getY(), v.getZ()}; }
};

struct HPointKind {
  template <typename T> using Type = gman::HPoint<T>;
  template <typename T> static Type<T> make(T a, T b, T c, T d) { return Type<T>(a, b, c, d); }
  template <typename T> static std::vector<T> parts(Type<T> const& v) {
    return {v.getX(), v.getY(), v.getZ(), v.getW()};
  }
};

struct Vector4Kind {
  template <typename T> using Type = gman::Vector4<T>;
  template <typename T> static Type<T> make(T a, T b, T c, T d) { return Type<T>(a, b, c, d); }
  template <typename T> static std::vector<T> parts(Type<T> const& v) {
    return {v.getX(), v.getY(), v.getZ(), v.getW()};
  }
};

struct Matrix4Kind {
  template <typename T> using Type = gman::Matrix4<T>;
  template <typename T> static Type<T> make(T a, T b, T c, T d) {
    Type<T> m;
    for (RtInt i = 0; i < 4; i++) {
      for (RtInt j = 0; j < 4; j++) {
        m[i][j] = a * static_cast<T>(i) + b * static_cast<T>(j) + c * static_cast<T>(i * j) + d;
      }
    }
    return m;
  }
  template <typename T> static std::vector<T> parts(Type<T> const& m) {
    std::vector<T> all;
    for (RtInt i = 0; i < 4; i++) {
      for (RtInt j = 0; j < 4; j++) {
        all.push_back(m[i][j]);
      }
    }
    return all;
  }
};

template <typename T> T coneDefect() {
  const gman::Point<T> eye(0, 0, 0);
  const auto centre = gman::Point<T>(0, 0, 1) * 2;
  const auto radius = static_cast<T>(2e-4);
  const gman::Vector<T> toCentre(eye, centre);
  const auto distanceSquared = toCentre.dot(toCentre);
  const auto sineSquared = radius * radius / distanceSquared;
  return 1 - std::sqrt(1 - sineSquared);
}

template <typename T> void checkPrecision(std::string const& scalar) {
  const auto defect = coneDefect<T>();
  if (std::numeric_limits<T>::digits == 24) {
    check(defect == 0, std::format("{}: the sphere-cone quantity 1 - cos is exactly 0, measured {}", scalar, defect));
    return;
  }
  if (std::numeric_limits<T>::digits >= 53) {
    const gman::WideReal measured = defect;
    const auto error = std::abs(measured - gman::WideReal(5e-9));
    check(error < gman::WideReal(1e-10),
          std::format("{}: the sphere-cone quantity 1 - cos is within 1e-10 of 5e-9, measured {}", scalar, defect));
  }
}

template <typename T> void checkDotKeepsItsScalarWidth(std::string const& scalar) {
  constexpr T ax = T(1) / 3;
  constexpr T ay = T(1) / 7;
  constexpr T az = T(1) / 11;
  constexpr T bx = T(1) / 5;
  constexpr T by = T(1) / 13;
  constexpr T bz = T(1) / 17;
  constexpr T expected = ax * bx + ay * by + az * bz;
  const auto measured = gman::Vector<T>(ax, ay, az).dot(gman::Vector<T>(bx, by, bz));
  const auto error = std::abs(measured - expected);
  check(error <= 2 * std::numeric_limits<T>::epsilon() * expected,
        std::format("{}: Vector dot accumulates in its own scalar, expected {}, measured {}", scalar, expected,
                    measured));
}

template <typename T> void checkNormalizeKeepsItsScalarWidth(std::string const& scalar) {
  auto diagonal = gman::Vector<T>(1, 1, 1);
  diagonal.normalize();
  constexpr T expected = std::numbers::inv_sqrt3_v<T>;
  const auto measured = diagonal.getX();
  const auto error = std::abs(measured - expected);
  check(error <= 4 * std::numeric_limits<T>::epsilon() * expected,
        std::format("{}: Vector normalize works in its own scalar, expected {}, measured {}", scalar, expected,
                    measured));
}

template <typename Kind> void checkConversions(std::string const& kind) {
  using NarrowType = typename Kind::template Type<Narrow>;
  using WideType = typename Kind::template Type<Wide>;
  static_assert(std::is_constructible_v<NarrowType, WideType const&>);
  static_assert(std::is_constructible_v<WideType, NarrowType const&>);
  static_assert(!std::is_convertible_v<WideType const&, NarrowType>);
  static_assert(!std::is_convertible_v<NarrowType const&, WideType>);

  const auto narrow = Kind::template make<Narrow>(static_cast<Narrow>(0.1), static_cast<Narrow>(0.3),
                                                  static_cast<Narrow>(-0.7), static_cast<Narrow>(1.9));
  const WideType widened(narrow);
  const NarrowType roundTrip(widened);
  const auto original = Kind::template parts<Narrow>(narrow);
  const auto widenedParts = Kind::template parts<Wide>(widened);
  const auto roundTripParts = Kind::template parts<Narrow>(roundTrip);
  bool widenedExact = widenedParts.size() == original.size();
  bool roundTripExact = roundTripParts.size() == original.size();
  for (std::size_t i = 0; widenedExact && roundTripExact && i < original.size(); ++i) {
    widenedExact = widenedExact && widenedParts[i] == static_cast<Wide>(original[i]);
    roundTripExact = roundTripExact && roundTripParts[i] == original[i];
  }
  check(widenedExact, kind + ": the narrower instance widens exactly");
  check(roundTripExact, kind + ": the narrower instance converted to the wider and back keeps every component");

  const auto wide = Kind::template make<Wide>(Wide(1) / 3, Wide(2) / 7, Wide(-5) / 11, Wide(13) / 17);
  const NarrowType narrowed(wide);
  const auto wideParts = Kind::template parts<Wide>(wide);
  const auto narrowedParts = Kind::template parts<Narrow>(narrowed);
  bool narrowedExact = narrowedParts.size() == wideParts.size();
  bool narrowingLoses = false;
  for (std::size_t i = 0; narrowedExact && i < wideParts.size(); ++i) {
    narrowedExact = narrowedParts[i] == static_cast<Narrow>(wideParts[i]);
    narrowingLoses = narrowingLoses || static_cast<Wide>(narrowedParts[i]) != wideParts[i];
  }
  check(narrowedExact, kind + ": the wider instance narrows to each component's static_cast");
  check(narrowingLoses, kind + ": the sample loses precision when narrowed");
}

template <typename Kind, typename T> void checkCopies(std::string const& name) {
  using Type = typename Kind::template Type<T>;
  static_assert(std::is_copy_constructible_v<Type>);
  static_assert(std::is_copy_assignable_v<Type>);

  const Type source = Kind::template make<T>(T(1) / 4, T(-3) / 8, T(5) / 2, T(7) / 16);
  const Type copy(source);
  Type assigned = Kind::template make<T>(T(9), T(9), T(9), T(9));
  assigned = source;
  const auto sourceParts = Kind::template parts<T>(source);
  check(Kind::template parts<T>(copy) == sourceParts, name + ": a copy reproduces the source");
  check(Kind::template parts<T>(assigned) == sourceParts, name + ": a copy assignment reproduces the source");
}

template <typename Kind> void checkKind(std::string const& name) {
  checkConversions<Kind>(name);
  checkCopies<Kind, Real>(name + "<RtFloat>");
  checkCopies<Kind, Partner>(name + "<PartnerReal>");
  assertComponentsProtected<typename Kind::template Type<Real>>();
  assertComponentsProtected<typename Kind::template Type<Partner>>();
}

template <typename T> void checkIndexing(std::string const& scalar) {
  static_assert(std::is_same_v<decltype(std::declval<gman::Vector<T>&>()[std::size_t{}]), T&>);
  static_assert(std::is_same_v<decltype(std::declval<gman::Vector<T> const&>()[std::size_t{}]), T const&>);
  static_assert(std::is_same_v<decltype(std::declval<gman::Normal<T>&>()[std::size_t{}]), T&>);
  static_assert(std::is_same_v<decltype(std::declval<gman::Point<T>&>()[std::size_t{}]), T const&>);
  static_assert(std::is_same_v<decltype(std::declval<gman::Point<T> const&>()[std::size_t{}]), T const&>);

  gman::Vector<T> vector(1, 2, 3);
  gman::Normal<T> normal(4, 5, 6);
  const gman::Point<T> point(7, 8, 9);
  const T vectorParts[3] = {vector.getX(), vector.getY(), vector.getZ()};
  const T normalParts[3] = {normal.getX(), normal.getY(), normal.getZ()};
  const T pointParts[3] = {point.getX(), point.getY(), point.getZ()};
  bool vectorMatches = true;
  bool normalMatches = true;
  bool pointMatches = true;
  bool constVectorMatches = true;
  bool constNormalMatches = true;
  for (std::size_t i = 0; i < 3; ++i) {
    vectorMatches = vectorMatches && vector[i] == vectorParts[i];
    normalMatches = normalMatches && normal[i] == normalParts[i];
    pointMatches = pointMatches && point[i] == pointParts[i];
    constVectorMatches = constVectorMatches && std::as_const(vector)[i] == vectorParts[i];
    constNormalMatches = constNormalMatches && std::as_const(normal)[i] == normalParts[i];
  }
  check(vectorMatches, scalar + ": Vector operator[] matches getX, getY and getZ");
  check(normalMatches, scalar + ": Normal operator[] matches getX, getY and getZ");
  check(pointMatches, scalar + ": Point operator[] matches getX, getY and getZ");
  check(constVectorMatches, scalar + ": a const Vector operator[] matches getX, getY and getZ");
  check(constNormalMatches, scalar + ": a const Normal operator[] matches getX, getY and getZ");

  vector[0] = 10;
  vector[1] = 20;
  vector[2] = 30;
  check(vector.getX() == 10 && vector.getY() == 20 && vector.getZ() == 30,
        scalar + ": Vector operator[] writes the component it names");
}

template <typename T> void checkOutOfLineMembers(std::string const& scalar) {
  gman::Matrix4<T> columnTranslation;
  columnTranslation[0][3] = 10;
  columnTranslation[1][3] = 20;
  columnTranslation[2][3] = 30;

  gman::Vector<T> vector(1, 2, 3);
  vector *= columnTranslation;
  check(vector.getX() == 11 && vector.getY() == 22 && vector.getZ() == 33,
        scalar + ": Vector operator*= Matrix4 transforms the vector");

  gman::Point<T> point(1, 2, 3);
  point *= columnTranslation;
  check(point == gman::Point<T>(11, 22, 33), scalar + ": Point operator*= Matrix4 transforms the point");
  check(point != gman::Point<T>(11, 22, 34), scalar + ": Point operator!= tells points apart");

  gman::Matrix4<T> scaling;
  scaling.scale(2, 4, 8);
  auto inverse = scaling;
  inverse.invert();
  check(inverse[0][0] == T(1) / 2 && inverse[1][1] == T(1) / 4 && inverse[2][2] == T(1) / 8 && inverse[3][3] == 1,
        scalar + ": Matrix4 invert inverts a scale");
  const auto product = scaling * inverse;
  bool identity = true;
  for (RtInt i = 0; i < 4; i++) {
    for (RtInt j = 0; j < 4; j++) {
      identity = identity && product[i][j] == (i == j ? 1 : 0);
    }
  }
  check(identity, scalar + ": a Matrix4 times its inverse is the identity");

  const gman::Normal<T> normal(1, 2, 3);
  check(normal.getX() == 1 && normal.getY() == 2 && normal.getZ() == 3, scalar + ": Normal constructs from components");
  const gman::Normal<T> fromPoint(point);
  check(fromPoint.getX() == 11 && fromPoint.getY() == 22 && fromPoint.getZ() == 33,
        scalar + ": Normal constructs from a Point");

  gman::HPoint<T> homogeneous(2, 4, 6, 2);
  homogeneous.wToOne();
  check(homogeneous.getX() == 1 && homogeneous.getY() == 2 && homogeneous.getZ() == 3 && homogeneous.getW() == 1,
        scalar + ": HPoint wToOne divides by w");

  gman::Vector4<T> four(1, 2, 3, 1);
  gman::Matrix4<T> rowTranslation;
  rowTranslation.trans(1, 2, 3);
  four *= rowTranslation;
  check(four.getX() == 2 && four.getY() == 4 && four.getZ() == 6 && four.getW() == 1,
        scalar + ": Vector4 operator*= Matrix4 transforms the vector");
}

void checkBridge() {
  static_assert(std::is_same_v<decltype(std::declval<gman::Matrix4<RtFloat> const&>().get()), RtMatrix const&>);
  static_assert(sizeof(gman::Matrix4<RtFloat>) == sizeof(RtMatrix));

  static_assert(std::is_constructible_v<gman::Vector<RtFloat>, RtFloat const*>);
  static_assert(std::is_constructible_v<gman::Vector<RtFloat>, RtFloat*>);
  static_assert(std::is_constructible_v<gman::Point<RtFloat>, RtFloat const*>);
  static_assert(std::is_constructible_v<gman::Point<RtFloat>, RtFloat*>);
  static_assert(std::is_constructible_v<gman::HPoint<RtFloat>, RtFloat const*>);
  static_assert(std::is_constructible_v<gman::HPoint<RtFloat>, RtFloat*>);
  static_assert(std::is_constructible_v<gman::Vector4<RtFloat>, RtFloat const*>);
  static_assert(std::is_constructible_v<gman::Vector4<RtFloat>, RtFloat*>);
  static_assert(std::is_constructible_v<gman::Matrix4<RtFloat>, RtMatrix>);

  static_assert(!std::is_constructible_v<gman::Vector<gman::PartnerReal>, RtFloat const*>);
  static_assert(!std::is_constructible_v<gman::Vector<gman::PartnerReal>, RtFloat*>);
  static_assert(!std::is_constructible_v<gman::Point<gman::PartnerReal>, RtFloat const*>);
  static_assert(!std::is_constructible_v<gman::Point<gman::PartnerReal>, RtFloat*>);
  static_assert(!std::is_constructible_v<gman::HPoint<gman::PartnerReal>, RtFloat const*>);
  static_assert(!std::is_constructible_v<gman::HPoint<gman::PartnerReal>, RtFloat*>);
  static_assert(!std::is_constructible_v<gman::Vector4<gman::PartnerReal>, RtFloat const*>);
  static_assert(!std::is_constructible_v<gman::Vector4<gman::PartnerReal>, RtFloat*>);
  static_assert(!std::is_constructible_v<gman::Matrix4<gman::PartnerReal>, RtMatrix>);

  const RtFloat parts[4] = {1, 2, 3, 4};
  const gman::Vector4<RtFloat> four(parts);
  check(four.getX() == 1 && four.getY() == 2 && four.getZ() == 3 && four.getW() == 4,
        "the RtFloat Vector4 reads four components from an RtFloat array");
  const GMANVector aliased(parts);
  check(aliased.getZ() == 3, "GMANVector names the RtFloat Vector");
}

} // namespace

int main() {
  checkBridge();

  checkPrecision<Real>("RtFloat");
  checkPrecision<Partner>("PartnerReal");
  checkDotKeepsItsScalarWidth<Real>("RtFloat");
  checkDotKeepsItsScalarWidth<Partner>("PartnerReal");
  checkNormalizeKeepsItsScalarWidth<Real>("RtFloat");
  checkNormalizeKeepsItsScalarWidth<Partner>("PartnerReal");

  checkKind<VectorKind>("Vector");
  checkKind<PointKind>("Point");
  checkKind<NormalKind>("Normal");
  checkKind<HPointKind>("HPoint");
  checkKind<Vector4Kind>("Vector4");
  checkKind<Matrix4Kind>("Matrix4");

  checkIndexing<Real>("RtFloat");
  checkIndexing<Partner>("PartnerReal");

  checkOutOfLineMembers<Real>("RtFloat");
  checkOutOfLineMembers<Partner>("PartnerReal");

  return checkSummary("geometrytemplates ok");
}
