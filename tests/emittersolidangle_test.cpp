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
 * gman::sample draws a polygon mesh's points uniformly in the solid angle its
 * emitting faces subtend from the receiving point; a disk's, near it,
 * uniformly in the solid angle of its bounding rectangle, keeping the draws
 * that land on the disk; and a sphere's uniformly in the cone it subtends, a
 * zone near it keeping the draws that land on the zone.
 * gman::lightSolidAnglePdf answers the density each draw reports. Each case
 * builds an emitter by hand in camera space and draws 2^14 stratified (u1, u2)
 * from one receiving point; a draw is accepted when its pdf is positive and
 * its Cl is not black, and A is the fraction accepted.
 *
 * a: A is exactly 1 for a mesh, a full sphere and a disk beyond its switch, and
 *    the integral of lightSolidAnglePdf over the directions in which the
 *    emitting side is visible equals A within 1e-3 plus five binomial sigma. A
 *    flat region, a disk's an inscribed polygon of 64 sides a quarter turn, is
 *    swept along each edge and by the cosine off its normal, in which the
 *    solid-angle element is exact; a rigidly placed full sphere over its cone
 *    of directions; any other sphere over its area, by cos / d^2 and Nanson's
 *    area factor |det L| |L^-T n|.
 * b: every accepted draw's ray from the receiving point along wi meets the
 *    shape (its own intersect), the hit lies within 1e-4 of the distance from
 *    the drawn point, and lightSolidAnglePdf there, with the hit's normal,
 *    equals the draw's pdf within 1e-5. wi and the points are RtFloat, so each
 *    bound adds twice the float rounding a draw carries, epsilon (d + 4 X), X
 *    the size of the coordinates p has in camera and object space, as that
 *    rounding moves the hit along a ray meeting the shape at incidence g, by
 *    1 / g, and moves an area density's distance and cosine, by 2 / d +
 *    curvature / g. A ray rounded off a tangent may miss.
 * c: the fraction of accepted draws landing, at p + distance wi, in each of
 *    four regions (a triangle's midpoint sub-triangles, a quad's quadrants, a
 *    disk sector's or a zone's four equal turns, each face of a two-face mesh)
 *    equals that region's integral of the pdf over A within five binomial
 *    sigma plus the quadrature's 1e-3.
 * d: for a single triangle, every draw's pdf times the triangle's solid angle,
 *    by Van Oosterom and Strackee's formula, equals 1 within 1e-5 relative.
 * e: degenerate inputs (a zero-area fan triangle, a receiving point in a
 *    disk's or a polygon's plane, 1e-6 above a disk, on a sphere or at its
 *    centre) answer finite fields, a pdf of 0 or more and a finite
 *    lightSolidAnglePdf at every accepted draw's hit.
 * f: a disk draws through its rectangle nearer it than 0.75 times the radius
 *    of a full disk of its sector's area, and a zone in the cone nearer its
 *    full sphere than 1.5 times its radius; there every accepted draw reports
 *    1 / the solid angle drawn over. Beyond, each reports the area density d^2
 *    / (A J cos) at its point, within 1e-5 plus the float rounding b allows.
 *    At 0.9 and 1.1 times that distance the two sides differ.
 */

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <initializer_list>
#include <limits>
#include <memory>
#include <numbers>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "check.h"
#include "gmancolor.h"
#include "gmanemitter.h"
#include "gmaninvertiblematrix.h"
#include "gmanlightsourcemgr.h"
#include "gmanmath.h"
#include "gmanmatrix4.h"
#include "gmannormal.h"
#include "gmanparameterlist.h"
#include "gmanpoint.h"
#include "gmanray.h"
#include "gmanraydisk.h"
#include "gmanraypolygon.h"
#include "gmanraypolygonmesh.h"
#include "gmanraysphere.h"
#include "gmansampling.h"
#include "gmanshading.h"
#include "gmanvector.h"
#include "maketransform.h"
#include "ri.h"
#include "taggedmesh.h"

