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

/*
 * GMANDictionary::getTypeSize's default: arm, reached whenever a TokenType
 * holds a value outside its nine enumerators, throws a GMANError that
 * carries RIE_BUG at RIE_SEVERE and prints as such.
 */

#include <iostream>
#include <sstream>
#include <string>

#include "check.h"
#include "gmandictionary.h"
#include "gmanerror.h"

namespace {

void checkThrownErrorFields() {
  GMANDictionary dictionary;
  bool threw = false;
  try {
    dictionary.getTypeSize(static_cast<GMANTokenEntry::TokenType>(9));
  } catch (GMANError const& error) {
    threw = true;
    check(error.getCode() == RIE_BUG, "an unknown token type throws RIE_BUG");
    check(error.getSeverity() == RIE_SEVERE, "an unknown token type throws RIE_SEVERE");
  }
  check(threw, "an out-of-range token type throws GMANError");
}

void checkKnownTypeSizes() {
  GMANDictionary dictionary;
  check(dictionary.getTypeSize(GMANTokenEntry::FLOAT) == 1, "FLOAT is size 1");
  check(dictionary.getTypeSize(GMANTokenEntry::POINT) == 3, "POINT is size 3");
  check(dictionary.getTypeSize(GMANTokenEntry::VECTOR) == 3, "VECTOR is size 3");
  check(dictionary.getTypeSize(GMANTokenEntry::NORMAL) == 3, "NORMAL is size 3");
  check(dictionary.getTypeSize(GMANTokenEntry::COLOR) == 3, "COLOR is size 3");
  check(dictionary.getTypeSize(GMANTokenEntry::STRING) == 1, "STRING is size 1");
  check(dictionary.getTypeSize(GMANTokenEntry::MATRIX) == 16, "MATRIX is size 16");
  check(dictionary.getTypeSize(GMANTokenEntry::HPOINT) == 4, "HPOINT is size 4");
  check(dictionary.getTypeSize(GMANTokenEntry::INTEGER) == 1, "INTEGER is size 1");
}

void checkPrintedDiagnosticIsHonest() {
  GMANDictionary dictionary;
  try {
    dictionary.getTypeSize(static_cast<GMANTokenEntry::TokenType>(9));
    check(false, "an out-of-range token type throws GMANError");
    return;
  } catch (GMANError& error) {
    std::ostringstream captured;
    std::streambuf* const savedBuffer = std::cout.rdbuf(captured.rdbuf());
    GMANHandleError(error);
    std::cout.rdbuf(savedBuffer);

    const auto text = captured.str();
    check(text.find("RIE_BUG") != std::string::npos, "the printed diagnostic names RIE_BUG");
    check(text.find("RIE_NOMEM") == std::string::npos, "the printed diagnostic no longer names RIE_NOMEM");
  }
}

} // namespace

int main() {
  checkThrownErrorFields();
  checkKnownTypeSizes();
  checkPrintedDiagnosticIsHonest();

  return checkSummary("GMANDictionary::getTypeSize reports an unknown token type truthfully");
}
