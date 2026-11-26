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
#include <cstdint>
#include <format>
#include <limits>
#include <string>
#include <vector>

#include "gmancolor.h"
#include "gmandenoise.h"
#include "gmanerror.h"
#include "gmannormal.h"
#include "gmanparallel.h"
#include "ri.h"

namespace {

constexpr int kTapRadius = 2;
constexpr std::array<double, 5> kSplineWeights = {1.0 / 16.0, 1.0 / 4.0, 3.0 / 8.0, 1.0 / 4.0, 1.0 / 16.0};

using Triple = std::array<double, 3>;

struct Guides {
  int width = 0;
  int height = 0;
  std::vector<Triple> albedo;
  std::vector<Triple> normal;
  std::vector<bool> usable;
};

struct Sigmas {
  double normal;
  double albedo;
};

bool finite(GMANColor const& c) {
  return std::isfinite(c.getRed()) && std::isfinite(c.getGreen()) && std::isfinite(c.getBlue());
}

bool finite(GMANNormal const& n) {
  return std::isfinite(n.getX()) && std::isfinite(n.getY()) && std::isfinite(n.getZ());
}

bool finitePositive(RtFloat value) { return std::isfinite(value) && value > 0.0f; }

Triple triple(GMANColor const& c) {
  return {static_cast<double>(c.getRed()), static_cast<double>(c.getGreen()), static_cast<double>(c.getBlue())};
}

Triple triple(GMANNormal const& n) {
  return {static_cast<double>(n.getX()), static_cast<double>(n.getY()), static_cast<double>(n.getZ())};
}

GMANColor toColor(Triple const& t) {
  return GMANColor(static_cast<RtFloat>(t[0]), static_cast<RtFloat>(t[1]), static_cast<RtFloat>(t[2]));
}

double squaredDistance(Triple const& a, Triple const& b) {
  double sum = 0.0;
  for (std::size_t i = 0; i < a.size(); ++i) {
    double const difference = a[i] - b[i];
    sum += difference * difference;
  }
  return sum;
}

double gaussian(double distanceSquared, double sigma) { return std::exp(-distanceSquared / (sigma * sigma)); }

void requireRange(bool ok, char const* name, std::string const& requirement) {
  if (!ok) {
    throw GMANError(RIE_RANGE, RIE_ERROR, std::format("atrousDenoise: {} must be {}.", name, requirement).c_str());
  }
}

void requireSize(std::size_t size, std::size_t expected, char const* name) {
  if (size != expected) {
    throw GMANError(
        RIE_CONSISTENCY, RIE_ERROR,
        std::format("atrousDenoise: {} holds {} elements, width times height is {}.", name, size, expected).c_str());
  }
}

void validate(gman::GuidedImage const& image, gman::AtrousParameters const& parameters) {
  requireRange(parameters.iterations >= 1, "iterations", "at least 1");
  requireRange(!parameters.sigmaColor || finitePositive(*parameters.sigmaColor), "sigmaColor", "finite and positive");
  requireRange(finitePositive(parameters.sigmaNormal), "sigmaNormal", "finite and positive");
  requireRange(finitePositive(parameters.sigmaAlbedo), "sigmaAlbedo", "finite and positive");
  requireRange(image.width >= 0 && image.height >= 0, "width and height", "not negative");
  std::size_t const pixels = static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height);
  requireSize(image.color.size(), pixels, "color");
  requireSize(image.albedo.size(), pixels, "albedo");
  requireSize(image.normal.size(), pixels, "normal");
}

bool usablePixel(gman::GuidedImage const& image, std::size_t index) {
  return finite(image.color[index]) && finite(image.albedo[index]) && finite(image.normal[index]);
}

RtFloat resolvedColorSigma(gman::GuidedImage const& image, gman::AtrousParameters const& parameters) {
  if (parameters.sigmaColor) {
    return *parameters.sigmaColor;
  }
  double largest = 0.0;
  for (std::size_t i = 0; i < image.color.size(); ++i) {
    if (!usablePixel(image, i)) {
      continue;
    }
    Triple const c = triple(image.color[i]);
    largest = std::max(largest, std::sqrt(c[0] * c[0] + c[1] * c[1] + c[2] * c[2]));
  }
  return static_cast<RtFloat>(std::min(largest, static_cast<double>(std::numeric_limits<RtFloat>::max())));
}