namespace {

using namespace taggedmesh;

constexpr std::uint32_t kSeed = 0x3b5e9a27u;
constexpr std::uint32_t kDraws = 1u << 14;
constexpr double kSigmas = 5.0;
constexpr double kIntegralFloor = 1.0e-3;
constexpr double kPdfTolerance = 1.0e-5;
constexpr double kHitTolerance = 1.0e-4;
constexpr double kWiRounding = std::numeric_limits<RtFloat>::epsilon();
constexpr double kRoundings = 4.0;
constexpr double kMargin = 2.0;
constexpr double kPanelGrowth = 4.0;
constexpr int kEdgeNodes = 8;
constexpr int kCosineNodes = 16;
constexpr int kNewtonIterations = 100;
constexpr double kNewtonStep = 1.0e-15;
constexpr int kArcSegments = 64;
constexpr double kDiskSwitch = 0.75;
constexpr double kZoneSwitch = 1.5;
constexpr int kTurnPanels = 16;
constexpr int kZonePanels = 8;
const GMANColor kRadiance(1.0f, 2.0f, 3.0f);

struct Vec {
  double x;
  double y;
  double z;
};

Vec operator+(Vec const& a, Vec const& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }

Vec operator-(Vec const& a, Vec const& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }

Vec operator*(double s, Vec const& a) { return {s * a.x, s * a.y, s * a.z}; }

double dot(Vec const& a, Vec const& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

Vec cross(Vec const& a, Vec const& b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }

double length(Vec const& a) { return std::sqrt(dot(a, a)); }

Vec normalized(Vec const& a) { return (1.0 / length(a)) * a; }

Vec toVec(GMANPoint const& p) { return {p.getX(), p.getY(), p.getZ()}; }

Vec toVec(GMANVector const& v) { return {v.getX(), v.getY(), v.getZ()}; }

GMANPoint toPoint(Vec const& a) {
  return GMANPoint(static_cast<RtFloat>(a.x), static_cast<RtFloat>(a.y), static_cast<RtFloat>(a.z));
}

GMANVector toVector(Vec const& a) {
  return GMANVector(static_cast<RtFloat>(a.x), static_cast<RtFloat>(a.y), static_cast<RtFloat>(a.z));
}

GMANNormal toNormal(Vec const& a) {
  return GMANNormal(static_cast<RtFloat>(a.x), static_cast<RtFloat>(a.y), static_cast<RtFloat>(a.z));
}

// A unit vector perpendicular to the unit vector n.
Vec perpendicular(Vec const& n) {
  if (std::fabs(n.x) < std::fabs(n.y)) {
    return normalized(cross(n, Vec{1.0, 0.0, 0.0}));
  }
  return normalized(cross(n, Vec{0.0, 1.0, 0.0}));
}

struct QuadratureRule {
  std::vector<double> nodes;
  std::vector<double> weights;
};

// The Legendre polynomial of degree n at x and its derivative there.
std::pair<double, double> legendre(int n, double x) {
  double previous = 1.0;
  double value = x;
  for (int k = 2; k <= n; ++k) {
    const auto next = ((2.0 * k - 1.0) * x * value - (k - 1.0) * previous) / k;
    previous = value;
    value = next;
  }
  return {value, n * (x * value - previous) / (x * x - 1.0)};
}

// The n-point Gauss-Legendre rule on [0, 1].
QuadratureRule gaussLegendre(int n) {
  QuadratureRule rule;
  for (int i = 0; i < n; ++i) {
    double x = std::cos(std::numbers::pi * (i + 0.75) / (n + 0.5));
    for (int iteration = 0; iteration < kNewtonIterations; ++iteration) {
      const auto [value, slope] = legendre(n, x);
      const auto step = value / slope;
      x -= step;
      if (std::fabs(step) < kNewtonStep) {
        break;
      }
    }
    const auto slope = legendre(n, x).second;
    rule.nodes.push_back((1.0 - x) / 2.0);
    rule.weights.push_back(1.0 / ((1.0 - x * x) * slope * slope));
  }
  return rule;
}

// A flat region of an emitter: its loop in camera space and the unit normal
// of its emitting side.
struct Region {
  std::vector<Vec> loop;
  Vec normal;
};

// The panel ends of [0, 1] for an edge whose closest point to the foot lies
// at parameter closest: panels kPanelGrowth times wider at each step away
// from it, the first of width scale, where the integrand varies fastest.
std::vector<double> gradedBreaks(double closest, double scale) {
  std::vector<double> breaks = {0.0, 1.0};
  if (closest > 0.0 && closest < 1.0) {
    breaks.push_back(closest);
  }
  for (double width = scale; width < 1.0; width *= kPanelGrowth) {
    for (double const edge : {closest - width, closest + width}) {
      if (edge > 0.0 && edge < 1.0) {
        breaks.push_back(edge);
      }
    }
  }
  std::ranges::sort(breaks);
  return breaks;
}

// The integral of lightSolidAnglePdf over the directions from p that meet
// region; 0 when p lies on or behind its plane. Each edge (a, b) bounds the
// triangle (f, a, b), f the foot of p on the plane. The edge's parameter t
// and the cosine s of a direction off the normal sweep that triangle with
// solid-angle element ds dphi, dphi = cross(a - f, b - a) / |x(t) - f|^2 dt;
// the signed triangles sum to the region.
double regionIntegral(gman::Emitter const& emitter, GMANPoint const& p, Region const& region) {
  const auto origin = toVec(p);
  const auto height = dot(origin - region.loop[0], region.normal);
  if (!(height > 0.0)) {
    return 0.0;
  }
  const auto foot = origin - height * region.normal;
  const auto e1 = perpendicular(region.normal);
  const auto e2 = cross(region.normal, e1);
  const auto normal = toNormal(region.normal);
  const auto edgeRule = gaussLegendre(kEdgeNodes);
  const auto cosineRule = gaussLegendre(kCosineNodes);
  double total = 0.0;
  double signedArea = 0.0;
  for (std::size_t i = 0; i < region.loop.size(); ++i) {
    const auto a = region.loop[i] - foot;
    const auto b = region.loop[(i + 1) % region.loop.size()] - foot;
    const auto ax = dot(a, e1);
    const auto ay = dot(a, e2);
    const auto dx = dot(b, e1) - ax;
    const auto dy = dot(b, e2) - ay;
    const auto crossAB = ax * dy - ay * dx;
    signedArea += crossAB / 2.0;
    if (crossAB == 0.0) {
      continue;
    }
    const auto edgeSquared = dx * dx + dy * dy;
    const auto closest = std::clamp(-(ax * dx + ay * dy) / edgeSquared, 0.0, 1.0);
    const auto scale = std::sqrt((height * height + crossAB * crossAB / edgeSquared) / edgeSquared);
    std::vector<double> const breaks = gradedBreaks(closest, scale);
    for (std::size_t panel = 0; panel + 1 < breaks.size(); ++panel) {
      const auto width = breaks[panel + 1] - breaks[panel];
      for (std::size_t k = 0; k < edgeRule.nodes.size(); ++k) {
        const auto t = breaks[panel] + width * edgeRule.nodes[k];
        const auto x = ax + t * dx;
        const auto y = ay + t * dy;
        const auto reachSquared = x * x + y * y;
        const auto reach = std::sqrt(reachSquared);
        const auto edgeCosine = height / std::sqrt(height * height + reachSquared);
        const auto along = (x / reach) * e1 + (y / reach) * e2;
        double inner = 0.0;
        for (std::size_t j = 0; j < cosineRule.nodes.size(); ++j) {
          const auto cosine = edgeCosine + (1.0 - edgeCosine) * cosineRule.nodes[j];
          const auto rho = height * std::sqrt((1.0 - cosine) * (1.0 + cosine)) / cosine;
          inner += cosineRule.weights[j] * gman::lightSolidAnglePdf(emitter, p, toPoint(foot + rho * along), normal);
        }
        total += width * edgeRule.weights[k] * crossAB / reachSquared * (1.0 - edgeCosine) * inner;
      }
    }
  }
  return (signedArea < 0.0) ? -total : total;
}

// The solid angle triangle (a, b, c) subtends from p, by Van Oosterom and
// Strackee's formula.
double vanOosteromStrackee(Vec const& p, Vec const& a, Vec const& b, Vec const& c) {
  const auto ra = a - p;
  const auto rb = b - p;
  const auto rc = c - p;
  const auto la = length(ra);
  const auto lb = length(rb);
  const auto lc = length(rc);
  const auto numerator = std::fabs(dot(ra, cross(rb, rc)));
  const auto denominator = la * lb * lc + dot(ra, rb) * lc + dot(ra, rc) * lb + dot(rb, rc) * la;
  return 2.0 * std::atan2(numerator, denominator);
}

bool isBlack(GMANColor const& c) { return c.getRed() == 0.0f && c.getGreen() == 0.0f && c.getBlue() == 0.0f; }

bool accepted(gman::EmitterSample const& s) { return s.pdf > 0.0f && !isBlack(s.Cl); }

// The strategy f expects p to select: uniform in a solid angle, every
// accepted draw reporting one pdf, or by area, each reporting the area
// density at its point.
enum class Strategy { unchecked, solidAngle, area };

// One receiving point's view of one emitter: its regions' integrals, which
// region a hit lies in, and what a, d and f expect. solidAngle, when known,
// is the solid angle a strategy by solid angle draws over; areaDensity
// answers the area density at a draw's point.
struct Case {
  std::string label;
  gman::Emitter emitter;
  GMANPoint p;
  std::vector<double> regionIntegrals;
  std::function<std::size_t(Vec const&)> regionOf;
  bool acceptsEvery = false;
  std::optional<double> triangleSolidAngle;
  Strategy strategy = Strategy::unchecked;
  std::optional<double> solidAngle;
  std::function<double(gman::EmitterSample const&)> areaDensity;
  double curvature = 0.0;
  double scale = 0.0;
  std::function<Vec(Vec const&)> normalAt;
};

// Draws from c's receiving point and checks a to d.
void runCase(Case const& c, std::uint32_t dim) {
  std::vector<std::uint32_t> counts(c.regionIntegrals.size(), 0);
  std::uint32_t acceptedCount = 0;
  std::uint32_t missed = 0;
  std::uint32_t tangentMisses = 0;
  double worstPdf = 0.0;
  double worstHit = 0.0;
  double worstSolidAngle = 0.0;
  double worstStrategy = 0.0;
  double lowestPdf = std::numeric_limits<double>::max();
  double highestPdf = 0.0;
  for (std::uint32_t i = 0; i < kDraws; ++i) {
    gman::Sample2D const uv = gman::sample2D(kSeed, dim, 0, i, kDraws, 0u);
    gman::EmitterSample const s = gman::sample(c.emitter, c.p, uv.u1, uv.u2);
    if (!accepted(s)) {
      continue;
    }
    ++acceptedCount;
    if (c.triangleSolidAngle) {
      worstSolidAngle = std::max(worstSolidAngle, std::fabs(s.pdf * *c.triangleSolidAngle - 1.0));
    }
    lowestPdf = std::min(lowestPdf, (double)s.pdf);
    highestPdf = std::max(highestPdf, (double)s.pdf);
    if (c.strategy == Strategy::solidAngle && c.solidAngle) {
      worstStrategy = std::max(worstStrategy, std::fabs(s.pdf * *c.solidAngle - 1.0));
    }
    const auto distance = (double)s.distance;
    const auto drawn = toVec(c.p) + distance * toVec(s.wi);
    const auto rounding = kWiRounding * (distance + kRoundings * c.scale);
    const auto region = c.regionOf(drawn);
    if (region < counts.size()) {
      ++counts[region];
    }
    GMANHit hit;
    if (!c.emitter.shape->intersect(GMANRay(c.p, s.wi), hit)) {
      double grazing = 1.0;
      if (c.normalAt) {
        grazing = std::fabs(dot(c.normalAt(drawn), toVec(s.wi)));
      }
      if (grazing * grazing <= 2.0 * c.curvature * rounding) {
        ++tangentMisses;
      } else {
        ++missed;
      }
      continue;
    }
    const auto incidence = std::fabs(dot(normalized(toVec(hit.normal)), toVec(s.wi)));
    const auto bend = 2.0 + distance * c.curvature / incidence;
    if (c.strategy == Strategy::area) {
      const auto density = c.areaDensity(s);
      const auto allowed = kPdfTolerance + kMargin * rounding / distance * bend;
      worstStrategy = std::max(worstStrategy, std::fabs((double)s.pdf - density) / density / allowed);
    }
    const auto offset = length(toVec(hit.point) - drawn);
    const auto offsetAllowed = kHitTolerance * distance + kMargin * rounding * (1.0 + 1.0 / incidence);
    worstHit = std::max(worstHit, offset / offsetAllowed);
    const auto pdf = gman::lightSolidAnglePdf(c.emitter, c.p, hit.point, toNormal(toVec(hit.normal)));
    const auto pdfAllowed = kPdfTolerance + kMargin * (offset + rounding) / distance * bend;
    worstPdf = std::max(worstPdf, std::fabs((double)pdf - (double)s.pdf) / (double)s.pdf / pdfAllowed);
  }

  const auto acceptance = (double)acceptedCount / kDraws;
  double integral = 0.0;
  for (double const regionValue : c.regionIntegrals) {
    integral += regionValue;
  }
  const auto binomial = std::sqrt(acceptance * (1.0 - acceptance) / kDraws);
  std::printf("%s: A %.6f, integral %.6f, worst b pdf %.2f and hit %.2f of their allowances, %u missed, %u at a "
              "tangent",
              c.label.c_str(), acceptance, integral, worstPdf, worstHit, missed, tangentMisses);
  if (c.triangleSolidAngle) {
    std::printf(", worst d %.2e", worstSolidAngle);
  }
  std::printf("\n");

  if (c.acceptsEvery) {
    check(acceptedCount == kDraws, c.label + " a: every draw is accepted");
  }
  check(acceptedCount > 0, c.label + ": some draw is accepted");
  check(std::fabs(integral - acceptance) <= kIntegralFloor + kSigmas * binomial,
        c.label + " a: the pdf integrates to A within 1e-3 plus five binomial sigma");
  check(missed == 0, c.label + " b: every accepted draw's ray meets the shape, but one rounding left of a tangent");
  check(worstPdf <= 1.0, c.label + " b: lightSolidAnglePdf at each hit equals the draw's pdf within 1e-5 plus the "
                                   "change the hit's offset carries");
  check(worstHit <= 1.0, c.label + " b: each hit lies within 1e-4 of the distance plus wi's rounding at its incidence "
                                   "from the drawn point");
  if (c.triangleSolidAngle) {
    check(worstSolidAngle <= kPdfTolerance, c.label + " d: pdf times the triangle's solid angle is 1 within 1e-5");
  }
  if (c.strategy == Strategy::solidAngle) {
    std::printf("  f: by solid angle, pdf spread %.2e, worst against 1 / solid angle %.2e\n",
                (highestPdf - lowestPdf) / lowestPdf, worstStrategy);
    check((highestPdf - lowestPdf) / lowestPdf <= kPdfTolerance,
          c.label + " f: every accepted draw reports one pdf within 1e-5");
    check(worstStrategy <= kPdfTolerance, c.label + " f: that pdf is 1 / the solid angle drawn over within 1e-5");
  }
  if (c.strategy == Strategy::area) {
    std::printf("  f: by area, worst against the area density %.2f of its allowance\n", worstStrategy);
    check(worstStrategy <= 1.0, c.label + " f: each pdf equals the area density at its point within 1e-5 plus "
                                          "wi's rounding at its incidence");
  }
  if (acceptedCount == 0 || counts.size() < 2) {
    return;
  }
  for (std::size_t r = 0; r < counts.size(); ++r) {
    const auto share = (double)counts[r] / acceptedCount;
    const auto expected = c.regionIntegrals[r] / acceptance;
    const auto sigma = std::sqrt(expected * (1.0 - expected) / acceptedCount);
    std::printf("  region %zu: share %.5f, expected %.5f, sigma %.5f\n", r, share, expected, sigma);
    check(std::fabs(share - expected) <= kIntegralFloor + kSigmas * sigma,
          c.label + " c: region " + std::to_string(r) + "'s share of the draws matches its integral");
  }
}

// e: every field of every draw from p is finite, every pdf is 0 or more, and
// lightSolidAnglePdf at each accepted draw's hit is finite.
void checkDegenerate(gman::Emitter const& emitter, GMANPoint const& p, std::string const& label, std::uint32_t dim) {
  bool finiteFields = true;
  bool pdfNotNegative = true;
  bool finiteHitPdf = true;
  std::uint32_t acceptedCount = 0;
  for (std::uint32_t i = 0; i < kDraws; ++i) {
    gman::Sample2D const uv = gman::sample2D(kSeed, dim, 0, i, kDraws, 0u);
    gman::EmitterSample const s = gman::sample(emitter, p, uv.u1, uv.u2);
    std::initializer_list<RtFloat> const fields = {s.wi.getX(),
                                                   s.wi.getY(),
                                                   s.wi.getZ(),
                                                   s.distance,
                                                   s.shadowTarget.getX(),
                                                   s.shadowTarget.getY(),
                                                   s.shadowTarget.getZ(),
                                                   s.Cl.getRed(),
                                                   s.Cl.getGreen(),
                                                   s.Cl.getBlue(),
                                                   s.pdf};
    finiteFields = finiteFields && std::ranges::all_of(fields, [](RtFloat v) { return std::isfinite(v); });
    pdfNotNegative = pdfNotNegative && s.pdf >= 0.0f;
    if (!accepted(s)) {
      continue;
    }
    ++acceptedCount;
    GMANHit hit;
    if (emitter.shape->intersect(GMANRay(p, s.wi), hit)) {
      finiteHitPdf =
          finiteHitPdf && std::isfinite(gman::lightSolidAnglePdf(emitter, p, hit.point, toNormal(toVec(hit.normal))));
    }
  }
  std::printf("%s: %u of %u draws accepted\n", label.c_str(), acceptedCount, kDraws);
  check(finiteFields, label + " e: every field of every draw is finite");
  check(pdfNotNegative, label + " e: every pdf is 0 or positive");
  check(finiteHitPdf, label + " e: lightSolidAnglePdf at each accepted draw's hit is finite");
}

// loop, reversed when its Newell normal points away from side.
std::vector<GMANPoint> facing(std::vector<GMANPoint> loop, Vec const& side) {
  Vec newell{0.0, 0.0, 0.0};
  for (std::size_t i = 0; i < loop.size(); ++i) {
    newell = newell + cross(toVec(loop[i]), toVec(loop[(i + 1) % loop.size()]));
  }
  if (dot(newell, side) < 0.0) {
    std::ranges::reverse(loop);
  }
  return loop;
}

std::vector<Vec> toVecs(std::vector<GMANPoint> const& loop) {
  std::vector<Vec> result;
  for (GMANPoint const& point : loop) {
    result.push_back(toVec(point));
  }
  return result;
}

Vec midpoint(Vec const& a, Vec const& b) { return 0.5 * (a + b); }

// The regions of a mesh case: each face's loop with its plane normal.
std::vector<Region> faceRegions(GMANRayPolygonMesh const& mesh) {
  std::vector<Region> regions;
  for (std::size_t f = 0; f < mesh.getFaceCount(); ++f) {
    GMANRayPolygon const& face = mesh.getFace(f);
    regions.push_back({toVecs(face.getOuterLoop()), normalized(toVec(face.getPlaneNormal()))});
  }
  return regions;
}

std::vector<double> integrals(gman::Emitter const& emitter, GMANPoint const& p, std::vector<Region> const& regions) {
  std::vector<double> result;
  for (Region const& region : regions) {
    result.push_back(regionIntegral(emitter, p, region));
  }
  return result;
}

// A one-face mesh emitter of loop and its regions.
struct MeshEmitter {
  GMANLight light{GMAN_LIGHT_AREA, kRadiance, GMANPoint(), GMANVector()};
  std::unique_ptr<GMANRayPolygonMesh> mesh;

  explicit MeshEmitter(std::vector<std::vector<GMANPoint>> const& loops)
      : mesh(buildTaggedMesh(solidFaces(loops), areaLightAppearance(light))) {}

  gman::Emitter emitter() const { return {&light, mesh.get(), 1.0f}; }
};

// The equilateral triangle of side 1 centred on the origin in the plane y =
// 0, emitting toward +y.
std::vector<GMANPoint> equilateralTriangle() {
  const auto radius = 1.0 / std::numbers::sqrt3;
  std::vector<GMANPoint> loop;
  for (int k = 0; k < 3; ++k) {
    const auto angle = std::numbers::pi / 2.0 + 2.0 * std::numbers::pi * k / 3.0;
    loop.push_back(toPoint(Vec{radius * std::cos(angle), 0.0, radius * std::sin(angle)}));
  }
  return facing(loop, Vec{0.0, 1.0, 0.0});
}

// A triangle case: c's regions are its midpoint sub-triangles, the three at
// its corners and the one at its centre.
void runTriangleCase(std::vector<GMANPoint> const& loop, GMANPoint const& p, std::string const& label,
                     std::uint32_t dim) {
  MeshEmitter const emitter({loop});
  const auto v = toVecs(loop);
  const auto normal = normalized(toVec(emitter.mesh->getFace(0).getPlaneNormal()));
  const auto m01 = midpoint(v[0], v[1]);
  const auto m12 = midpoint(v[1], v[2]);
  const auto m20 = midpoint(v[2], v[0]);
  std::vector<Region> const regions = {
      {{v[0], m01, m20}, normal}, {{m01, v[1], m12}, normal}, {{m20, m12, v[2]}, normal}, {{m01, m12, m20}, normal}};

  Case c;
  c.label = label;
  c.emitter = emitter.emitter();
  c.p = p;
  c.scale = length(toVec(p));
  c.regionIntegrals = integrals(c.emitter, p, regions);
  c.regionOf = [v](Vec const& point) {
    const auto e1 = v[1] - v[0];
    const auto e2 = v[2] - v[0];
    const auto w = point - v[0];
    const auto d11 = dot(e1, e1);
    const auto d12 = dot(e1, e2);
    const auto d22 = dot(e2, e2);
    const auto determinant = d11 * d22 - d12 * d12;
    const auto l1 = (d22 * dot(w, e1) - d12 * dot(w, e2)) / determinant;
    const auto l2 = (d11 * dot(w, e2) - d12 * dot(w, e1)) / determinant;
    const auto l0 = 1.0 - l1 - l2;
    if (l0 > 0.5) {
      return std::size_t{0};
    }
    if (l1 > 0.5) {
      return std::size_t{1};
    }
    if (l2 > 0.5) {
      return std::size_t{2};
    }
    return std::size_t{3};
  };
  c.acceptsEvery = true;
  c.triangleSolidAngle = vanOosteromStrackee(toVec(p), v[0], v[1], v[2]);
  runCase(c, dim);
}

void testTriangles() {
  std::vector<GMANPoint> const equilateral = equilateralTriangle();
  runTriangleCase(equilateral, GMANPoint(0.0f, 1.0f, 0.0f), "equilateral, on its axis at 1", 1u);

  std::vector<GMANPoint> const sliver = facing(
      {GMANPoint(-0.5f, 0.0f, 0.0f), GMANPoint(0.5f, 0.0f, 0.0f), GMANPoint(0.0f, 0.0f, 0.01f)}, Vec{0.0, 1.0, 0.0});
  runTriangleCase(sliver, GMANPoint(0.3f, 0.4f, 0.0033333f), "100:1 sliver, off its axis at 0.5", 2u);

  const auto edgeZ = (double)equilateral[1].getZ();
  const auto grazingHeight = 0.5 * std::tan(std::numbers::pi / 180.0);
  runTriangleCase(equilateral, toPoint(Vec{0.0, grazingHeight, edgeZ - 0.5}),
                  "equilateral, 1 degree above its plane 0.5 beyond an edge", 3u);

  runTriangleCase(equilateral, GMANPoint(0.0f, 1.0e-3f, 0.0f), "equilateral, 1e-3 above its centroid", 4u);

  const auto corner = toVec(equilateral[0]);
  const auto inward = normalized(Vec{0.0, 0.0, 0.0} - corner);
  runTriangleCase(equilateral, toPoint(corner + 1.0e-3 * inward + Vec{0.0, 1.0e-3, 0.0}),
                  "equilateral, 1e-3 above its plane 1e-3 inside a vertex", 5u);
}

// An 8.4 x 6.4 quad, p 0.3 above an interior point; c's regions are its
// quadrants.
void testLargeQuad() {
  MeshEmitter const emitter({upRectangle(-4.2f, 4.2f, -3.2f, 3.2f, 0.0f)});
  const auto normal = Vec{0.0, 1.0, 0.0};
  std::vector<Region> regions;
  for (double const x0 : {-4.2, 0.0}) {
    for (double const z0 : {-3.2, 0.0}) {
      const auto x1 = x0 + 4.2;
      const auto z1 = z0 + 3.2;
      regions.push_back({{Vec{x0, 0.0, z0}, Vec{x0, 0.0, z1}, Vec{x1, 0.0, z1}, Vec{x1, 0.0, z0}}, normal});
    }
  }
  Case c;
  c.label = "8.4 x 6.4 quad, 0.3 above an interior point";
  c.emitter = emitter.emitter();
  c.p = GMANPoint(1.0f, 0.3f, -0.7f);
  c.scale = length(toVec(c.p));
  c.regionIntegrals = integrals(c.emitter, c.p, regions);
  c.regionOf = [](Vec const& point) {
    return std::size_t{point.x < 0.0 ? 0u : 2u} + std::size_t{point.z < 0.0 ? 0u : 1u};
  };
  c.acceptsEvery = true;
  runCase(c, 6u);
}

// The face of mesh nearest point: the least sum of point's distance from the
// face's plane and from the box that bounds the face.
std::size_t nearestFace(GMANRayPolygonMesh const& mesh, Vec const& point) {
  std::size_t nearest = 0;
  double least = std::numeric_limits<double>::max();
  for (std::size_t f = 0; f < mesh.getFaceCount(); ++f) {
    GMANRayPolygon const& face = mesh.getFace(f);
    const auto loop = toVecs(face.getOuterLoop());
    auto low = loop[0];
    auto high = loop[0];
    for (Vec const& corner : loop) {
      low = Vec{std::min(low.x, corner.x), std::min(low.y, corner.y), std::min(low.z, corner.z)};
      high = Vec{std::max(high.x, corner.x), std::max(high.y, corner.y), std::max(high.z, corner.z)};
    }
    const auto outside = Vec{point.x - std::clamp(point.x, low.x, high.x), point.y - std::clamp(point.y, low.y, high.y),
                             point.z - std::clamp(point.z, low.z, high.z)};
    const auto planeDistance = std::fabs(dot(point - loop[0], normalized(toVec(face.getPlaneNormal()))));
    const auto score = planeDistance + length(outside);
    if (score < least) {
      least = score;
      nearest = f;
    }
  }
  return nearest;
}

// A mesh case whose regions are its faces.
void runFaceCase(MeshEmitter const& emitter, GMANPoint const& p, std::string const& label, std::uint32_t dim) {
  Case c;
  c.label = label;
  c.emitter = emitter.emitter();
  c.p = p;
  c.scale = length(toVec(p));
  c.regionIntegrals = integrals(c.emitter, p, faceRegions(*emitter.mesh));
  GMANRayPolygonMesh const* const mesh = emitter.mesh.get();
  c.regionOf = [mesh](Vec const& point) { return nearestFace(*mesh, point); };
  c.acceptsEvery = true;
  runCase(c, dim);
}

// emitter_test.cpp's uneven panel, faces of area 1 and 3, p 0.2 above the
// smaller face's centre, where its share of the solid angle far exceeds its
// share of the area.
void testUnevenPanel() {
  MeshEmitter const emitter({upRectangle(-2.0f, -1.0f, -1.0f, 0.0f, 0.0f), upRectangle(1.0f, 2.0f, -1.0f, 2.0f, 0.0f)});
  runFaceCase(emitter, GMANPoint(-1.5f, 0.2f, -0.5f), "uneven panel, 0.2 above the smaller face", 7u);
}

// A roof of two faces meeting at 90 degrees along the ridge x = 0, y = 1,
// each emitting outward; p lies in front of the face at x > 0 and behind the
// other, which hides none of the first from it.
void testRoof() {
  std::vector<GMANPoint> const right = facing({GMANPoint(0.0f, 1.0f, -0.5f), GMANPoint(0.0f, 1.0f, 0.5f),
                                               GMANPoint(1.0f, 0.0f, 0.5f), GMANPoint(1.0f, 0.0f, -0.5f)},
                                              Vec{1.0, 1.0, 0.0});
  std::vector<GMANPoint> const left = facing({GMANPoint(0.0f, 1.0f, -0.5f), GMANPoint(0.0f, 1.0f, 0.5f),
                                              GMANPoint(-1.0f, 0.0f, 0.5f), GMANPoint(-1.0f, 0.0f, -0.5f)},
                                             Vec{-1.0, 1.0, 0.0});
  MeshEmitter const emitter({right, left});
  runFaceCase(emitter, GMANPoint(2.0f, 0.5f, 0.1f), "roof, in front of one face and behind the other", 8u);
}

void testDegenerateMesh() {
  MeshEmitter const emitter({facing({GMANPoint(-1.0f, 0.0f, -1.0f), GMANPoint(-1.0f, 0.0f, 1.0f),
                                     GMANPoint(1.0f, 0.0f, 1.0f), GMANPoint(1.0f, 0.0f, 1.0f)},
                                    Vec{0.0, 1.0, 0.0})});
  checkDegenerate(emitter.emitter(), GMANPoint(3.0f, 0.0f, 0.0f), "repeated-vertex quad, p in its plane outside it",
                  20u);
  checkDegenerate(emitter.emitter(), GMANPoint(-1.0f, 0.0f, 0.0f), "repeated-vertex quad, p on an edge", 21u);
  checkDegenerate(emitter.emitter(), GMANPoint(1.0f, 0.0f, 1.0f), "repeated-vertex quad, p at a vertex", 22u);
  checkDegenerate(emitter.emitter(), GMANPoint(-0.5f, 0.0f, 0.3f), "repeated-vertex quad, p on the face", 23u);
  checkDegenerate(emitter.emitter(), GMANPoint(-0.5f, 0.5f, 0.3f), "repeated-vertex quad, p above the face", 24u);
}

// m.forward() applied to the object-space point q, in double.
Vec place(gman::InvertibleMatrix const& m, Vec const& q) {
  GMANMatrix4 const& f = m.forward();
  return {q.x * f[0][0] + q.y * f[1][0] + q.z * f[2][0] + f[3][0],
          q.x * f[0][1] + q.y * f[1][1] + q.z * f[2][1] + f[3][1],
          q.x * f[0][2] + q.y * f[1][2] + q.z * f[2][2] + f[3][2]};
}

// m.backward() applied to the camera-space point q, in double.
Vec unplace(gman::InvertibleMatrix const& m, Vec const& q) {
  GMANMatrix4 const& b = m.backward();
  return {q.x * b[0][0] + q.y * b[1][0] + q.z * b[2][0] + b[3][0],
          q.x * b[0][1] + q.y * b[1][1] + q.z * b[2][1] + b[3][1],
          q.x * b[0][2] + q.y * b[1][2] + q.z * b[2][2] + b[3][2]};
}

// The object-space normal n carried into camera space by m's inverse
// transpose, unnormalized: Nanson's formula's normal factor.
Vec placeNormal(gman::InvertibleMatrix const& m, Vec const& n) {
  GMANMatrix4 const& b = m.backward();
  return {b[0][0] * n.x + b[0][1] * n.y + b[0][2] * n.z, b[1][0] * n.x + b[1][1] * n.y + b[1][2] * n.z,
          b[2][0] * n.x + b[2][1] * n.y + b[2][2] * n.z};
}

// The determinant of m's linear part.
double linearDeterminant(gman::InvertibleMatrix const& m) {
  GMANMatrix4 const& f = m.forward();
  return f[0][0] * (f[1][1] * f[2][2] - f[1][2] * f[2][1]) - f[0][1] * (f[1][0] * f[2][2] - f[1][2] * f[2][0]) +
         f[0][2] * (f[1][0] * f[2][1] - f[1][1] * f[2][0]);
}

double radians(double degrees) { return degrees * std::numbers::pi / 180.0; }

// The angle of the object-space point q about its z axis, in [0, 2 pi).
double turnAngle(Vec const& q) {
  const auto angle = std::atan2(q.y, q.x);
  if (angle < 0.0) {
    return angle + 2.0 * std::numbers::pi;
  }
  return angle;
}

// A disk emitter of radius and thetamax at height 0, placed by placement;
// its emitting side faces object-space -z.
struct DiskEmitter {
  GMANLight light{GMAN_LIGHT_AREA, kRadiance, GMANPoint(), GMANVector()};
  GMANRayDisk disk;

  DiskEmitter(RtFloat radius, RtFloat thetamax, GMANMatrix4 const& placement)
      : disk(0.0f, radius, thetamax, GMANParameterList(), makeTransform(placement)) {}

  gman::Emitter emitter() const { return {&light, &disk, 1.0f}; }
  gman::InvertibleMatrix const& placement() const { return *disk.getObjectToCamera(); }
};

// An object-space rectangle in a disk's plane.
struct Rectangle {
  double xMin;
  double xMax;
  double yMin;
  double yMax;
};

// A disk case seen from the object-space point objectP: c's regions are the
// four equal turns of its sector, each an inscribed polygon placed in
// camera space. Within the switch every draw reports 1 / the solid angle of
// bounds, the sector's bounding rectangle, when the placement is rigid;
// beyond it each reports the area density at its point.
void runDiskCase(DiskEmitter const& emitter, Vec const& objectP, Rectangle const& bounds, bool rigid, Strategy strategy,
                 std::string const& label, std::uint32_t dim) {
  gman::InvertibleMatrix const& m = emitter.placement();
  const auto radius = static_cast<double>(emitter.disk.getRadius());
  const auto sweep = radians(static_cast<double>(emitter.disk.getThetaMax()));
  const auto objectNormal = Vec{0.0, 0.0, -1.0};
  const auto normal = normalized(placeNormal(m, objectNormal));
  std::vector<Region> regions;
  for (int quarter = 0; quarter < 4; ++quarter) {
    Region region{{place(m, Vec{0.0, 0.0, 0.0})}, normal};
    for (int j = 0; j <= kArcSegments; ++j) {
      const auto angle = sweep * (quarter + static_cast<double>(j) / kArcSegments) / 4.0;
      region.loop.push_back(place(m, Vec{radius * std::cos(angle), radius * std::sin(angle), 0.0}));
    }
    regions.push_back(region);
  }

  Case c;
  c.label = label;
  c.emitter = emitter.emitter();
  c.p = toPoint(place(m, objectP));
  c.scale = length(toVec(c.p)) + length(objectP);
  c.regionIntegrals = integrals(c.emitter, c.p, regions);
  c.regionOf = [&m, sweep](Vec const& point) {
    const auto quarter = static_cast<std::size_t>(4.0 * turnAngle(unplace(m, point)) / sweep);
    return std::min(quarter, std::size_t{3});
  };
  c.strategy = strategy;
  c.acceptsEvery = strategy == Strategy::area;
  if (strategy == Strategy::solidAngle && rigid) {
    const auto p = toVec(c.p);
    const auto c0 = place(m, Vec{bounds.xMin, bounds.yMin, 0.0});
    const auto c1 = place(m, Vec{bounds.xMax, bounds.yMin, 0.0});
    const auto c2 = place(m, Vec{bounds.xMax, bounds.yMax, 0.0});
    const auto c3 = place(m, Vec{bounds.xMin, bounds.yMax, 0.0});
    c.solidAngle = vanOosteromStrackee(p, c0, c1, c2) + vanOosteromStrackee(p, c0, c2, c3);
  }
  const auto area = radius * radius * sweep / 2.0;
  const auto jacobian = std::fabs(linearDeterminant(m)) * length(placeNormal(m, objectNormal));
  c.areaDensity = [area, jacobian, normal](gman::EmitterSample const& s) {
    const auto cosine = -dot(normal, toVec(s.wi));
    return (double)s.distance * (double)s.distance / (area * jacobian * cosine);
  };
  runCase(c, dim);
}

// A disk case pair at 0.9 and 1.1 times its switch distance from bounds, the
// sector's bounding rectangle, once above its centre and once 80 degrees off
// its normal beside its x = xMax edge: near draws by solid angle, far by area.
std::uint32_t runSwitchCases(DiskEmitter const& emitter, Rectangle const& bounds, std::string const& name,
                             std::uint32_t dim) {
  const auto switchDistance = kDiskSwitch * static_cast<double>(emitter.disk.getRadius()) *
                              std::sqrt(static_cast<double>(emitter.disk.getThetaMax()) / 360.0);
  const auto offNormal = radians(80.0);
  for (double const factor : {0.9, 1.1}) {
    const auto rho = factor * switchDistance;
    const auto strategy = (factor < 1.0) ? Strategy::solidAngle : Strategy::area;
    const auto side = name + " at " + std::to_string(factor).substr(0, 3) + " times its switch distance";
    const auto middleY = (bounds.yMin + bounds.yMax) / 2.0;
    runDiskCase(emitter, {(bounds.xMin + bounds.xMax) / 2.0, middleY, -rho}, bounds, true, strategy, side + ", frontal",
                dim++);
    runDiskCase(emitter, {bounds.xMax + rho * std::sin(offNormal), middleY, -rho * std::cos(offNormal)}, bounds, true,
                strategy, side + ", 80 degrees off its normal", dim++);
  }
  return dim;
}

void testDisks() {
  GMANMatrix4 rigid;
  rigid.rot(GMANRadians(30.0f), 1.0f, 0.0f, 0.0f);
  rigid.trans(0.3f, -0.2f, 1.5f);
  GMANMatrix4 skewed;
  skewed.rot(GMANRadians(45.0f), 0.0f, 1.0f, 0.0f);
  skewed.scale(2.0f, 1.0f, 1.0f);

  DiskEmitter const unit(1.0f, 360.0f, rigid);
  Rectangle const unitBounds{-1.0, 1.0, -1.0, 1.0};
  runDiskCase(unit, {0.0, 0.0, -0.5}, unitBounds, true, Strategy::solidAngle, "unit disk, on its axis at 0.5", 30u);

  DiskEmitter const quarter(1.0f, 90.0f, rigid);
  Rectangle const quarterBounds{0.0, 1.0, 0.0, 1.0};
  const auto quarterCentroid = 4.0 * std::sin(radians(45.0)) / (3.0 * radians(90.0));
  runDiskCase(quarter, {quarterCentroid * std::cos(radians(45.0)), quarterCentroid * std::sin(radians(45.0)), -0.2},
              quarterBounds, true, Strategy::solidAngle, "90-degree sector, 0.2 above its centroid", 31u);

  DiskEmitter const threeQuarter(1.0f, 270.0f, rigid);
  const auto threeQuarterCentroid = 4.0 * std::sin(radians(135.0)) / (3.0 * radians(270.0));
  runDiskCase(threeQuarter,
              {threeQuarterCentroid * std::cos(radians(135.0)), threeQuarterCentroid * std::sin(radians(135.0)), -0.2},
              unitBounds, true, Strategy::solidAngle, "270-degree sector, 0.2 above its centroid", 32u);

  DiskEmitter const scaled(1.0f, 360.0f, skewed);
  runDiskCase(scaled, {0.2, 0.1, -0.5}, unitBounds, false, Strategy::solidAngle,
              "rotated, non-uniformly scaled disk, within its switch", 33u);

  GMANMatrix4 turned;
  turned.rot(GMANRadians(30.0f), 1.0f, 0.0f, 0.0f);
  DiskEmitter const large(4.0f, 360.0f, turned);
  runDiskCase(large, {0.0, 0.0, -1.0e-3}, Rectangle{-4.0, 4.0, -4.0, 4.0}, true, Strategy::solidAngle,
              "radius-4 disk, 1e-3 above its centre", 34u);

  const auto dim = runSwitchCases(unit, unitBounds, "unit disk", 35u);
  runSwitchCases(quarter, quarterBounds, "90-degree sector", dim);
}

void testDegenerateDisk() {
  DiskEmitter const emitter(1.0f, 360.0f, GMANMatrix4());
  checkDegenerate(emitter.emitter(), GMANPoint(0.3f, 0.2f, 0.0f), "unit disk, p in its plane", 25u);
  checkDegenerate(emitter.emitter(), GMANPoint(0.0f, 0.0f, -1.0e-6f), "unit disk, p 1e-6 above its centre", 26u);
}

// A sphere emitter of radius and z and theta bounds about the origin, placed
// by placement.
struct SphereEmitter {
  GMANLight light{GMAN_LIGHT_AREA, kRadiance, GMANPoint(), GMANVector()};
  GMANRaySphere sphere;

  SphereEmitter(RtFloat radius, RtFloat zmin, RtFloat zmax, RtFloat thetamax, GMANMatrix4 const& placement)
      : sphere(radius, zmin, zmax, thetamax, GMANParameterList(), makeTransform(placement)) {}

  gman::Emitter emitter() const { return {&light, &sphere, 1.0f}; }
  gman::InvertibleMatrix const& placement() const { return *sphere.getObjectToCamera(); }
};

// 1 - cos theta_max and the solid angle of the cone of directions from a
// point distance from a sphere's centre that meet it.
std::pair<double, double> coneOf(double radius, double distance) {
  const auto sinSquared = (radius / distance) * (radius / distance);
  const auto oneMinusCos = sinSquared / (1.0 + std::sqrt((1.0 - radius / distance) * (1.0 + radius / distance)));
  return {oneMinusCos, 2.0 * std::numbers::pi * oneMinusCos};
}

// The integral of lightSolidAnglePdf over the directions from p that meet a
// rigidly placed full sphere: the cone about the direction to its centre,
// swept by 1 - cos theta and phi, a direction's value read at the hit its ray
// meets.
double coneIntegral(SphereEmitter const& emitter, GMANPoint const& p) {
  gman::InvertibleMatrix const& m = emitter.placement();
  const auto origin = toVec(p);
  const auto toCentre = place(m, Vec{0.0, 0.0, 0.0}) - origin;
  const auto axis = normalized(toCentre);
  const auto e1 = perpendicular(axis);
  const auto e2 = cross(axis, e1);
  const auto [oneMinusCos, solidAngle] = coneOf(static_cast<double>(emitter.sphere.getRadius()), length(toCentre));
  const auto rule = gaussLegendre(kCosineNodes);
  double total = 0.0;
  for (std::size_t i = 0; i < rule.nodes.size(); ++i) {
    const auto below = oneMinusCos * rule.nodes[i];
    const auto sinTheta = std::sqrt(below * (2.0 - below));
    for (int panel = 0; panel < kTurnPanels; ++panel) {
      for (std::size_t k = 0; k < rule.nodes.size(); ++k) {
        const auto phi = 2.0 * std::numbers::pi * (panel + rule.nodes[k]) / kTurnPanels;
        const auto direction = (1.0 - below) * axis + (sinTheta * std::cos(phi)) * e1 + (sinTheta * std::sin(phi)) * e2;
        GMANHit hit;
        if (!emitter.sphere.intersect(GMANRay(p, toVector(direction)), hit)) {
          continue;
        }
        const auto pdf = gman::lightSolidAnglePdf(emitter.emitter(), p, hit.point, toNormal(toVec(hit.normal)));
        total += rule.weights[i] * rule.weights[k] / kTurnPanels * solidAngle * pdf;
      }
    }
  }
  return total;
}

// The integral of lightSolidAnglePdf over the directions from p that meet the
// sphere's zone between zLow and zHigh and turns turnLow to turnHigh, in
// object space: over its area r dz dphi, by cos / d^2 and Nanson's area
// factor |det L| |L^-T n| into the camera-space solid angle, the part facing
// away from p contributing nothing.
double zoneIntegral(SphereEmitter const& emitter, GMANPoint const& p, double zLow, double zHigh, double turnLow,
                    double turnHigh) {
  gman::InvertibleMatrix const& m = emitter.placement();
  const auto radius = static_cast<double>(emitter.sphere.getRadius());
  const auto origin = toVec(p);
  const auto determinant = std::fabs(linearDeterminant(m));
  const auto rule = gaussLegendre(kCosineNodes);
  std::vector<double> zBreaks = {zLow, zHigh};
  const auto objectP = unplace(m, origin);
  const auto horizon = radius * radius / objectP.z;
  if (objectP.x == 0.0 && objectP.y == 0.0 && horizon > zLow && horizon < zHigh) {
    zBreaks.insert(zBreaks.begin() + 1, horizon);
  }
  double total = 0.0;
  for (std::size_t piece = 0; piece + 1 < zBreaks.size(); ++piece) {
    for (int zPanel = 0; zPanel < kZonePanels; ++zPanel) {
      const auto zWidth = (zBreaks[piece + 1] - zBreaks[piece]) / kZonePanels;
      for (std::size_t i = 0; i < rule.nodes.size(); ++i) {
        const auto z = zBreaks[piece] + zWidth * (zPanel + rule.nodes[i]);
        const auto ring = std::sqrt((radius - z) * (radius + z));
        for (int turnPanel = 0; turnPanel < kTurnPanels; ++turnPanel) {
          const auto turnWidth = (turnHigh - turnLow) / kTurnPanels;
          for (std::size_t k = 0; k < rule.nodes.size(); ++k) {
            const auto phi = turnLow + turnWidth * (turnPanel + rule.nodes[k]);
            const auto objectPoint = Vec{ring * std::cos(phi), ring * std::sin(phi), z};
            const auto placedNormal = placeNormal(m, (1.0 / radius) * objectPoint);
            const auto normal = normalized(placedNormal);
            const auto point = place(m, objectPoint);
            const auto toP = origin - point;
            const auto distance = length(toP);
            const auto cosine = dot(normal, toP) / distance;
            if (!(cosine > 0.0)) {
              continue;
            }
            const auto pdf = gman::lightSolidAnglePdf(emitter.emitter(), p, toPoint(point), toNormal(normal));
            total += rule.weights[i] * zWidth * rule.weights[k] * turnWidth * radius * determinant *
                     length(placedNormal) * cosine / (distance * distance) * pdf;
          }
        }
      }
    }
  }
  return total;
}

// A sphere case seen from the object-space point objectP. A full sphere's
// one region is the whole sphere, integrated over the cone from p when its
// placement is rigid and over its area otherwise; a zone's regions are its
// four equal turns. Within the switch every draw reports 1 / the cone's solid
// angle under a rigid placement; beyond it each reports the area density at
// its point.
void runSphereCase(SphereEmitter const& emitter, Vec const& objectP, bool rigid, Strategy strategy,
                   std::string const& label, std::uint32_t dim) {
  gman::InvertibleMatrix const& m = emitter.placement();
  auto const& sphere = emitter.sphere;
  const auto radius = static_cast<double>(sphere.getRadius());
  const auto full = sphere.getZMin() <= -sphere.getRadius() && sphere.getZMax() >= sphere.getRadius() &&
                    sphere.getThetaMax() >= 360.0f;
  const auto sweep = radians(static_cast<double>(sphere.getThetaMax()));
  const auto zLow = static_cast<double>(sphere.getZMin());
  const auto zHigh = static_cast<double>(sphere.getZMax());

  Case c;
  c.label = label;
  c.emitter = emitter.emitter();
  c.p = toPoint(place(m, objectP));
  c.scale = length(toVec(c.p)) + length(objectP);
  if (full && rigid) {
    c.regionIntegrals = {coneIntegral(emitter, c.p)};
  } else if (full) {
    c.regionIntegrals = {zoneIntegral(emitter, c.p, zLow, zHigh, 0.0, sweep)};
  } else {
    for (int quarter = 0; quarter < 4; ++quarter) {
      c.regionIntegrals.push_back(
          zoneIntegral(emitter, c.p, zLow, zHigh, sweep * quarter / 4.0, sweep * (quarter + 1) / 4.0));
    }
  }
  c.regionOf = [&m, sweep, full](Vec const& point) {
    if (full) {
      return std::size_t{0};
    }
    const auto quarter = static_cast<std::size_t>(4.0 * turnAngle(unplace(m, point)) / sweep);
    return std::min(quarter, std::size_t{3});
  };
  c.acceptsEvery = full;
  c.strategy = strategy;
  c.curvature = 1.0 / radius;
  c.normalAt = [&m](Vec const& point) { return normalized(placeNormal(m, normalized(unplace(m, point)))); };
  if (strategy == Strategy::solidAngle && rigid) {
    c.solidAngle = coneOf(radius, length(objectP)).second;
  }
  const auto area = radius * (zHigh - zLow) * sweep;
  const auto determinant = std::fabs(linearDeterminant(m));
  c.areaDensity = [&m, area, determinant, origin = toVec(c.p)](gman::EmitterSample const& s) {
    const auto drawn = origin + (double)s.distance * toVec(s.wi);
    const auto placedNormal = placeNormal(m, normalized(unplace(m, drawn)));
    const auto cosine = -dot(normalized(placedNormal), toVec(s.wi));
    return (double)s.distance * (double)s.distance / (area * determinant * length(placedNormal) * cosine);
  };
  runCase(c, dim);
}

void testSpheres() {
  GMANMatrix4 rigid;
  rigid.rot(GMANRadians(30.0f), 1.0f, 0.0f, 0.0f);
  rigid.trans(0.3f, -0.2f, 1.5f);
  GMANMatrix4 stretched;
  stretched.scale(2.0f, 1.0f, 1.0f);

  SphereEmitter const two(2.0f, -2.0f, 2.0f, 360.0f, rigid);
  runSphereCase(two, {0.0, 6.0, -8.0}, true, Strategy::solidAngle, "radius-2 sphere seen from 10", 50u);

  SphereEmitter const scaled(2.0f, -2.0f, 2.0f, 360.0f, stretched);
  runSphereCase(scaled, {0.0, 0.0, -10.0}, false, Strategy::unchecked, "radius-2 sphere scaled by (2, 1, 1)", 51u);

  SphereEmitter const unit(1.0f, -1.0f, 1.0f, 360.0f, rigid);
  runSphereCase(unit, {0.0, 0.6006, -0.8008}, true, Strategy::solidAngle, "unit sphere, p at 1.001 r from its centre",
                52u);
  runSphereCase(unit, {0.0, 0.0, -1000.0}, true, Strategy::solidAngle, "unit sphere, r / d = 1e-3", 53u);

  SphereEmitter const zone(1.0f, -0.5f, 0.8f, 270.0f, rigid);
  const auto toward = normalized(Vec{1.0, 1.0, 0.2});
  runSphereCase(zone, 1.5 * toward, true, Strategy::solidAngle, "zone, within its switch", 54u);
  const auto switchDistance = kZoneSwitch * static_cast<double>(zone.sphere.getRadius());
  runSphereCase(zone, (1.0 + 0.9 * switchDistance) * toward, true, Strategy::solidAngle,
                "zone at 0.9 times its switch distance", 55u);
  runSphereCase(zone, (1.0 + 1.1 * switchDistance) * toward, true, Strategy::area,
                "zone at 1.1 times its switch distance", 56u);
}

void testDegenerateSphere() {
  SphereEmitter const emitter(1.0f, -1.0f, 1.0f, 360.0f, GMANMatrix4());
  checkDegenerate(emitter.emitter(), GMANPoint(0.6f, 0.0f, -0.8f), "unit sphere, p on its surface", 27u);
  checkDegenerate(emitter.emitter(), GMANPoint(0.0f, 0.0f, 0.0f), "unit sphere, p at its centre", 28u);
}

} // namespace

int main() {
  testTriangles();
  testLargeQuad();
  testUnevenPanel();
  testRoof();
  testDegenerateMesh();
  testDisks();
  testDegenerateDisk();
  testSpheres();
  testDegenerateSphere();

  return checkSummary("gman::sample draws an area emitter uniformly in the solid angle lightSolidAnglePdf reports");
}
