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

namespace gman {

namespace {

// gman's own six-way Compression, mapped to libtiff's constants -- the one
// switch every writer shares instead of one copy per caller.
uint16_t libtiffCompression(OutputTIFF::Compression compression) {
  switch (compression) {
  case OutputTIFF::NONE:
    return COMPRESSION_NONE;
  case OutputTIFF::PACKBITS:
    return COMPRESSION_PACKBITS;
  case OutputTIFF::LZW:
    return COMPRESSION_LZW;
  case OutputTIFF::CCITTRLE:
    return COMPRESSION_CCITTRLE;
  case OutputTIFF::CCITTFAX3:
    return COMPRESSION_CCITTFAX3;
  case OutputTIFF::CCITTFAX4:
    return COMPRESSION_CCITTFAX4;
  }
  return COMPRESSION_NONE;
}

} // namespace

TIFFReader::TIFFReader(const std::string& path) : handle(TIFFOpen(path.c_str(), "r")) {}

TIFFReader::~TIFFReader() {
  if (handle != nullptr) {
    TIFFClose(handle);
  }
}

bool TIFFReader::isOpen() const { return handle != nullptr; }

std::optional<std::string> TIFFReader::wrapModes() const {
  char* tag = nullptr;
  if (TIFFGetField(handle, TIFFTAG_PIXAR_WRAPMODES, &tag) && tag != nullptr) {
    return std::string(tag);
  }
  return std::nullopt;
}

bool TIFFReader::decode(std::uint32_t& width, std::uint32_t& height, std::vector<unsigned char>& rgb) {
  TIFFGetField(handle, TIFFTAG_IMAGEWIDTH, &width);
  TIFFGetField(handle, TIFFTAG_IMAGELENGTH, &height);

  std::vector<std::uint32_t> raster((std::size_t)width * (std::size_t)height, 0);
  const int ok = TIFFReadRGBAImageOriented(handle, width, height, raster.data(), ORIENTATION_TOPLEFT, 0);
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

TIFFWriter::TIFFWriter(const std::string& path, std::uint32_t width, std::uint32_t height,
                       std::uint16_t samplesPerPixel, std::uint16_t bitsPerSample, OutputTIFF::Compression compression)
    : handle(TIFFOpen(path.c_str(), "w")) {
  if (handle == nullptr) {
    return;
  }
  TIFFSetField(handle, TIFFTAG_IMAGEWIDTH, width);
  TIFFSetField(handle, TIFFTAG_IMAGELENGTH, height);
  TIFFSetField(handle, TIFFTAG_BITSPERSAMPLE, bitsPerSample);
  TIFFSetField(handle, TIFFTAG_SAMPLESPERPIXEL, samplesPerPixel);
  TIFFSetField(handle, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT);
  TIFFSetField(handle, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB);
  TIFFSetField(handle, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
  TIFFSetField(handle, TIFFTAG_COMPRESSION, libtiffCompression(compression));
}

TIFFWriter::~TIFFWriter() {
  if (handle != nullptr) {
    TIFFClose(handle);
  }
}

bool TIFFWriter::isOpen() const { return handle != nullptr; }

void TIFFWriter::setRowsPerStrip(std::uint32_t rowsPerStrip) {
  TIFFSetField(handle, TIFFTAG_ROWSPERSTRIP, rowsPerStrip);
}

std::uint32_t TIFFWriter::defaultStripSize(std::uint32_t hint) const {
  return (std::uint32_t)TIFFDefaultStripSize(handle, hint);
}

void TIFFWriter::setImageDescription(const std::string& text) {
  TIFFSetField(handle, TIFFTAG_IMAGEDESCRIPTION, text.c_str());
}

void TIFFWriter::setWrapModes(const std::string& modes) {
  TIFFSetField(handle, TIFFTAG_PIXAR_WRAPMODES, modes.c_str());
}

void TIFFWriter::setTextureFormat(const std::string& format) {
  TIFFSetField(handle, TIFFTAG_PIXAR_TEXTUREFORMAT, format.c_str());
}

void TIFFWriter::tagAlphaAssociated() {
  const uint16_t extraSample = EXTRASAMPLE_ASSOCALPHA;
  TIFFSetField(handle, TIFFTAG_EXTRASAMPLES, 1, &extraSample);
}

std::size_t TIFFWriter::scanlineSize() const { return (std::size_t)TIFFScanlineSize(handle); }

bool TIFFWriter::writeScanline(unsigned char* data, std::uint32_t row) {
  return TIFFWriteScanline(handle, data, row, 0) >= 0;
}

} // namespace gman
