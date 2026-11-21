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
 * GMANParameterList's own contract: a default or moved-from list answers
 * every lookup with NULL, a present zero-length entry still answers
 * non-NULL, a copy shares its source's storage and outlives it, and a
 * construction that throws partway through frees what it had already
 * allocated. A failed allocation reports RIE_NOMEM.
 */

#include <atomic>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <optional>
#include <string>

#include "check.h"
#include "gmandictionary.h"
#include "gmanparameterlist.h"

#if defined(__SANITIZE_ADDRESS__)
#define GMAN_ADDRESS_SANITIZED 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define GMAN_ADDRESS_SANITIZED 1
#endif
#endif
#ifndef GMAN_ADDRESS_SANITIZED
#define GMAN_ADDRESS_SANITIZED 0
#endif

namespace {

// Counts live allocations across the whole process so the throw-safety
// check below can tell a leak from a false pass.
std::atomic<long> liveAllocations{0};

// A request above this many bytes fails with bad_alloc without allocating.
std::atomic<std::size_t> failAllocationsAbove{static_cast<std::size_t>(-1)};

} // namespace

void* operator new(std::size_t n) {
  if (n > failAllocationsAbove.load()) {
    throw std::bad_alloc();
  }
  void* p = std::malloc(n != 0 ? n : 1);
  if (p == nullptr) {
    throw std::bad_alloc();
  }
  ++liveAllocations;
  return p;
}

void operator delete(void* p) noexcept {
  if (p != nullptr) {
    --liveAllocations;
  }
  std::free(p);
}

// GCC's -Wsized-deallocation requires the sized form once the unsized one
// is replaced; both just forward, since this allocator tracks calls, not
// sizes.
void operator delete(void* p, std::size_t) noexcept { ::operator delete(p); }

void* operator new[](std::size_t n) { return ::operator new(n); }

void operator delete[](void* p) noexcept { ::operator delete(p); }

void operator delete[](void* p, std::size_t) noexcept { ::operator delete(p); }

