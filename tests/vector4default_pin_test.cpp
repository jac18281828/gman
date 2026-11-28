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
 * Pins a contract decision, not a value: a default-constructed Vector4 has w
 * equal to 0, as its x, y and z are 0. Vector4() leaves w uninitialised, so
 * the object holds whatever its storage held, and reading w is undefined
 * behavior that no test can pin as a value. The test constructs the object
 * in storage filled with 0xff bytes, where the uninitialised w reads as NaN,
 * so the failure is deterministic. WILL_FAIL until the constructor
 * initialises w.
 */

#include <cstddef>
#include <cstring>
#include <new>

#include "check.h"
#include "geometrycheck.h"
#include "gmanreal.h"
#include "gmanvector4.h"
#include "ri.h"

namespace {

using namespace geometrycheck;

template <typename T> void testDefaultConstructorInitialisesW() {
  alignas(gman::Vector4<T>) std::byte storage[sizeof(gman::Vector4<T>)];
  std::memset(storage, 0xff, sizeof(storage));
  auto* const v = new (storage) gman::Vector4<T>();
  expectExact(member<T>("Vector4", "Vector4() initialises w to 0"), v->getW(), 0);
  v->~Vector4();
}

} // namespace

int main() {
  testDefaultConstructorInitialisesW<RtFloat>();
  testDefaultConstructorInitialisesW<gman::PartnerReal>();
  return checkSummary("vector4default: all checks passed");
}
