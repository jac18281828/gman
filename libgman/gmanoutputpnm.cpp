/* SPDX-License-Identifier: LGPL-2.1-or-later */

/* This is part of GMAN, a RenderMan-compatible renderer.
 *
 * Copyright (c) 2001, 2000, 1999  John Cairns
 *
 * Author: John Cairns <john@2ad.com>
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

#include <cstdio>

#include "gmanerror.h"
#include "gmanoutput.h"
#include "gmanoutputpnm.h"
#include "ri.h"

namespace gman {

/*
 * RenderMan API gman::OutputPNM
 *
 */

// default constructor
OutputPNM::OutputPNM(const char* path, int width, int height) : GMANOutput(path, width, height) {};

// default destructor
OutputPNM::~OutputPNM() {};

// Writes a binary P6 portable pixmap directly, with no netpbm dependency.
// PNM's own P6 format holds one byte a sample, so this driver's widest
// sample stays 8 bits: GMANOutput::maxBitsPerSample's default.
RtVoid OutputPNM::writeImage(GMANOutput::DisplayMode /*mode*/, std::vector<std::uint16_t> const& samples,
                             int /*bitsPerSample*/, RtFloat /*gamma*/) {

  FILE* ppmFile = std::fopen(outputName.c_str(), "wb");
  if (!ppmFile) {
    std::string errorMsg("Unable to open output file: ");
    errorMsg.append(outputName);
    throw(GMANError(RIE_SYSTEM, RIE_SEVERE, errorMsg.c_str()));
  }

  // A short fprintf/fwrite, or a failed fclose (stdio's buffered data
  // flushes there, so a write can still fail at that point), all report
  // the same "Unable to write output file" error, built once here.
  auto writeFailure = [this]() {
    std::string errorMsg("Unable to write output file: ");
    errorMsg.append(outputName);
    return GMANError(RIE_SYSTEM, RIE_SEVERE, errorMsg.c_str());
  };

  if (std::fprintf(ppmFile, "P6\n%d %d\n255\n", xres, yres) < 0) {
    std::fclose(ppmFile);
    throw(writeFailure());
  }

  for (int row = 0; row < yres; row++) {
    for (int col = 0; col < xres; col++) {
      const std::size_t idx = 4 * ((std::size_t)row * (std::size_t)xres + (std::size_t)col);

      const unsigned char rgb[3] = {static_cast<unsigned char>(samples[idx + 0]),
                                    static_cast<unsigned char>(samples[idx + 1]),
                                    static_cast<unsigned char>(samples[idx + 2])};
      if (std::fwrite(rgb, 1, sizeof(rgb), ppmFile) != sizeof(rgb)) {
        std::fclose(ppmFile);
        throw(writeFailure());
      }
    }
  }

  if (std::fclose(ppmFile) != 0) {
    throw(writeFailure());
  }
}

} // namespace gman
