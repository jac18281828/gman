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

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <utility>
#include <vector>

#include "gmanerror.h"
#include "gmanlightsourcemgr.h"
#include "gmanmath.h"
#include "gmannormal.h"
#include "gmanradiositygrouping.h"
#include "gmanradiositymesh.h"
#include "gmanradiositysolver.h"
#include "gmanraybbox.h"
#include "gmanrayinterface.h"
#include "gmanrayoccluder.h"
#include "gmanraypolygon.h"
#include "gmanshaderenvironment.h"

namespace {

// The transmission walk stops this fraction of the segment short of the
// shooter, keeping a flat shooter's own plane out of it.
constexpr double kShooterClearance = 1e-4;

// The clearance never falls below this fraction of the segment's largest
// coordinate: the occluder's own self-shadow offset ceiling, by which its
// walk moves the origin off the receiver without shortening the walk.
constexpr double kClearanceFloor = 3e-5;

// Node grouping pairs nodes whose projections lie within this fraction
// of the scene's largest coordinate: ten times sameNode's own reach, so
// every pair it joins is a candidate.
constexpr double kNodeSearchScale = 1e-5;

std::size_t sideIndex(std::size_t element, GMANRadiositySide side) {
  return 2 * element + static_cast<std::size_t>(side);
}

// sqrt(samples), when samples is a perfect square with an even root.
std::size_t sampleRoot(std::size_t samples) {
  auto const root = static_cast<std::size_t>(std::llround(std::sqrt(static_cast<double>(samples))));
  if (samples == 0 || root * root != samples || root % 2 != 0) {
    throw GMANError(RIE_RANGE, RIE_ERROR, "radiosity: samples must be a perfect square with an even root");
  }
  return root;
}

double maxChannel(GMANColor const& c) {
  return GMANMax(GMANMax((double)c.getRed(), (double)c.getGreen()), (double)c.getBlue());
}

bool isPolygon(GMANRayInterface const* primitive) { return dynamic_cast<GMANRayPolygon const*>(primitive) != nullptr; }

// The centre, in [0, 1], of cell index of a subgrid root cells wide.
double subgridCentre(std::size_t index, std::size_t root) { return ((double)index + 0.5) / (double)root; }

// H_d on each side of a receiver: the ray tracer's own diffuse() there.
std::array<GMANColor, 2> directIrradiance(gman::RadiosityReceiver const& receiver,
                                          std::vector<GMANLight const*> const& lights,
                                          GMANRayOccluder const& occluder) {
  GMANSurfaceEnv env{};
  env.P = receiver.P;
  env.Ng = GMANNormal(receiver.N.getX(), receiver.N.getY(), receiver.N.getZ());
  env.lights = lights;
  env.occluder = &occluder;
  env.surfaceMagnitude = receiver.surfaceMagnitude;
  return {env.diffuse(receiver.N), env.diffuse(-receiver.N)};
}

// Each node's sameNode group root, the lowest node index in its group:
// candidate pairs come from a sweep over the nodes' projections, and the
// mesh's own sameNode decides each.
std::vector<std::size_t> nodeGroupRoots(GMANRadiosityMesh const& mesh, double largestMagnitude) {
  std::size_t const count = mesh.getNodeCount();
  double extent = largestMagnitude;
  for (std::size_t node = 0; node < count; ++node) {
    extent = GMANMax(extent, gman::maxAbsCoordinate(mesh.getNode(node).position));
  }
  double const window = kNodeSearchScale * extent;

  std::vector<std::pair<double, std::size_t>> order;
  order.reserve(count);
  for (std::size_t node = 0; node < count; ++node) {
    order.push_back({gman::nodeGroupSweepProjection(mesh.getNode(node).position), node});
  }
  std::sort(order.begin(), order.end());

  std::vector<std::size_t> parent(count);
  for (std::size_t node = 0; node < count; ++node) {
    parent[node] = node;
  }
  for (std::size_t i = 0; i < count; ++i) {
    for (std::size_t j = i + 1; j < count && order[j].first - order[i].first <= window; ++j) {
      if (mesh.sameNode(order[i].second, order[j].second)) {
        gman::uniteGroups(parent, order[i].second, order[j].second);
      }
    }
  }

  std::vector<std::size_t> roots(count);
  for (std::size_t node = 0; node < count; ++node) {
    roots[node] = gman::findGroupRoot(parent, node);
  }
  return roots;
}

// Per node side, the A_i-weighted mean indirect over every element
// touching the node's group, summed in element order.
std::vector<GMANColor> nodeIndirectMeans(GMANRadiosityMesh const& mesh, std::vector<std::size_t> const& roots,
                                         std::vector<RtFloat> const& areas, std::vector<GMANColor> const& indirect) {
  std::size_t const nodeCount = mesh.getNodeCount();
  std::vector<std::vector<std::size_t>> groupElements(nodeCount);
  for (std::size_t element = 0; element < mesh.getElementCount(); ++element) {
    for (std::size_t const corner : mesh.getElement(element).corners) {
      std::vector<std::size_t>& members = groupElements[roots[corner]];
      if (members.empty() || members.back() != element) {
        members.push_back(element);
      }
    }
  }

  std::vector<GMANColor> means(2 * nodeCount);
  for (std::size_t node = 0; node < nodeCount; ++node) {
    std::vector<std::size_t> const& members = groupElements[roots[node]];
    for (std::size_t side = 0; side < 2; ++side) {
      double weight = 0;
      std::array<double, 3> sum = {0, 0, 0};
      for (std::size_t const element : members) {
        GMANColor const& value = indirect[2 * element + side];
        double const area = areas[element];
        weight += area;
        sum[0] += area * value.getRed();
        sum[1] += area * value.getGreen();
        sum[2] += area * value.getBlue();
      }
      if (weight > 0) {
        means[2 * node + side] =
            GMANColor((RtFloat)(sum[0] / weight), (RtFloat)(sum[1] / weight), (RtFloat)(sum[2] / weight));
      }
    }
  }
  return means;
}

// B = rho * H_d as the starting unshot radiosity of every side, and the
// scene's total direct power, sum max-channel(rho H_d) A_i.
double initializeUnshot(std::vector<GMANColor> const& reflectance, std::vector<GMANColor> const& direct,
                        std::vector<RtFloat> const& areas, std::vector<GMANColor>& unshot) {
  double directPower = 0;
  for (std::size_t side = 0; side < unshot.size(); ++side) {
    unshot[side] = gman::multiplyChannels(reflectance[side / 2], direct[side]);
    directPower += maxChannel(unshot[side]) * areas[side / 2];
  }
  return directPower;
}

// The side of largest unshot power, max-channel(unshot) * A_i, ties broken
// by lowest side index (lowest element, then front); and the summed unshot
// power over every side.
struct RadiosityShooterPick {
  std::size_t side = 0;
  double totalUnshotPower = 0;
};

RadiosityShooterPick pickShooterSide(std::vector<GMANColor> const& unshot, std::vector<RtFloat> const& areas) {
  RadiosityShooterPick pick;
  double largest = -1;
  for (std::size_t side = 0; side < unshot.size(); ++side) {
    double const power = maxChannel(unshot[side]) * areas[side / 2];
    pick.totalUnshotPower += power;
    if (power > largest) {
      largest = power;
      pick.side = side;
    }
  }
  return pick;
}

// H_ind = H - H_d per side.
std::vector<GMANColor> computeIndirect(std::vector<GMANColor> const& irradiance, std::vector<GMANColor> const& direct) {
  std::vector<GMANColor> indirect(irradiance.size());
  for (std::size_t side = 0; side < indirect.size(); ++side) {
    indirect[side] = irradiance[side];
    indirect[side] -= direct[side];
  }
  return indirect;
}

void checkReflectance(GMANRadiosityMesh const& mesh, std::vector<GMANColor> const& reflectance) {
  if (reflectance.size() != mesh.getElementCount()) {
    throw GMANError(RIE_CONSISTENCY, RIE_ERROR, "radiosity: one reflectance per element");
  }
  for (GMANColor const& rho : reflectance) {
    for (RtFloat const channel : {rho.getRed(), rho.getGreen(), rho.getBlue()}) {
      if (!(channel >= 0 && channel <= 1)) {
        throw GMANError(RIE_RANGE, RIE_ERROR, "radiosity: reflectance channels lie in [0, 1]");
      }
    }
  }
}

} // namespace