namespace {

void checkDefaultAndMoved() {
  GMANDictionary dictionary;
  const auto kaId = dictionary.getTokenId(std::string(RI_KA));

  GMANParameterList empty;
  check(empty.getPointer(kaId) == nullptr, "a default list answers every lookup with NULL");

  RtFloat ka = 1.0f;
  RtToken tokens[1] = {RI_KA};
  RtPointer parms[1] = {&ka};

  GMANParameterList movedFromByCtor(dictionary, 1, tokens, parms);
  GMANParameterList movedInto(std::move(movedFromByCtor));
  check(movedFromByCtor.getPointer(kaId) == nullptr, "a list moved-from by construction answers NULL");
  check(movedInto.getPointer(kaId) != nullptr, "the list moved into holds the data");

  GMANParameterList movedFromByAssign(dictionary, 1, tokens, parms);
  GMANParameterList assignTarget;
  assignTarget = std::move(movedFromByAssign);
  check(movedFromByAssign.getPointer(kaId) == nullptr, "a list moved-from by assignment answers NULL");
  check(assignTarget.getPointer(kaId) != nullptr, "the list assigned into holds the data");
}

void checkZeroLengthEntry() {
  GMANDictionary dictionary;
  const auto widthId = dictionary.getTokenId(std::string(RI_WIDTH));

  RtToken tokens[1] = {RI_WIDTH};
  RtPointer parms[1] = {nullptr};
  GMANParameterList pl(dictionary, 1, tokens, parms, /*vertex=*/1, /*varying=*/0);

  check(pl.getPointer(widthId) != nullptr, "a present zero-length entry still answers non-NULL");
}

void checkCopyOutlivesSource() {
  GMANDictionary dictionary;
  const auto kaId = dictionary.getTokenId(std::string(RI_KA));

  RtFloat ka = 2.5f;
  RtToken tokens[1] = {RI_KA};
  RtPointer parms[1] = {&ka};

  auto source = std::make_optional<GMANParameterList>(dictionary, 1, tokens, parms);
  GMANParameterList copy = *source;
  const auto sourcePointer = source->getPointer(kaId);
  const auto copyPointer = copy.getPointer(kaId);
  check(copyPointer == sourcePointer, "a copy starts out sharing its source's pointer");

  source.reset(); // the source is gone; the copy must still own the data

  check(copy.getPointer(kaId) == copyPointer, "a copy's pointer survives its source's destruction");
}

// RI_P is a VERTEX POINT: allocSize multiplies its type size by vertex, so
// vertex=-1 yields a negative element count, which allocSize rejects with
// RIE_RANGE before P's array is allocated. The first entry's array is
// already allocated at that point; nothing must remain live once the
// exception propagates.
void checkThrowingConstructionLeaksNothing() {
  GMANDictionary dictionary;

  RtFloat ka = 1.0f;
  RtFloat point[3] = {0.0f, 0.0f, 0.0f};
  RtToken tokens[2] = {RI_KA, RI_P};
  RtPointer parms[2] = {&ka, point};

  const auto before = liveAllocations.load();
  bool threw = false;
  try {
    GMANParameterList pl(dictionary, 2, tokens, parms, /*vertex=*/-1);
  } catch (GMANError const& error) {
    threw = error.getCode() == RIE_RANGE;
  }
  const auto after = liveAllocations.load();

  check(threw, "a negative vertex count is rejected with RIE_RANGE");
  check(after == before, "a construction that throws partway leaks nothing");
}

// A supplied length below the declared one stops the construction with
// RIE_CONSISTENCY at the short entry, after the entry before it was built;
// nothing may remain live once the exception propagates.
void checkShortfallPartwayLeaksNothing() {
  GMANDictionary dictionary;

  RtFloat ka = 1.0f;
  RtFloat points[6] = {0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f};
  RtToken tokens[2] = {RI_KA, RI_P};
  RtPointer parms[2] = {&ka, points};
  const RtInt supplied[2] = {1, 3};

  const auto before = liveAllocations.load();
  bool threw = false;
  try {
    GMANParameterList pl(dictionary, 2, tokens, parms, /*vertex=*/2, 1, 1, 1, supplied);
  } catch (GMANError const& error) {
    threw = error.getCode() == RIE_CONSISTENCY;
  }
  const auto after = liveAllocations.load();

  check(threw, "a short P after an allocated Ka is rejected with RIE_CONSISTENCY");
  check(after == before, "a shortfall partway through leaks nothing");
}

// A failed entry allocation becomes RIE_NOMEM naming the token, with the
// entry built before it released.
void checkFailedAllocationReportsNoMemory() {
#if GMAN_ADDRESS_SANITIZED
  // AddressSanitizer instruments gman_core's own allocations directly, so
  // this test's operator new override never runs for them. The plain
  // (non-ASan) build runs this exact check; skip the unreachable one here
  // rather than report a false pass.
  std::printf("skip: a failed allocation reports RIE_NOMEM (ASan-built)\n");
#else
  GMANDictionary dictionary;

  RtFloat ka = 1.0f;
  RtFloat point[3] = {0.0f, 0.0f, 0.0f};
  RtToken tokens[2] = {RI_KA, RI_P};
  RtPointer parms[2] = {&ka, point};
  constexpr std::size_t threshold = 1 << 20;
  constexpr RtInt vertices = 1 << 20;

  const auto before = liveAllocations.load();
  bool threw = false;
  RtInt code = RIE_NOERROR;
  RtInt severity = RIE_INFO;
  bool namesToken = false;
  failAllocationsAbove = threshold;
  try {
    GMANParameterList pl(dictionary, 2, tokens, parms, vertices);
  } catch (GMANError const& error) {
    threw = true;
    code = error.getCode();
    severity = error.getSeverity();
    namesToken = std::string(error.getMessage()).find("\"P\"") != std::string::npos;
  }
  failAllocationsAbove = static_cast<std::size_t>(-1);
  const auto after = liveAllocations.load();

  check(threw, "an allocation that fails throws");
  check(code == RIE_NOMEM, "a failed allocation reports RIE_NOMEM");
  check(severity == RIE_ERROR, "a failed allocation reports at RIE_ERROR");
  check(namesToken, "a failed allocation names the token");
  check(after == before, "a failed allocation leaks nothing");
#endif
}

} // namespace

int main() {
  checkDefaultAndMoved();
  checkZeroLengthEntry();
  checkCopyOutlivesSource();
  checkThrowingConstructionLeaksNothing();
  checkShortfallPartwayLeaksNothing();
  checkFailedAllocationReportsNoMemory();

  return checkSummary("GMANParameterList's contract holds");
}
