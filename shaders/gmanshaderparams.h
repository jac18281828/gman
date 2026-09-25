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

#include "gmancolor.h"
#include "gmandictionary.h"
#include "gmanerror.h"
#include "gmanparameterlist.h"
#include "gmanpolygon.h"

namespace gmanshader {

// Every shader parameter here is optional (RiSurface "matte" may pass none
// of them), so probing for one is routine, not exceptional.
// GMANParameterList::getPointer returns NULL for a token this parameter
// list doesn't carry. The try/catch is still load-bearing, for the other
// call: GMANDictionary::getTokenId throws RIE_BADTOKEN for a name the
// dictionary has never seen, which is what a shader asking for a parameter
// nobody ever declared does. getTokenId resolves against
// gman::standardDictionary() rather than pl's own dictionary; both agree
// on a standard token's ID.
inline RtFloat* tryGetFloatParam(GMANParameterList const& pl, RtToken token) {
  try {
    return (RtFloat*)pl.getPointer(gman::standardDictionary().getTokenId(token));
  } catch (GMANError&) {
    return NULL;
  }
}

inline RtFloat getFloatParam(GMANParameterList const& pl, RtToken token, RtFloat def) {
  RtFloat* p = tryGetFloatParam(pl, token);
  return p ? p[0] : def;
}

inline GMANColor getColorParam(GMANParameterList const& pl, RtToken token, const GMANColor& def) {
  RtFloat* p = tryGetFloatParam(pl, token);
  return p ? GMANColor(p[0], p[1], p[2]) : def;
}

// GMANParameterList stores a STRING parameter as std::string[], not
// RtFloat[] -- getPointer's void* still needs the caller's own cast, same
// as tryGetFloatParam above, just to std::string rather than RtFloat.
inline std::string* tryGetStringParam(GMANParameterList const& pl, RtToken token) {
  try {
    return (std::string*)pl.getPointer(gman::standardDictionary().getTokenId(token));
  } catch (GMANError&) {
    return NULL;
  }
}

inline std::string getStringParam(GMANParameterList const& pl, RtToken token, const std::string& def) {
  std::string* p = tryGetStringParam(pl, token);
  return p ? p[0] : def;
}

} // namespace gmanshader
