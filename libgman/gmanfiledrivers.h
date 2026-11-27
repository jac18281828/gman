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

#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "gman/export.h"
#include "gmanoptions.h"
#include "gmanoutput.h"
#include "gmanquantize.h"

/*
 * The file-Display driver table, generated at configure time from
 * gmanfiledrivers.cpp.in: one entry per known driver, in a fixed order,
 * carrying a factory only when that driver was actually compiled. A known
 * but disabled extension is a diagnosed error, not a silently missing
 * feature; an unknown one is the same diagnostic RiWorldBegin has always
 * given.
 */

namespace gman {

// The sample depth a command line asks of the drivers that quantize: 8 or 16
// bits, the only two depths core is handed.
enum class BitDepth { eight, sixteen };

using FileDriverFactory = std::unique_ptr<GMANOutput> (*)(char const*, int, int);
using FileDriverMatcher = bool (*)(GMANOutput const&);

struct FileDriverEntry {
  std::string name;
  std::vector<std::string> extensions;
  std::string library; // empty when the driver needs no library
  std::string displayName;
  FileDriverFactory factory; // null when not compiled into this build
  FileDriverMatcher matches; // null when not compiled into this build
  bool straightAlpha;        // stores colour unassociated from alpha
  bool doesNotQuantize;      // writes float and refuses a nonzero Quantize one
};

// Returns the driver for `extension` (compared case-sensitively). Throws
// GMANError(RIE_BADFILE, ...) for a known but disabled extension, naming
// the extension and the missing library, or for an unrecognized one.
std::unique_ptr<GMANOutput> makeFileOutput(std::string const& extension, char const* path, int width, int height);

// The compiled file drivers' names, in table order.
GMAN_EXPORT std::vector<std::string> fileDrivers();

// The table entry whose driver class `output` is, or null for an object no
// compiled entry matches.
FileDriverEntry const* fileDriverEntry(GMANOutput const& output);

// Throws GMANError at RIE_ERROR, naming the driver and the request, when
// `output` cannot store `quantize` exactly: RIE_RANGE for a value no driver
// could mean, RIE_INCAPABLE for a valid request the driver's sample width,
// integer-only samples or lack of quantizing cannot hold. Returns for a
// request `output` honours.
void checkQuantize(GMANOutput const& output, GMANQuantize const& quantize);

// The Quantize "rgba" request `output` receives, resolved in order:
// - an output no table entry matches receives the request `options` stores;
// - a driver that does not quantize, which writes float, receives one 0 when
//   the request is unset and the stored request otherwise, whatever `depth`;
// - an absent `depth` leaves the stored request;
// - a set request must agree with `depth`: one above 0 asks for 16 bits when
//   max exceeds 255 and 8 otherwise, and one 0 asks for float, which agrees
//   with neither. Disagreement throws GMANError(RIE_CONSISTENCY, RIE_ERROR,
//   ...) naming the request and the depth; agreement, and a negative one that
//   checkQuantize reports, return the stored request;
// - an unset request at 16 bits resolves to 65535 0 65535 0.5, and throws
//   GMANError(RIE_INCAPABLE, RIE_ERROR, ...) when `output` cannot write 16
//   bits; at 8 bits it resolves to the stored default.
GMANQuantize quantizeRequest(GMANOutput const& output, GMANOptions const& options, std::optional<BitDepth> depth);

} // namespace gman
