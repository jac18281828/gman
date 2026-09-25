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
 * The replacement operator new/delete raybvhalloc_test.cpp counts through.
 * Kept in their own translation unit so a compiler that inlines a call site
 * back to malloc/free never sees both halves of one pairing in the same
 * file.
 */

#include <atomic>
#include <cstdlib>
#include <new>

namespace {

// Counts every operator new and operator delete call this process makes,
// so a batch of calls can be bracketed and its own delta checked against
// 0: an allocation freed within the same batch still moves it.
std::atomic<long> allocationCount{0};

} // namespace

long raybvhAllocationCount() { return allocationCount.load(); }

void* operator new(std::size_t n) {
  void* p = std::malloc(n != 0 ? n : 1);
  if (p == nullptr) {
    throw std::bad_alloc();
  }
  ++allocationCount;
  return p;
}

void operator delete(void* p) noexcept {
  if (p != nullptr) {
    ++allocationCount;
  }
  std::free(p);
}

// GCC's -Wsized-deallocation requires the sized form once the unsized one
// is replaced; both just forward, since this allocator counts calls, not
// sizes.
void operator delete(void* p, std::size_t) noexcept { ::operator delete(p); }

void* operator new[](std::size_t n) { return ::operator new(n); }

void operator delete[](void* p) noexcept { ::operator delete(p); }

void operator delete[](void* p, std::size_t) noexcept { ::operator delete(p); }