Guides makeGuides(gman::GuidedImage const& image) {
  Guides guides;
  guides.width = image.width;
  guides.height = image.height;
  std::size_t const pixels = image.color.size();
  guides.albedo.resize(pixels);
  guides.normal.resize(pixels);
  guides.usable.resize(pixels);
  for (std::size_t i = 0; i < pixels; ++i) {
    guides.usable[i] = usablePixel(image, i);
    if (guides.usable[i]) {
      guides.albedo[i] = triple(image.albedo[i]);
      guides.normal[i] = triple(image.normal[i]);
    }
  }
  return guides;
}

Triple filteredPixel(Guides const& guides, Sigmas const& sigmas, double colorSigma, std::int64_t step,
                     std::vector<Triple> const& source, int x, int y) {
  std::size_t const centre =
      static_cast<std::size_t>(y) * static_cast<std::size_t>(guides.width) + static_cast<std::size_t>(x);
  Triple sum = {0.0, 0.0, 0.0};
  double total = 0.0;
  for (int dy = -kTapRadius; dy <= kTapRadius; ++dy) {
    std::int64_t const qy = y + step * dy;
    if (qy < 0 || qy >= guides.height) {
      continue;
    }
    for (int dx = -kTapRadius; dx <= kTapRadius; ++dx) {
      std::int64_t const qx = x + step * dx;
      if (qx < 0 || qx >= guides.width) {
        continue;
      }
      std::size_t const tap =
          static_cast<std::size_t>(qy) * static_cast<std::size_t>(guides.width) + static_cast<std::size_t>(qx);
      if (!guides.usable[tap]) {
        continue;
      }
      double const colorWeight = gaussian(squaredDistance(source[centre], source[tap]), colorSigma);
      double const normalWeight = gaussian(squaredDistance(guides.normal[centre], guides.normal[tap]), sigmas.normal);
      double const albedoWeight = gaussian(squaredDistance(guides.albedo[centre], guides.albedo[tap]), sigmas.albedo);
      double const spline = kSplineWeights[static_cast<std::size_t>(dx + kTapRadius)] *
                            kSplineWeights[static_cast<std::size_t>(dy + kTapRadius)];
      double const weight = spline * colorWeight * normalWeight * albedoWeight;
      for (std::size_t c = 0; c < sum.size(); ++c) {
        sum[c] += weight * source[tap][c];
      }
      total += weight;
    }
  }
  for (double& channel : sum) {
    channel /= total;
  }
  return sum;
}

void filterLevel(Guides const& guides, Sigmas const& sigmas, double colorSigma, std::int64_t step,
                 std::vector<Triple> const& source, std::vector<Triple>& target, RtInt workers) {
  gman::parallelFor(
      guides.height,
      [&](RtInt row, RtInt) {
        for (int x = 0; x < guides.width; ++x) {
          std::size_t const index =
              static_cast<std::size_t>(row) * static_cast<std::size_t>(guides.width) + static_cast<std::size_t>(x);
          target[index] =
              guides.usable[index] ? filteredPixel(guides, sigmas, colorSigma, step, source, x, row) : source[index];
        }
      },
      workers);
}

} // namespace

namespace gman {

RtFloat atrousColorSigma(GuidedImage const& image, AtrousParameters const& parameters) {
  validate(image, parameters);
  return resolvedColorSigma(image, parameters);
}

std::vector<GMANColor> atrousDenoise(GuidedImage const& image, AtrousParameters const& parameters, RtInt workers) {
  validate(image, parameters);
  double const baseColorSigma = static_cast<double>(resolvedColorSigma(image, parameters));
  if (baseColorSigma == 0.0) {
    return image.color;
  }

  Guides const guides = makeGuides(image);
  Sigmas const sigmas{static_cast<double>(parameters.sigmaNormal), static_cast<double>(parameters.sigmaAlbedo)};
  std::vector<Triple> current(image.color.size());
  for (std::size_t i = 0; i < current.size(); ++i) {
    current[i] = triple(image.color[i]);
  }
  std::vector<Triple> next(current.size());

  std::int64_t const extent = std::max(image.width, image.height);
  std::int64_t step = 1;
  for (int level = 0; level < parameters.iterations && step < extent; ++level, step *= 2) {
    filterLevel(guides, sigmas, baseColorSigma / static_cast<double>(step), step, current, next, workers);
    current.swap(next);
  }

  std::vector<GMANColor> result(image.color.size());
  for (std::size_t i = 0; i < result.size(); ++i) {
    result[i] = guides.usable[i] ? toColor(current[i]) : image.color[i];
  }
  return result;
}

} // namespace gman
