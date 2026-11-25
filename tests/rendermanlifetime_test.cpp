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
 * GMANRenderManImpl frees everything it owns: two RiWorldBegins in one
 * RiBegin/RiEnd cycle, two RiBegins and an impl destroyed before RiEnd each
 * return the live heap to its starting size. So does the C API: a
 * RiBegin/RiEnd cycle frees its context on RiEnd, and a context still open
 * when the process exits is freed by the C API's static destructor. Each case
 * runs its sequence once to fill the process-lifetime caches, then measures a
 * second run.
 *
 * The count is the heap's own, taken in process, because LeakSanitizer aborts
 * every test on arm64 macOS. Three counters serve three builds, and each is
 * process-wide, so this test is its own executable.
 *
 * Under AddressSanitizer the allocator answers every call, the library's
 * included, so a replaced operator new would count the executable's
 * allocations and miss the library's matching frees; the test replaces nothing
 * and reads ASan's allocated-byte total.
 *
 * On macOS without AddressSanitizer the test replaces nothing and reads
 * malloc_zone_statistics' blocks in use. Under the release preset's link-time
 * optimization Apple's linker hides an executable's replaced operator new and
 * delete unless it links with -export_dynamic, so the library's allocations
 * and frees bypass the counter and the net imbalance fails with no leak. The
 * zone count needs no link flag and also counts C malloc blocks.
 *
 * Elsewhere the test replaces the global allocation functions and counts live
 * blocks.
 */

#include <cstddef>
#include <format>
#include <new>
#include <string>

#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define GMAN_LIFETIME_TEST_ASAN 1
#endif
#endif
#if !defined(GMAN_LIFETIME_TEST_ASAN) && defined(__SANITIZE_ADDRESS__)
#define GMAN_LIFETIME_TEST_ASAN 1
#endif

#if !defined(GMAN_LIFETIME_TEST_ASAN) && defined(__APPLE__)
#define GMAN_LIFETIME_TEST_ZONES 1
#endif

#if defined(GMAN_LIFETIME_TEST_ASAN)
#include <sanitizer/allocator_interface.h>
#elif defined(GMAN_LIFETIME_TEST_ZONES)
#include <malloc/malloc.h>
#else
#include <atomic>
#include <cstdlib>

#include <stdlib.h>
#endif

#include "check.h"
#include "gmanerror.h"
#include "gmanrendermanimpl.h"
#include "ri.h"

#if defined(GMAN_LIFETIME_TEST_ASAN)

namespace {

constexpr char const* kHeapUnit = "bytes";

std::ptrdiff_t liveHeap() { return static_cast<std::ptrdiff_t>(__sanitizer_get_current_allocated_bytes()); }

} // namespace

#elif defined(GMAN_LIFETIME_TEST_ZONES)

namespace {

constexpr char const* kHeapUnit = "blocks";

std::ptrdiff_t liveHeap() {
  malloc_statistics_t stats{};
  malloc_zone_statistics(nullptr, &stats);
  return static_cast<std::ptrdiff_t>(stats.blocks_in_use);
}

} // namespace

#else

namespace {

constexpr char const* kHeapUnit = "blocks";

std::atomic<std::ptrdiff_t> liveBlocks{0};

std::ptrdiff_t liveHeap() { return liveBlocks.load(); }

void* allocateBlock(std::size_t size, std::size_t alignment) noexcept {
  std::size_t const bytes = size == 0 ? 1 : size;
  void* block = nullptr;
  if (alignment <= alignof(std::max_align_t)) {
    block = std::malloc(bytes);
  } else if (posix_memalign(&block, alignment, bytes) != 0) {
    block = nullptr;
  }
  if (block != nullptr) {
    liveBlocks.fetch_add(1);
  }
  return block;
}

void* allocateOrThrow(std::size_t size, std::size_t alignment) {
  void* const block = allocateBlock(size, alignment);
  if (block == nullptr) {
    throw std::bad_alloc();
  }
  return block;
}

void releaseBlock(void* block) noexcept {
  if (block == nullptr) {
    return;
  }
  liveBlocks.fetch_sub(1);
  std::free(block);
}

} // namespace