namespace gman {

RadiosityReceiver radiosityReceiver(GMANRadiosityMesh const& mesh, std::size_t element) {
  RadiosityReceiver receiver;
  GMANRadiositySurfacePoint point;
  if (mesh.surfacePoint(element, 0.5, 0.5, point)) {
    receiver.P = point.P;
    receiver.N = point.N;
  } else {
    receiver.P = mesh.getElement(element).centre;
    receiver.N = mesh.getElement(element).normal;
  }
  GMANRayInterface const* primitive = mesh.getElementPrimitive(element);
  receiver.surfaceMagnitude = gman::primitiveMagnitude(primitive->getBBox());
  return receiver;
}

RtFloat radiosityElementArea(GMANRadiosityMesh const& mesh, std::size_t element, std::size_t samples) {
  std::size_t const root = sampleRoot(samples);
  if (isPolygon(mesh.getElementPrimitive(element))) {
    return mesh.getElement(element).area;
  }
  double sum = 0;
  for (std::size_t b = 0; b < root; ++b) {
    for (std::size_t a = 0; a < root; ++a) {
      GMANRadiositySurfacePoint point;
      if (mesh.surfacePoint(element, subgridCentre(a, root), subgridCentre(b, root), point)) {
        sum += point.density;
      }
    }
  }
  return (RtFloat)(sum / (double)samples);
}

std::vector<RadiositySample> placeRadiositySamples(GMANRadiosityMesh const& mesh, std::size_t element, RtFloat area,
                                                   std::size_t samples) {
  std::size_t const root = sampleRoot(samples);
  std::vector<RadiositySample> placed;
  std::vector<double> densities;
  double total = 0;
  for (std::size_t b = 0; b < root; ++b) {
    for (std::size_t a = 0; a < root; ++a) {
      GMANRadiositySurfacePoint point;
      if (!mesh.surfacePoint(element, subgridCentre(a, root), subgridCentre(b, root), point)) {
        continue;
      }
      placed.push_back({point.P, point.N, 0});
      densities.push_back(point.density);
      total += point.density;
    }
  }

  if (placed.empty() || total <= 0) {
    RadiosityReceiver const receiver = radiosityReceiver(mesh, element);
    return {{receiver.P, receiver.N, area}};
  }
  for (std::size_t k = 0; k < placed.size(); ++k) {
    placed[k].weight = (RtFloat)((double)area * densities[k] / total);
  }
  return placed;
}

} // namespace gman

