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

// Whether the file carries exactly one associated-alpha extra sample.
bool hasOnlyAssociatedAlpha(TIFF* handle) {
  std::uint16_t count = 0;
  std::uint16_t const* types = nullptr;
  return TIFFGetField(handle, TIFFTAG_EXTRASAMPLES, &count, &types) != 0 && count == 1 && types != nullptr &&
         types[0] == EXTRASAMPLE_ASSOCALPHA;
}

// The samples per pixel of a file readable at full 16-bit depth, or 0 when
// the file is not one: unsigned 16-bit samples in strips, one contiguous
// plane, top-left oriented, and either RGB (with at most an associated-alpha
// fourth sample) or one-sample greyscale.
std::uint16_t fullDepthSamplesPerPixel(TIFF* handle) {
  std::uint16_t bitsPerSample = 0, sampleFormat = 0, planarConfig = 0, orientation = 0, samplesPerPixel = 0,
                photometric = 0;
  TIFFGetFieldDefaulted(handle, TIFFTAG_BITSPERSAMPLE, &bitsPerSample);
  TIFFGetFieldDefaulted(handle, TIFFTAG_SAMPLEFORMAT, &sampleFormat);
  TIFFGetFieldDefaulted(handle, TIFFTAG_PLANARCONFIG, &planarConfig);
  TIFFGetFieldDefaulted(handle, TIFFTAG_ORIENTATION, &orientation);
  TIFFGetFieldDefaulted(handle, TIFFTAG_SAMPLESPERPIXEL, &samplesPerPixel);
  if (bitsPerSample != 16 || sampleFormat != SAMPLEFORMAT_UINT || planarConfig != PLANARCONFIG_CONTIG ||
      orientation != ORIENTATION_TOPLEFT || TIFFIsTiled(handle) != 0) {
    return 0;
  }
  if (TIFFGetField(handle, TIFFTAG_PHOTOMETRIC, &photometric) == 0) {
    return 0;
  }
  if (photometric == PHOTOMETRIC_MINISBLACK && samplesPerPixel == 1) {
    return samplesPerPixel;
  }
  if (photometric == PHOTOMETRIC_RGB && samplesPerPixel == 3) {
    return samplesPerPixel;
  }
  if (photometric == PHOTOMETRIC_RGB && samplesPerPixel == 4 && hasOnlyAssociatedAlpha(handle)) {
    return samplesPerPixel;
  }
  return 0;
}

// Reads a file fullDepthSamplesPerPixel accepted, one scanline at a time.
// False on a scanline error.
bool decodeFullDepth(TIFF* handle, std::uint32_t width, std::uint32_t height, std::uint16_t samplesPerPixel,
                     std::vector<std::uint16_t>& rgb) {
  std::vector<std::uint16_t> scanline(((std::size_t)TIFFScanlineSize(handle) + 1) / 2);
  rgb.resize((std::size_t)width * (std::size_t)height * 3);
  for (std::uint32_t y = 0; y < height; ++y) {
    if (TIFFReadScanline(handle, scanline.data(), y, 0) < 0) {
      return false;
    }
    std::uint16_t* out = rgb.data() + (std::size_t)y * (std::size_t)width * 3;
    for (std::size_t x = 0; x < width; ++x) {
      std::uint16_t const* in = scanline.data() + x * samplesPerPixel;
      const bool grey = samplesPerPixel == 1;
      out[x * 3 + 0] = in[0];
      out[x * 3 + 1] = grey ? in[0] : in[1];
      out[x * 3 + 2] = grey ? in[0] : in[2];
    }
  }
  return true;
}

// Reads any file through libtiff's own 8-bit RGBA conversion.
bool decodeEightBit(TIFF* handle, std::uint32_t width, std::uint32_t height, std::vector<std::uint16_t>& rgb) {
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

} // namespace

TIFFReader::TIFFReader(std::string const& path) : handle(TIFFOpen(path.c_str(), "r")) {}

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

std::uint16_t TIFFReader::storedBitsPerSample() const {
  std::uint16_t bitsPerSample = 0;
  TIFFGetFieldDefaulted(handle, TIFFTAG_BITSPERSAMPLE, &bitsPerSample);
  return bitsPerSample;
}

bool TIFFReader::decode(std::uint32_t& width, std::uint32_t& height, std::uint16_t& bitsPerSample,
                        std::vector<std::uint16_t>& rgb) {
  TIFFGetField(handle, TIFFTAG_IMAGEWIDTH, &width);
  TIFFGetField(handle, TIFFTAG_IMAGELENGTH, &height);

  const std::uint16_t samplesPerPixel = fullDepthSamplesPerPixel(handle);
  if (samplesPerPixel == 0) {
    bitsPerSample = 8;
    return decodeEightBit(handle, width, height, rgb);
  }

  bitsPerSample = 16;
  return width != 0 && height != 0 && decodeFullDepth(handle, width, height, samplesPerPixel, rgb);
}

TIFFWriter::TIFFWriter(std::string const& path, std::uint32_t width, std::uint32_t height,
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

void TIFFWriter::setImageDescription(std::string const& text) {
  TIFFSetField(handle, TIFFTAG_IMAGEDESCRIPTION, text.c_str());
}

void TIFFWriter::setWrapModes(std::string const& modes) {
  TIFFSetField(handle, TIFFTAG_PIXAR_WRAPMODES, modes.c_str());
}

void TIFFWriter::setTextureFormat(std::string const& format) {
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
