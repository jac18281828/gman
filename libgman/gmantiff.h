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

#ifndef __GMAN_GMANTIFF_H
#define __GMAN_GMANTIFF_H 1

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "gmanoutputtiff.h"

// libtiff's own handle, typedef struct tiff TIFF, spelled by libtiff's
// own I/O header. Forward declared by its tag name so a pointer to it
// needs no libtiff header; only gmantiff.cpp ever includes one.
struct tiff;

/*
 * The one seam onto libtiff. Every other source file under include,
 * libgman, libgmanrib, shaders, renderers, gman and gmansl reaches a TIFF
 * file only through GMANTIFFReader and GMANTIFFWriter below.
 *
 * Two classes, not one with a mode: a reader and a writer share nothing
 * beyond "a handle that must be closed," and each constructor establishes
 * its own invariant instead of a runtime flag re-asserting it.
 */

// Reads a TIFF's pixels as unpacked, top-left-oriented RGB -- three bytes
// per pixel -- and its Pixar wrap-modes tag, the only tag gman's texture
// reader needs.
class GMANTIFFReader {
public:
  explicit GMANTIFFReader(const std::string& path);
  ~GMANTIFFReader();

  GMANTIFFReader(const GMANTIFFReader&) = delete;
  GMANTIFFReader& operator=(const GMANTIFFReader&) = delete;
  GMANTIFFReader(GMANTIFFReader&&) = delete;
  GMANTIFFReader& operator=(GMANTIFFReader&&) = delete;

  // False when path could not be opened for reading.
  bool isOpen() const;

  // The Pixar wrap-modes tag's value, or nullopt when the tag is absent.
  std::optional<std::string> wrapModes() const;

  // Sizes rgb from the image's own width and height, decodes into it, then
  // reports whether the read succeeded and both dimensions are nonzero --
  // in that order, so a failed read is never masked by checking the
  // dimensions first.
  bool decode(std::uint32_t& width, std::uint32_t& height, std::vector<unsigned char>& rgb);

private:
  struct tiff* handle;
};

// Writes an 8-bit RGB or RGBA TIFF one scanline at a time. Construction
// opens the file and sets the fields every gman TIFF shares: width,
// height, 8 bits per sample, samplesPerPixel, top-left orientation, RGB
// photometric, contiguous planar config and compression -- gman's own
// six-way Compression, mapped to libtiff's constants by the seam's one
// switch over it.
class GMANTIFFWriter {
public:
  GMANTIFFWriter(const std::string& path, std::uint32_t width, std::uint32_t height, std::uint16_t samplesPerPixel,
                 GMANOutputTIFF::Compression compression);
  ~GMANTIFFWriter();

  GMANTIFFWriter(const GMANTIFFWriter&) = delete;
  GMANTIFFWriter& operator=(const GMANTIFFWriter&) = delete;
  GMANTIFFWriter(GMANTIFFWriter&&) = delete;
  GMANTIFFWriter& operator=(GMANTIFFWriter&&) = delete;

  // False when path could not be opened for writing.
  bool isOpen() const;

  void setRowsPerStrip(std::uint32_t rowsPerStrip);

  // libtiff's own default strip size for a row of hint bytes.
  std::uint32_t defaultStripSize(std::uint32_t hint) const;

  void setImageDescription(const std::string& text);
  void setWrapModes(const std::string& modes);
  void setTextureFormat(const std::string& format);

  // The byte length libtiff expects for one scanline of this file -- a
  // caller sizes its row buffer against this rather than trusting
  // samplesPerPixel * width to match.
  std::size_t scanlineSize() const;

  // Writes one scanline at row; false on failure.
  bool writeScanline(unsigned char* data, std::uint32_t row);

private:
  struct tiff* handle;
};

#endif
