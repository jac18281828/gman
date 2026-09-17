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

#include "gmantiff.h"

// The tree's one <tiffio.h>. Every other production file reaches a TIFF only
// through the classes above, so a libtiff symbol appearing anywhere else is
// a leak, not a convenience.
extern "C" {
#include <tiffio.h>
}

namespace {

// gman's own six-way Compression, mapped to libtiff's constants -- the one
// switch every writer shares instead of one copy per caller.
uint16_t libtiffCompression(GMANOutputTIFF::Compression compression) {
  switch (compression) {
    case GMANOutputTIFF::NONE:
      return COMPRESSION_NONE;
    case GMANOutputTIFF::PACKBITS:
      return COMPRESSION_PACKBITS;
    case GMANOutputTIFF::LZW:
      return COMPRESSION_LZW;
    case GMANOutputTIFF::CCITTRLE:
      return COMPRESSION_CCITTRLE;
    case GMANOutputTIFF::CCITTFAX3:
      return COMPRESSION_CCITTFAX3;
    case GMANOutputTIFF::CCITTFAX4:
      return COMPRESSION_CCITTFAX4;
  }
  return COMPRESSION_NONE;
}

}  // namespace

GMANTIFFReader::GMANTIFFReader(const std::string &path)
    : handle(TIFFOpen(path.c_str(), "r")) {}

GMANTIFFReader::~GMANTIFFReader() {
  if (handle != nullptr) {
    TIFFClose(handle);
  }
}

bool GMANTIFFReader::isOpen() const {
  return handle != nullptr;
}

std::optional<std::string> GMANTIFFReader::wrapModes() const {
  char *tag = nullptr;
  if (TIFFGetField(handle, TIFFTAG_PIXAR_WRAPMODES, &tag) && tag != nullptr) {
    return std::string(tag);
  }
  return std::nullopt;
}

bool GMANTIFFReader::decode(std::uint32_t &width, std::uint32_t &height,
                            std::vector<unsigned char> &rgb) {
  TIFFGetField(handle, TIFFTAG_IMAGEWIDTH, &width);
  TIFFGetField(handle, TIFFTAG_IMAGELENGTH, &height);

  std::vector<std::uint32_t> raster((std::size_t)width * (std::size_t)height,
                                    0);
  const int ok = TIFFReadRGBAImageOriented(handle, width, height, raster.data(),
                                           ORIENTATION_TOPLEFT, 0);
  if (ok == 0 || width == 0 || height == 0) {
    return false;
  }

  rgb.resize((std::size_t)width * (std::size_t)height * 3);
  for (std::size_t i = 0; i < raster.size(); ++i) {
    rgb[i * 3 + 0] = TIFFGetR(raster[i]);
    rgb[i * 3 + 1] = TIFFGetG(raster[i]);
    rgb[i * 3 + 2] = TIFFGetB(raster[i]);
  }
  return true;
}

GMANTIFFWriter::GMANTIFFWriter(const std::string &path, std::uint32_t width,
                               std::uint32_t height,
                               std::uint16_t samplesPerPixel,
                               GMANOutputTIFF::Compression compression)
    : handle(TIFFOpen(path.c_str(), "w")) {
  if (handle == nullptr) {
    return;
  }
  TIFFSetField(handle, TIFFTAG_IMAGEWIDTH, width);
  TIFFSetField(handle, TIFFTAG_IMAGELENGTH, height);
  TIFFSetField(handle, TIFFTAG_BITSPERSAMPLE, 8);
  TIFFSetField(handle, TIFFTAG_SAMPLESPERPIXEL, samplesPerPixel);
  TIFFSetField(handle, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT);
  TIFFSetField(handle, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB);
  TIFFSetField(handle, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
  TIFFSetField(handle, TIFFTAG_COMPRESSION, libtiffCompression(compression));
}

GMANTIFFWriter::~GMANTIFFWriter() {
  if (handle != nullptr) {
    TIFFClose(handle);
  }
}

bool GMANTIFFWriter::isOpen() const {
  return handle != nullptr;
}

void GMANTIFFWriter::setRowsPerStrip(std::uint32_t rowsPerStrip) {
  TIFFSetField(handle, TIFFTAG_ROWSPERSTRIP, rowsPerStrip);
}

std::uint32_t GMANTIFFWriter::defaultStripSize(std::uint32_t hint) const {
  return (std::uint32_t)TIFFDefaultStripSize(handle, hint);
}

void GMANTIFFWriter::setImageDescription(const std::string &text) {
  TIFFSetField(handle, TIFFTAG_IMAGEDESCRIPTION, text.c_str());
}

void GMANTIFFWriter::setWrapModes(const std::string &modes) {
  TIFFSetField(handle, TIFFTAG_PIXAR_WRAPMODES, modes.c_str());
}

void GMANTIFFWriter::setTextureFormat(const std::string &format) {
  TIFFSetField(handle, TIFFTAG_PIXAR_TEXTUREFORMAT, format.c_str());
}

std::size_t GMANTIFFWriter::scanlineSize() const {
  return (std::size_t)TIFFScanlineSize(handle);
}

bool GMANTIFFWriter::writeScanline(unsigned char *data, std::uint32_t row) {
  return TIFFWriteScanline(handle, data, row, 0) >= 0;
}
