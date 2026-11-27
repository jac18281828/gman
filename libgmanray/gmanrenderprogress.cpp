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

#include <chrono>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "gmanlog.h"
#include "gmanoptions.h"
#include "gmanrenderprogress.h"
#include "ri.h"

namespace gman {

namespace {

constexpr std::int64_t kSecondsPerMinute = 60;
constexpr std::int64_t kSecondsPerHour = 3600;
constexpr std::int64_t kPercentWhole = 100;

RtInt percentDone(RtInt done, RtInt total) {
  if (total <= 0) {
    return static_cast<RtInt>(kPercentWhole);
  }
  return static_cast<RtInt>(static_cast<std::int64_t>(done) * kPercentWhole / total);
}

} // namespace

bool progressRequested(GMANOptions const& options) {
  GMANOptions::NamedOptionValue const* const named = options.getNamedOption("statistics", "progress");
  if (named == nullptr) {
    return false;
  }
  auto const* const integers = std::get_if<std::vector<RtInt>>(named);
  return integers != nullptr && !integers->empty() && integers->front() != 0;
}

std::string formatDuration(std::chrono::seconds duration) {
  const std::int64_t whole = duration.count();
  if (whole >= kSecondsPerHour) {
    return std::format("{}h{:02}m", whole / kSecondsPerHour, whole % kSecondsPerHour / kSecondsPerMinute);
  }
  if (whole >= kSecondsPerMinute) {
    return std::format("{}m{:02}s", whole / kSecondsPerMinute, whole % kSecondsPerMinute);
  }
  return std::format("{}s", whole);
}

std::string progressLine(std::string_view name, RtInt done, RtInt total, std::chrono::seconds elapsed) {
  const auto head = std::format("{}: {}% ({}/{} rows), {} elapsed", name, percentDone(done, total), done, total,
                                formatDuration(elapsed));
  if (done <= 0 || done >= total) {
    return head;
  }
  const std::chrono::seconds left{elapsed.count() * (total - done) / done};
  return std::format("{}, ~{} left", head, formatDuration(left));
}

RowProgress::RowProgress(std::string name, RtInt total)
    : name(std::move(name)), total(total), start(std::chrono::steady_clock::now()) {}

void RowProgress::operator()(RtInt done) {
  const auto percent = percentDone(done, total);
  if (percent <= lastPercent) {
    return;
  }
  lastPercent = percent;
  const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - start);
  info("{}", progressLine(name, done, total, elapsed));
}

} // namespace gman
