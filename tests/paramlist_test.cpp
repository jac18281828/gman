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
 * allocated.
 */

#include <atomic>
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

} // namespace

void* operator new(std::size_t n) {
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
// vertex=-1 yields a negative element count, converted to an enormous
// size_t by the second entry's array new. Whether the library rejects it as
// bad_array_new_length or as a failed bad_alloc is implementation-defined --
// the former derives from the latter, so catching bad_alloc covers both.
// The first entry's array is already allocated at that point; nothing must
// remain live once the exception propagates.
void checkThrowingConstructionLeaksNothing() {
#if GMAN_ADDRESS_SANITIZED
  // AddressSanitizer instruments gman_core's own allocations directly, so
  // this test's operator new override never runs for them; ASan's
  // allocator hits its own maximum-request-size abort first, which no
  // catch clause observes. The plain (non-ASan) build runs this exact
  // check; skip the unreachable one here rather than report a false pass
  // or bring down the whole test binary.
  std::printf("skip: a construction that throws partway leaks nothing (ASan-built)\n");
#else
  GMANDictionary dictionary;

  RtFloat ka = 1.0f;
  RtFloat point[3] = {0.0f, 0.0f, 0.0f};
  RtToken tokens[2] = {RI_KA, RI_P};
  RtPointer parms[2] = {&ka, point};

  const auto before = liveAllocations.load();
  bool threw = false;
  try {
    GMANParameterList pl(dictionary, 2, tokens, parms, /*vertex=*/-1);
  } catch (std::bad_alloc const&) {
    threw = true;
  }
  const auto after = liveAllocations.load();

  check(threw, "a negative vertex count fails the allocation instead of succeeding");
  check(after == before, "a construction that throws partway leaks nothing");
#endif
}

} // namespace

int main() {
  checkDefaultAndMoved();
  checkZeroLengthEntry();
  checkCopyOutlivesSource();
  checkThrowingConstructionLeaksNothing();

  return checkSummary("GMANParameterList's contract holds");
}