namespace {

// One shooter sample's form-factor contribution to receiver: the arrival
// side its segment reaches, and the per-channel geometric term times
// transmission, in plain doubles. False when the segment has zero length,
// does not leave the shooter on its shooting side, or lies exactly in the
// receiver's plane.
struct RadiositySampleContribution {
  std::size_t arrival = 0;
  double red = 0, green = 0, blue = 0;
};

bool sampleFormFactorContribution(GMANRayOccluder const& occluder, gman::RadiosityReceiver const& receiver,
                                  gman::RadiositySample const& sample, double shootingSign, double receiverScale,
                                  RadiositySampleContribution& contribution) {
  double const rx = receiver.P.getX(), ry = receiver.P.getY(), rz = receiver.P.getZ();
  double const nx = receiver.N.getX(), ny = receiver.N.getY(), nz = receiver.N.getZ();
  double const sx = sample.P.getX(), sy = sample.P.getY(), sz = sample.P.getZ();
  double dx = sx - rx, dy = sy - ry, dz = sz - rz;
  double const distanceSquared = dx * dx + dy * dy + dz * dz;
  if (distanceSquared <= 0) {
    return false;
  }
  double const distance = std::sqrt(distanceSquared);
  dx /= distance;
  dy /= distance;
  dz /= distance;

  // Positive when the segment leaves the shooter on its shooting side.
  double const cosShooter =
      -shootingSign * ((double)sample.N.getX() * dx + (double)sample.N.getY() * dy + (double)sample.N.getZ() * dz);
  double const cosReceiver = nx * dx + ny * dy + nz * dz;
  if (cosShooter <= 0 || cosReceiver == 0) {
    return false;
  }
  contribution.arrival = static_cast<std::size_t>(cosReceiver > 0 ? GMANRadiositySide::front : GMANRadiositySide::back);

  double const sampleScale = std::fmax(std::fmax(std::fabs(sx), std::fabs(sy)), std::fabs(sz));
  double const clearance =
      std::fmax(kShooterClearance * distance, kClearanceFloor * std::fmax(receiverScale, sampleScale));
  GMANLight const light(GMAN_LIGHT_POINT, GMANColor(1.0f, 1.0f, 1.0f), sample.P, GMANVector());
  GMANVector const direction((RtFloat)dx, (RtFloat)dy, (RtFloat)dz);
  GMANColor const transmission =
      occluder.transmission(light, receiver.P, direction, receiver.N, (RtFloat)std::fmax(distance - clearance, 0.0),
                            receiver.surfaceMagnitude);

  double const weight = sample.weight;
  double const geometric = weight * std::fabs(cosReceiver) * cosShooter / (PI * distanceSquared + weight);
  contribution.red = geometric * transmission.getRed();
  contribution.green = geometric * transmission.getGreen();
  contribution.blue = geometric * transmission.getBlue();
  return true;
}

} // namespace