void* operator new(std::size_t size) { return allocateOrThrow(size, alignof(std::max_align_t)); }
void* operator new[](std::size_t size) { return allocateOrThrow(size, alignof(std::max_align_t)); }
void* operator new(std::size_t size, std::nothrow_t const&) noexcept {
  return allocateBlock(size, alignof(std::max_align_t));
}
void* operator new[](std::size_t size, std::nothrow_t const&) noexcept {
  return allocateBlock(size, alignof(std::max_align_t));
}
void* operator new(std::size_t size, std::align_val_t alignment) {
  return allocateOrThrow(size, static_cast<std::size_t>(alignment));
}
void* operator new[](std::size_t size, std::align_val_t alignment) {
  return allocateOrThrow(size, static_cast<std::size_t>(alignment));
}
void* operator new(std::size_t size, std::align_val_t alignment, std::nothrow_t const&) noexcept {
  return allocateBlock(size, static_cast<std::size_t>(alignment));
}
void* operator new[](std::size_t size, std::align_val_t alignment, std::nothrow_t const&) noexcept {
  return allocateBlock(size, static_cast<std::size_t>(alignment));
}

void operator delete(void* block) noexcept { releaseBlock(block); }
void operator delete[](void* block) noexcept { releaseBlock(block); }
void operator delete(void* block, std::size_t) noexcept { releaseBlock(block); }
void operator delete[](void* block, std::size_t) noexcept { releaseBlock(block); }
void operator delete(void* block, std::nothrow_t const&) noexcept { releaseBlock(block); }
void operator delete[](void* block, std::nothrow_t const&) noexcept { releaseBlock(block); }
void operator delete(void* block, std::align_val_t) noexcept { releaseBlock(block); }
void operator delete[](void* block, std::align_val_t) noexcept { releaseBlock(block); }
void operator delete(void* block, std::size_t, std::align_val_t) noexcept { releaseBlock(block); }
void operator delete[](void* block, std::size_t, std::align_val_t) noexcept { releaseBlock(block); }
void operator delete(void* block, std::align_val_t, std::nothrow_t const&) noexcept { releaseBlock(block); }
void operator delete[](void* block, std::align_val_t, std::nothrow_t const&) noexcept { releaseBlock(block); }

#endif

namespace {

void worldEndRaisesNoOutput(GMANRenderManImpl& impl) {
  try {
    impl.RiWorldEnd();
  } catch (GMANError const&) {
  }
}

void twoWorlds() {
  auto* const impl = new GMANRenderManImpl();
  impl->RiBegin(RI_NULL);
  impl->RiWorldBegin();
  worldEndRaisesNoOutput(*impl);
  impl->RiWorldBegin();
  worldEndRaisesNoOutput(*impl);
  impl->RiEnd();
  delete impl;
}

void twoBegins() {
  auto* const impl = new GMANRenderManImpl();
  impl->RiBegin(RI_NULL);
  impl->RiBegin(RI_NULL);
  impl->RiEnd();
  delete impl;
}

void noEnd() {
  auto* const impl = new GMANRenderManImpl();
  impl->RiBegin(RI_NULL);
  impl->RiWorldBegin();
  delete impl;
}

void capiBeginEnd() {
  RiBegin(RI_NULL);
  RiEnd();
}

void openContextAtExit() {
  RiBegin(RI_NULL);
  RiSurface("plastic", RI_NULL);
  RiWorldBegin();
  RiSphere(1, -1, 1, 360, RI_NULL);
}

template <typename Sequence> void checkReturnsHeap(std::string const& name, Sequence sequence) {
  sequence();
  std::ptrdiff_t const before = liveHeap();
  sequence();
  std::ptrdiff_t const delta = liveHeap() - before;
  check(delta == 0, std::format("{} leaves the heap where it found it (delta {} {})", name, delta, kHeapUnit));
}

} // namespace

int main() {
  RiErrorHandler(RiErrorIgnore);

  checkReturnsHeap("two RiWorldBegins in one cycle", twoWorlds);
  checkReturnsHeap("two RiBegins", twoBegins);
  checkReturnsHeap("an impl destroyed before RiEnd", noEnd);
  checkReturnsHeap("a C API RiBegin and RiEnd", capiBeginEnd);

  openContextAtExit();

  return checkSummary("GMANRenderManImpl frees what it owns");
}
