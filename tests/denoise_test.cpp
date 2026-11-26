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

/*
 * gman::atrousDenoise on synthetic images. A 64 by 48 scene of seeded noise
 * over a flat region, a geometric edge only the normals mark, an albedo edge
 * and a shadow edge only the colour marks: filtered by the guides alone and
 * again at the default colour sigma, the filter removes the noise and keeps
 * each edge, and removing any one weight, holding the colour sigma at its
 * first level, reading the paper's shader's normal weight or defaulting the
 * colour sigma to the largest norm breaks a named check. An impulse maps the
 * kernel: its footprint, its holes and its reach after one, two and three
 * levels. A constant image is a fixed point, a non-finite pixel is excluded
 * without disturbing a neighbour, a black image returns unchanged, a level
 * past the image's size changes nothing, the worker count changes no bit,
 * and a sigma of 1e-30 neither makes a NaN nor leaves the input's range. A
 * bad argument throws a GMANError.
 */

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "check.h"
#include "gmancolor.h"
#include "gmandenoise.h"
#include "gmanerror.h"
#include "gmannormal.h"
#include "ri.h"

namespace {

constexpr int kSceneWidth = 64;
constexpr int kSceneHeight = 48;
constexpr int kGeometricEdgeX = 32;
constexpr int kAlbedoEdgeX = 16;
constexpr int kShadowEdgeY = 24;
constexpr RtFloat kNoiseAmplitude = 0.2f;
constexpr RtFloat kLeftAlbedo = 0.8f;
constexpr RtFloat kRightAlbedo = 0.4f;
constexpr RtFloat kLitIrradiance = 0.75f;
constexpr RtFloat kShadowedIrradiance = 0.25f;
constexpr std::uint32_t kNoiseSeed = 1;
constexpr double kNoiseRange = 4294967296.0;

constexpr double kNoiseFallsLimit = 0.10;
constexpr double kGeometricEdgeLimit = 0.02;
constexpr double kAlbedoEdgeLimit = 0.03;
constexpr double kLitLevelLimit = 0.03;
constexpr double kShadowEdgeLimit = 0.065;
constexpr RtFloat kGuidesOnlySigma = 1.0e6f;
constexpr double kSceneDefaultSigma = 0.631196;
constexpr double kSceneDefaultSigmaTolerance = 1.0e-6;

constexpr RtInt kFourWorkers = 4;
constexpr RtInt kOneWorker = 1;
constexpr int kUniformSize = 16;
constexpr RtFloat kUniformLevel = 0.5f;
constexpr int kImpulseSize = 64;
constexpr int kImpulseCentre = 32;
constexpr RtFloat kImpulseSigma = 1.0e6f;
constexpr RtFloat kTinySigma = 1.0e-30f;
constexpr double kKernelTolerance = 1.0e-6;
constexpr double kFixedPointTolerance = 1.0e-6;
constexpr int kBeyondTheImage = 40;
constexpr int kLastDistinctLevels = 6;
constexpr int kFixedPointIterations = 5;

struct Scene {
  gman::GuidedImage image;
  std::vector<GMANColor> truth;
};

std::size_t indexOf(int width, int x, int y) {
  return static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + x;
}

double channelOf(GMANColor const& c, int i) {
  if (i == 0) {
    return c.getRed();
  }
  if (i == 1) {
    return c.getGreen();
  }
  return c.getBlue();
}

GMANColor grey(RtFloat v) { return GMANColor(v, v, v); }

Scene makeScene() {
  Scene scene;
  gman::GuidedImage& image = scene.image;
  image.width = kSceneWidth;
  image.height = kSceneHeight;
  std::size_t const pixels = static_cast<std::size_t>(kSceneWidth) * static_cast<std::size_t>(kSceneHeight);
  image.color.resize(pixels);
  image.albedo.resize(pixels);
  image.normal.resize(pixels);
  scene.truth.resize(pixels);

  std::mt19937 gen(kNoiseSeed);
  for (int y = 0; y < kSceneHeight; ++y) {
    for (int x = 0; x < kSceneWidth; ++x) {
      std::size_t const i = indexOf(kSceneWidth, x, y);
      image.normal[i] = GMANNormal(0.0f, 0.0f, -1.0f);
      if (x >= kGeometricEdgeX) {
        image.normal[i] = GMANNormal(1.0f, 0.0f, 0.0f);
      }
      RtFloat const albedo = x < kAlbedoEdgeX ? kLeftAlbedo : kRightAlbedo;
      image.albedo[i] = grey(albedo);
      RtFloat irradiance = 1.0f;
      if (x >= kGeometricEdgeX) {
        irradiance = y < kShadowEdgeY ? kLitIrradiance : kShadowedIrradiance;
      }
      double const truth = static_cast<double>(albedo) * static_cast<double>(irradiance);
      scene.truth[i] = grey(static_cast<RtFloat>(truth));
      std::array<RtFloat, 3> noisy{};
      for (RtFloat& channel : noisy) {
        double const noise = kNoiseAmplitude * (2.0 * (gen() / kNoiseRange) - 1.0);
        channel = static_cast<RtFloat>(truth + noise);
      }
      image.color[i] = GMANColor(noisy[0], noisy[1], noisy[2]);
    }
  }
  return scene;
}

gman::GuidedImage uniformImage(RtFloat level) {
  gman::GuidedImage image;
  image.width = kUniformSize;
  image.height = kUniformSize;
  std::size_t const pixels = static_cast<std::size_t>(kUniformSize) * static_cast<std::size_t>(kUniformSize);
  image.color.assign(pixels, grey(level));
  image.albedo.assign(pixels, grey(kUniformLevel));
  image.normal.assign(pixels, GMANNormal(0.0f, 0.0f, -1.0f));
  return image;
}

gman::GuidedImage impulseImage() {
  gman::GuidedImage image;
  image.width = kImpulseSize;
  image.height = kImpulseSize;
  std::size_t const pixels = static_cast<std::size_t>(kImpulseSize) * static_cast<std::size_t>(kImpulseSize);
  image.color.assign(pixels, grey(0.0f));
  image.albedo.assign(pixels, grey(kUniformLevel));
  image.normal.assign(pixels, GMANNormal(0.0f, 0.0f, -1.0f));
  image.color[indexOf(kImpulseSize, kImpulseCentre, kImpulseCentre)] = grey(1.0f);
  return image;
}

std::vector<GMANColor> impulseResponse(int iterations) {
  gman::AtrousParameters parameters;
  parameters.iterations = iterations;
  parameters.sigmaColor = kImpulseSigma;
  return gman::atrousDenoise(impulseImage(), parameters, kOneWorker);
}

double impulseAt(std::vector<GMANColor> const& response, int dx, int dy) {
  return response[indexOf(kImpulseSize, kImpulseCentre + dx, kImpulseCentre + dy)].getRed();
}

gman::AtrousParameters defaultParameters() { return gman::AtrousParameters(); }

double rms(std::vector<GMANColor> const& image, std::vector<GMANColor> const& truth, int x0, int x1, int y0, int y1) {
  double sum = 0.0;
  std::size_t count = 0;
  for (int y = y0; y < y1; ++y) {
    for (int x = x0; x < x1; ++x) {
      std::size_t const i = indexOf(kSceneWidth, x, y);
      for (int c = 0; c < 3; ++c) {
        double const difference = channelOf(image[i], c) - channelOf(truth[i], c);
        sum += difference * difference;
        ++count;
      }
    }
  }
  return std::sqrt(sum / static_cast<double>(count));
}

double meanError(std::vector<GMANColor> const& image, std::vector<GMANColor> const& truth, int x0, int x1, int y0,
                 int y1) {
  double sum = 0.0;
  std::size_t count = 0;
  for (int y = y0; y < y1; ++y) {
    for (int x = x0; x < x1; ++x) {
      std::size_t const i = indexOf(kSceneWidth, x, y);
      for (int c = 0; c < 3; ++c) {
        sum += channelOf(image[i], c) - channelOf(truth[i], c);
        ++count;
      }
    }
  }
  return sum / static_cast<double>(count);
}

bool bitwiseEqual(std::vector<GMANColor> const& a, std::vector<GMANColor> const& b) {
  if (a.size() != b.size()) {
    return false;
  }
  for (std::size_t i = 0; i < a.size(); ++i) {
    for (int c = 0; c < 3; ++c) {
      if (channelOf(a[i], c) != channelOf(b[i], c)) {
        return false;
      }
    }
  }
  return true;
}

double largestDifference(std::vector<GMANColor> const& a, std::vector<GMANColor> const& b) {
  double largest = 0.0;
  for (std::size_t i = 0; i < a.size(); ++i) {
    for (int c = 0; c < 3; ++c) {
      double const difference = std::fabs(channelOf(a[i], c) - channelOf(b[i], c));
      if (!std::isfinite(difference)) {
        return std::numeric_limits<double>::infinity();
      }
      largest = std::max(largest, difference);
    }
  }
  return largest;
}

bool allFinite(std::vector<GMANColor> const& image) {
  for (GMANColor const& pixel : image) {
    for (int c = 0; c < 3; ++c) {
      if (!std::isfinite(channelOf(pixel, c))) {
        return false;
      }
    }
  }
  return true;
}

bool withinInputRange(std::vector<GMANColor> const& output, std::vector<GMANColor> const& input) {
  for (int c = 0; c < 3; ++c) {
    double low = std::numeric_limits<double>::infinity();
    double high = -std::numeric_limits<double>::infinity();
    for (GMANColor const& pixel : input) {
      low = std::min(low, channelOf(pixel, c));
      high = std::max(high, channelOf(pixel, c));
    }
    for (GMANColor const& pixel : output) {
      if (channelOf(pixel, c) < low || channelOf(pixel, c) > high) {
        return false;
      }
    }
  }
  return true;
}

double noiseRatio(Scene const& scene, std::vector<GMANColor> const& out) {
  return rms(out, scene.truth, 4, 12, 4, 44) / rms(scene.image.color, scene.truth, 4, 12, 4, 44);
}

void checkGuidesOnly(Scene const& scene) {
  gman::AtrousParameters parameters;
  parameters.sigmaColor = kGuidesOnlySigma;
  std::vector<GMANColor> const out = gman::atrousDenoise(scene.image, parameters, kFourWorkers);

  double const noise = noiseRatio(scene, out);
  double const geometricBias = std::fabs(meanError(out, scene.truth, kGeometricEdgeX - 1, kGeometricEdgeX, 4, 44));
  double albedoBias = 0.0;
  for (int x : {kAlbedoEdgeX - 1, kAlbedoEdgeX}) {
    albedoBias = std::max(albedoBias, std::fabs(meanError(out, scene.truth, x, x + 1, 4, 44)));
  }

  std::printf("guides alone: noise ratio %.4f, geometric-edge bias %.4f, albedo-edge bias %.4f\n", noise, geometricBias,
              albedoBias);
  check(noise <= kNoiseFallsLimit, "the guides alone cut the flat region's noise to a tenth");
  check(geometricBias <= kGeometricEdgeLimit, "the geometric edge only the normals mark holds");
  check(albedoBias <= kAlbedoEdgeLimit, "the albedo edge holds");
}

void checkDefaults(Scene const& scene) {
  std::vector<GMANColor> const out = gman::atrousDenoise(scene.image, defaultParameters(), kFourWorkers);

  double const noise = noiseRatio(scene, out);
  double const litBias = std::fabs(meanError(out, scene.truth, 40, 60, 4, 20));
  double shadowBias = 0.0;
  for (int y : {kShadowEdgeY - 1, kShadowEdgeY}) {
    shadowBias = std::max(shadowBias, std::fabs(meanError(out, scene.truth, 40, 60, y, y + 1)));
  }
  double const sigma = gman::atrousColorSigma(scene.image, defaultParameters());

  std::printf("defaults: colour sigma %.6f, noise ratio %.4f, lit-level bias %.4f, shadow-edge bias %.4f\n", sigma,
              noise, litBias, shadowBias);
  check(std::fabs(sigma - kSceneDefaultSigma) <= kSceneDefaultSigmaTolerance,
        "the default colour sigma on the scene is the lower median of its positive norms");
  check(noise <= kNoiseFallsLimit, "the default filter cuts the flat region's noise to a tenth");
  check(litBias <= kLitLevelLimit, "the lit level beside the shadow edge holds");
  check(shadowBias <= kShadowEdgeLimit, "the shadow edge only the colour marks holds");
}

void checkSceneMeasurements() {
  Scene const scene = makeScene();
  checkGuidesOnly(scene);
  checkDefaults(scene);
}

void checkImpulse() {
  constexpr std::array<double, 5> spline = {1.0 / 16.0, 1.0 / 4.0, 3.0 / 8.0, 1.0 / 4.0, 1.0 / 16.0};
  std::vector<GMANColor> const one = impulseResponse(1);
  bool kernelMatches = true;
  bool outsideIsZero = true;
  for (int dy = -kImpulseCentre; dy < kImpulseCentre; ++dy) {
    for (int dx = -kImpulseCentre; dx < kImpulseCentre; ++dx) {
      double const value = impulseAt(one, dx, dy);
      if (std::abs(dx) <= 2 && std::abs(dy) <= 2) {
        kernelMatches = kernelMatches && std::fabs(value - spline[dx + 2] * spline[dy + 2]) <= kKernelTolerance;
      } else {
        outsideIsZero = outsideIsZero && value == 0.0;
      }
    }
  }
  check(kernelMatches, "one level spreads an impulse as the outer product of the B3 spline");
  check(outsideIsZero, "one level leaves every pixel beyond two taps exactly 0");

  std::vector<GMANColor> const two = impulseResponse(2);
  constexpr double twoLevelReach = 11.0 / 16384.0;
  check(std::fabs(impulseAt(two, 6, 0) - twoLevelReach) <= 1.0e-9, "two levels weigh the pixel six away 11/16384");
  check(impulseAt(two, 7, 0) == 0.0, "two levels stop exactly six pixels out");

  std::vector<GMANColor> const three = impulseResponse(3);
  check(impulseAt(three, 14, 0) > 0.0, "three levels reach fourteen pixels");
  check(impulseAt(three, 15, 0) == 0.0, "three levels stop exactly fourteen pixels out");
}

void checkFixedPoint() {
  gman::AtrousParameters parameters;
  parameters.iterations = kFixedPointIterations;
  std::vector<GMANColor> const out = gman::atrousDenoise(uniformImage(kUniformLevel), parameters, kFourWorkers);
  std::vector<GMANColor> const same(out.size(), grey(kUniformLevel));
  check(largestDifference(out, same) <= kFixedPointTolerance, "a constant image is a fixed point, corners included");
}

enum class Spoil { colorNaN, colorInfinity, albedoNaN, normalInfinity };

void checkNonFinite(Spoil spoil, char const* name) {
  gman::GuidedImage image = uniformImage(kUniformLevel);
  std::size_t const bad = indexOf(kUniformSize, 5, 5);
  RtFloat const nan = std::numeric_limits<RtFloat>::quiet_NaN();
  RtFloat const infinity = std::numeric_limits<RtFloat>::infinity();
  switch (spoil) {
  case Spoil::colorNaN:
    image.color[bad] = grey(nan);
    break;
  case Spoil::colorInfinity:
    image.color[bad] = grey(infinity);
    break;
  case Spoil::albedoNaN:
    image.albedo[bad] = GMANColor(nan, kUniformLevel, kUniformLevel);
    break;
  case Spoil::normalInfinity:
    image.normal[bad] = GMANNormal(0.0f, -infinity, -1.0f);
    break;
  }
  std::vector<GMANColor> const out = gman::atrousDenoise(image, defaultParameters(), kOneWorker);

  bool othersHold = true;
  for (std::size_t i = 0; i < out.size(); ++i) {
    if (i == bad) {
      continue;
    }
    for (int c = 0; c < 3; ++c) {
      othersHold = othersHold && std::fabs(channelOf(out[i], c) - kUniformLevel) <= kFixedPointTolerance;
    }
  }
  check(othersHold, std::string("a non-finite pixel disturbs no neighbour: ") + name);

  bool badKeepsInput = true;
  for (int c = 0; c < 3; ++c) {
    double const in = channelOf(image.color[bad], c);
    double const got = channelOf(out[bad], c);
    if (std::isnan(in)) {
      badKeepsInput = badKeepsInput && std::isnan(got);
    } else {
      badKeepsInput = badKeepsInput && got == in;
    }
  }
  check(badKeepsInput, std::string("a non-finite pixel reads its input colour: ") + name);
}

void checkBlack() {
  gman::GuidedImage const image = uniformImage(0.0f);
  std::vector<GMANColor> const out = gman::atrousDenoise(image, defaultParameters(), kFourWorkers);
  check(allFinite(out), "a black image filters to finite values");
  check(largestDifference(out, image.color) == 0.0, "a black image filters to black");
  check(gman::atrousColorSigma(image, defaultParameters()) == 0.0f, "a black image resolves a colour sigma of 0");
}

gman::GuidedImage rowImage(std::vector<GMANColor> colors) {
  gman::GuidedImage image;
  image.width = static_cast<int>(colors.size());
  image.height = 1;
  image.color = std::move(colors);
  image.albedo.assign(image.color.size(), grey(kUniformLevel));
  image.normal.assign(image.color.size(), GMANNormal(0.0f, 0.0f, -1.0f));
  return image;
}

GMANColor red(RtFloat v) { return GMANColor(v, 0.0f, 0.0f); }

void checkColorSigma() {
  std::vector<GMANColor> const values = {red(0.0f), red(0.0f), red(0.0f), red(1.0f), red(2.0f), red(3.0f), red(100.0f)};
  check(gman::atrousColorSigma(rowImage(values), defaultParameters()) == 2.0f,
        "the default colour sigma is the lower median of the positive norms");

  RtFloat const nan = std::numeric_limits<RtFloat>::quiet_NaN();
  RtFloat const infinity = std::numeric_limits<RtFloat>::infinity();
  std::vector<GMANColor> widened = values;
  widened.push_back(red(nan));
  widened.push_back(red(infinity));
  check(gman::atrousColorSigma(rowImage(widened), defaultParameters()) == 2.0f,
        "the default colour sigma ignores a non-finite pixel");

  gman::AtrousParameters given;
  given.sigmaColor = 0.25f;
  check(gman::atrousColorSigma(rowImage(values), given) == 0.25f, "a given colour sigma is the one used");
}

void checkBeyondTheImage() {
  Scene const scene = makeScene();
  gman::AtrousParameters six;
  six.iterations = kLastDistinctLevels;
  gman::AtrousParameters many;
  many.iterations = kBeyondTheImage;
  std::vector<GMANColor> const a = gman::atrousDenoise(scene.image, six, kFourWorkers);
  std::vector<GMANColor> const b = gman::atrousDenoise(scene.image, many, kFourWorkers);
  check(largestDifference(a, b) <= kFixedPointTolerance && allFinite(b), "levels past the image's size change nothing");
}

void checkWorkers() {
  Scene const scene = makeScene();
  std::vector<GMANColor> const one = gman::atrousDenoise(scene.image, defaultParameters(), kOneWorker);
  std::vector<GMANColor> const four = gman::atrousDenoise(scene.image, defaultParameters(), kFourWorkers);
  check(bitwiseEqual(one, four), "one worker and four workers give the same bits");
}

void checkTinySigma() {
  Scene const scene = makeScene();
  gman::AtrousParameters normal;
  normal.sigmaNormal = kTinySigma;
  gman::AtrousParameters albedo;
  albedo.sigmaAlbedo = kTinySigma;
  gman::AtrousParameters colour;
  colour.sigmaColor = kTinySigma;
  std::array<std::pair<gman::AtrousParameters, char const*>, 3> const runs = {
      std::pair{normal, "normal"}, std::pair{albedo, "albedo"}, std::pair{colour, "colour"}};
  for (auto const& [parameters, name] : runs) {
    std::vector<GMANColor> const out = gman::atrousDenoise(scene.image, parameters, kFourWorkers);
    check(allFinite(out), std::string("a sigma of 1e-30 keeps every value finite: ") + name);
    check(withinInputRange(out, scene.image.color),
          std::string("a sigma of 1e-30 keeps every pixel inside the input's range: ") + name);
  }
}

template <class F> RtInt thrownCode(F const& call) {
  try {
    call();
  } catch (GMANError const& e) {
    return e.getCode();
  } catch (...) {
    return -1;
  }
  return 0;
}

void checkGuards() {
  gman::GuidedImage const image = uniformImage(kUniformLevel);
  auto codeFor = [&](gman::GuidedImage const& img, gman::AtrousParameters const& parameters) {
    return thrownCode([&] { gman::atrousDenoise(img, parameters, kOneWorker); });
  };

  gman::AtrousParameters zeroIterations;
  zeroIterations.iterations = 0;
  check(codeFor(image, zeroIterations) == RIE_RANGE, "0 iterations throws RIE_RANGE");

  RtFloat const badSigmas[] = {0.0f, -1.0f, std::numeric_limits<RtFloat>::quiet_NaN(),
                               std::numeric_limits<RtFloat>::infinity()};
  for (RtFloat const sigma : badSigmas) {
    gman::AtrousParameters parameters;
    parameters.sigmaNormal = sigma;
    check(codeFor(image, parameters) == RIE_RANGE, "a bad normal sigma throws RIE_RANGE");
    parameters = gman::AtrousParameters();
    parameters.sigmaAlbedo = sigma;
    check(codeFor(image, parameters) == RIE_RANGE, "a bad albedo sigma throws RIE_RANGE");
  }

  gman::AtrousParameters zeroColor;
  zeroColor.sigmaColor = 0.0f;
  check(codeFor(image, zeroColor) == RIE_RANGE, "a colour sigma of 0 throws RIE_RANGE");

  gman::GuidedImage shortAlbedo = image;
  shortAlbedo.albedo.pop_back();
  check(codeFor(shortAlbedo, defaultParameters()) == RIE_CONSISTENCY, "a short albedo throws RIE_CONSISTENCY");
}

} // namespace

int main() {
  checkSceneMeasurements();
  checkImpulse();
  checkFixedPoint();
  checkNonFinite(Spoil::colorNaN, "colour NaN");
  checkNonFinite(Spoil::colorInfinity, "colour infinity");
  checkNonFinite(Spoil::albedoNaN, "albedo NaN");
  checkNonFinite(Spoil::normalInfinity, "normal infinity");
  checkBlack();
  checkColorSigma();
  checkBeyondTheImage();
  checkWorkers();
  checkTinySigma();
  checkGuards();
  return checkSummary("denoise ok");
}