namespace gman {

std::array<GMANColor, 2> radiosityFormFactors(GMANRayOccluder const& occluder, RadiosityReceiver const& receiver,
                                              std::vector<RadiositySample> const& shooter,
                                              GMANRadiositySide shootingSide) {
  double const shootingSign = shootingSide == GMANRadiositySide::front ? 1.0 : -1.0;
  double const receiverScale = gman::maxAbsCoordinate(receiver.P);
  double sums[2][3] = {{0, 0, 0}, {0, 0, 0}};

  RadiositySampleContribution contribution;
  for (RadiositySample const& sample : shooter) {
    if (!sampleFormFactorContribution(occluder, receiver, sample, shootingSign, receiverScale, contribution)) {
      continue;
    }
    sums[contribution.arrival][0] += contribution.red;
    sums[contribution.arrival][1] += contribution.green;
    sums[contribution.arrival][2] += contribution.blue;
  }

  return {GMANColor((RtFloat)sums[0][0], (RtFloat)sums[0][1], (RtFloat)sums[0][2]),
          GMANColor((RtFloat)sums[1][0], (RtFloat)sums[1][1], (RtFloat)sums[1][2])};
}

} // namespace gman

GMANRadiositySolution GMANRadiositySolver::makeEmptySolution(std::size_t elementCount, std::size_t nodeCount) {
  GMANRadiositySolution solution;
  solution.areas.resize(elementCount);
  solution.direct.resize(2 * elementCount);
  solution.indirect.assign(2 * elementCount, GMANColor());
  solution.nodeIndirect.assign(2 * nodeCount, GMANColor());
  return solution;
}

void GMANRadiositySolver::initializeReceivers(GMANRadiosityMesh const& mesh, GMANRayOccluder const& occluder,
                                              std::size_t samples, GMANRadiositySolution& solution,
                                              std::vector<gman::RadiosityReceiver>& receivers,
                                              std::vector<bool>& shootsItself, double& largestMagnitude) {
  for (std::size_t element = 0; element < receivers.size(); ++element) {
    GMANRayInterface const* primitive = mesh.getElementPrimitive(element);
    receivers[element] = gman::radiosityReceiver(mesh, element);
    solution.areas[element] = gman::radiosityElementArea(mesh, element, samples);
    shootsItself[element] = !isPolygon(primitive);
    largestMagnitude = GMANMax(largestMagnitude, (double)receivers[element].surfaceMagnitude);

    std::array<GMANColor, 2> const direct =
        directIrradiance(receivers[element], primitive->getAppearance().lights, occluder);
    solution.direct[sideIndex(element, GMANRadiositySide::front)] = direct[0];
    solution.direct[sideIndex(element, GMANRadiositySide::back)] = direct[1];
  }
}

