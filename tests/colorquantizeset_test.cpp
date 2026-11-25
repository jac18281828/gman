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

#include "check.h"
#include "gmanoptions.h"
#include "gmanquantize.h"

namespace {

void checkFresh() {
  GMANOptions const options;
  check(!options.isColorQuantizeSet(), "fresh: Quantize \"rgba\" reads unset");
  GMANQuantize const& request = options.getColorQuantize();
  check(request.one == 255 && request.min == 0 && request.max == 255 && request.ditheramplitude == 0.5f,
        "fresh: the request reads 255 0 255 0.5");
}

void checkRestatedDefaults() {
  GMANOptions options;
  options.setColorQuantize(255, 0, 255, 0.5f);
  check(options.isColorQuantizeSet(), "restated defaults: Quantize \"rgba\" reads set");
  GMANQuantize const& request = options.getColorQuantize();
  check(request.one == 255 && request.min == 0 && request.max == 255 && request.ditheramplitude == 0.5f,
        "restated defaults: the request still reads 255 0 255 0.5");
}

void checkDepthAlone() {
  GMANOptions options;
  options.setDepthQuantize(0, 0, 0, 0.0f);
  check(!options.isColorQuantizeSet(), "Quantize \"z\" alone: Quantize \"rgba\" reads unset");
}

void checkCopy() {
  GMANOptions options;
  options.setColorQuantize(0, 0, 0, 0.0f);
  GMANOptions const copy(options);
  check(copy.isColorQuantizeSet(), "copy: carries the set flag");

  GMANOptions const unsetCopy{GMANOptions()};
  check(!unsetCopy.isColorQuantizeSet(), "copy: carries the unset flag");
}

} // namespace

int main() {
  checkFresh();
  checkRestatedDefaults();
  checkDepthAlone();
  checkCopy();

  return checkSummary("colorquantizeset holds");
}
