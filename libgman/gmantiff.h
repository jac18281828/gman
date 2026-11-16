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

namespace gman {

/*
 * The one seam onto libtiff. Every other source file under include,
 * libgman, libgmanrib, shaders, renderers, gman and gmansl reaches a TIFF
 * file only through TIFFReader and TIFFWriter below.
 *
 * Two classes, not one with a mode: a reader and a writer share nothing
 * beyond "a handle that must be closed," and each constructor establishes
 * its own invariant instead of a runtime flag re-asserting it.
 */

// Reads a TIFF's pixels as unpacked, top-left-oriented RGB -- three integer
// samples per pixel -- and its Pixar wrap-modes tag, the only tag gman's
// texture reader needs.
class TIFFReader {
public:
  explicit TIFFReader(std::string const& path);
  ~TIFFReader();

  TIFFReader(TIFFReader const&) = delete;
  TIFFReader& operator=(TIFFReader const&) = delete;
  TIFFReader(TIFFReader&&) = delete;
  TIFFReader& operator=(TIFFReader&&) = delete;

  // False when path could not be opened for reading.
  bool isOpen() const;

  // The Pixar wrap-modes tag's value, or nullopt when the tag is absent.
  std::optional<std::string> wrapModes() const;

  // The bits per sample the file stores, which decode's bitsPerSample may
  // undercut: a 16-bit file outside the layouts decode reads at full depth
  // comes back at 8.
  std::uint16_t storedBitsPerSample() const;

  // Decodes the image into rgb, three samples per pixel, each in
  // [0, 2^bitsPerSample - 1], and reports whether the read succeeded and
  // both dimensions are nonzero. bitsPerSample is 8 or 16.
  //
  // A strip-organised, top-left, contiguous 16-bit unsigned file -- RGB,
  // RGB with one associated-alpha sample, or one-sample greyscale, with no
  // other extra samples -- reads at 16 bits, the alpha sample dropped and a grey sample replicated to R,
  // G and B; width and height are checked before the read. Every other file
  // reads through libtiff's own 8-bit RGBA conversion and reports 8, its
  // samples unscaled: rgb is sized from the image's own width and height,
  // the read runs, and only then are both dimensions checked, so a failed
  // read is never masked by checking the dimensions first.
  bool decode(std::uint32_t& width, std::uint32_t& height, std::uint16_t& bitsPerSample,
              std::vector<std::uint16_t>& rgb);

private:
  struct tiff* handle;
};

// Writes an 8- or 16-bit RGB or RGBA TIFF one scanline at a time.
// Construction opens the file and sets the fields every gman TIFF shares:
// width, height, bitsPerSample, samplesPerPixel, top-left orientation, RGB
// photometric, contiguous planar config and compression -- gman's own
// six-way Compression, mapped to libtiff's constants by the seam's one
// switch over it.
class TIFFWriter {
public:
  TIFFWriter(std::string const& path, std::uint32_t width, std::uint32_t height, std::uint16_t samplesPerPixel,
             std::uint16_t bitsPerSample, OutputTIFF::Compression compression);
  ~TIFFWriter();

  TIFFWriter(TIFFWriter const&) = delete;
  TIFFWriter& operator=(TIFFWriter const&) = delete;
  TIFFWriter(TIFFWriter&&) = delete;
  TIFFWriter& operator=(TIFFWriter&&) = delete;

  // False when path could not be opened for writing.
  bool isOpen() const;

  void setRowsPerStrip(std::uint32_t rowsPerStrip);

  // libtiff's own default strip size for a row of hint bytes.
  std::uint32_t defaultStripSize(std::uint32_t hint) const;

  void setImageDescription(std::string const& text);
  void setWrapModes(std::string const& modes);
  void setTextureFormat(std::string const& format);

  // Tags the fourth of a 4-sample-per-pixel file as associated alpha.
  // Call before the first writeScanline.
  void tagAlphaAssociated();

  // The byte length libtiff expects for one scanline of this file -- a
  // caller sizes its row buffer against this rather than trusting
  // samplesPerPixel * width to match.
  std::size_t scanlineSize() const;

  // Writes one scanline at row; false on failure.
  bool writeScanline(unsigned char* data, std::uint32_t row);

private:
  struct tiff* handle;
};

} // namespace gman