void GMANRadiositySolver::fireShot(GMANRadiosityMesh const& mesh, GMANRayOccluder const& occluder,
                                   std::vector<gman::RadiosityReceiver> const& receivers,
                                   std::vector<bool> const& shootsItself, std::vector<GMANColor> const& reflectance,
                                   std::size_t samples, std::size_t shooterSide, GMANColor const& shot,
                                   GMANRadiositySolution& solution, std::vector<GMANColor>& unshot) {
  std::size_t const element = shooterSide / 2;
  auto const shootingSide = static_cast<GMANRadiositySide>(shooterSide % 2);
  std::vector<gman::RadiositySample> const shooterSamples =
      gman::placeRadiositySamples(mesh, element, solution.areas[element], samples);
  for (std::size_t receiver = 0; receiver < receivers.size(); ++receiver) {
    if (receiver == element && !shootsItself[element]) {
      continue;
    }
    std::array<GMANColor, 2> const factors =
        gman::radiosityFormFactors(occluder, receivers[receiver], shooterSamples, shootingSide);
    for (std::size_t arrival = 0; arrival < 2; ++arrival) {
      GMANColor const gained = gman::multiplyChannels(shot, factors[arrival]);
      solution.irradiance[2 * receiver + arrival] += gained;
      unshot[2 * receiver + arrival] += gman::multiplyChannels(reflectance[receiver], gained);
    }
  }
}

void GMANRadiositySolver::finalizeIndirect(GMANRadiosityMesh const& mesh, double largestMagnitude,
                                           GMANRadiositySolution& solution) {
  solution.indirect = computeIndirect(solution.irradiance, solution.direct);
  solution.nodeIndirect =
      nodeIndirectMeans(mesh, nodeGroupRoots(mesh, largestMagnitude), solution.areas, solution.indirect);
}

GMANRadiositySolution GMANRadiositySolver::solve(GMANRadiosityMesh const& mesh, GMANRayOccluder const& occluder,
                                                 std::vector<GMANColor> const& reflectance, std::size_t samples) const {
  checkReflectance(mesh, reflectance);
  sampleRoot(samples);

  std::size_t const elementCount = mesh.getElementCount();
  std::size_t const sideCount = 2 * elementCount;
  GMANRadiositySolution solution = makeEmptySolution(elementCount, mesh.getNodeCount());

  std::vector<gman::RadiosityReceiver> receivers(elementCount);
  std::vector<bool> shootsItself(elementCount);
  double largestMagnitude = 0;
  initializeReceivers(mesh, occluder, samples, solution, receivers, shootsItself, largestMagnitude);

  solution.irradiance = solution.direct;
  std::vector<GMANColor> unshot(sideCount);
  double const directPower = initializeUnshot(reflectance, solution.direct, solution.areas, unshot);
  if (directPower <= 0) {
    return solution;
  }

  std::size_t const shotCap = kMaxShotsPerSide * sideCount;
  for (;;) {
    RadiosityShooterPick const pick = pickShooterSide(unshot, solution.areas);
    if (pick.totalUnshotPower <= kConvergence * directPower) {
      break;
    }
    if (solution.shotCount == shotCap) {
      solution.shotCapReached = true;
      break;
    }

    GMANColor const shot = unshot[pick.side];
    unshot[pick.side] = GMANColor();
    fireShot(mesh, occluder, receivers, shootsItself, reflectance, samples, pick.side, shot, solution, unshot);
    ++solution.shotCount;
  }

  finalizeIndirect(mesh, largestMagnitude, solution);
  return solution;
}
