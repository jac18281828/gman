/* SPDX-License-Identifier: LGPL-2.1-or-later */

/* This is part of the GNU GMAN Library, a FREE implementation of the
 * RenderMan Interface Specification.
 *
 * Copyright (c) 2026 John Cairns <john@2ad.com>
 */

/*
  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Library General Public
  License as published by the Free Software Foundation; either
  version 2 of the License, or (at your option) any later version.


  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Library General Public License for more details.

  You should have received a copy of the GNU Library General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.

  To contact the author of GNU GMAN, write to John Cairns, 607 E STUART ST,
  FT COLLINS, CO, 80525, USA, or write via E-mail john@2ad.com.
*/

#ifndef __GMAN_SHADERS_GMANSHADERPARAMS_H
#define __GMAN_SHADERS_GMANSHADERPARAMS_H 1

#include "gmancolor.h"
#include "gmandictionary.h"
#include "gmanerror.h"
#include "gmanparameterlist.h"

namespace gmanshaders {

// A GMANDictionary registers the same standard RI_* tokens (Ka, Kd, ...)
// in the same order on every construction (GMANDictionary::GMANDictionary,
// gmandictionary.cpp), so a plugin's own instance resolves them to the
// same GMANTokenId the renderer's own dictionary already baked into pl --
// this is what makes reading a shader's declared parameters back out of
// its GMANParameterList work without the plugin sharing the renderer's
// actual dictionary object, which it has no access to.
inline GMANDictionary &dictionary() {
  static GMANDictionary d;
  return d;
}

// Every shader parameter here is optional (RiSurface "matte" may pass none
// of them), so probing for one is routine, not exceptional.
// GMANParameterList::getPointer returns NULL for a token this parameter
// list doesn't carry. The try/catch is still load-bearing, for the other
// call: GMANDictionary::getTokenId throws RIE_BADTOKEN for a name the
// dictionary has never seen, which is what a shader asking for a parameter
// nobody ever declared does.
inline RtFloat *tryGetFloatParam(GMANParameterList &pl, RtToken token) {
  try {
    return (RtFloat *) pl.getPointer(dictionary().getTokenId(token));
  } catch (GMANError &) {
    return NULL;
  }
}

inline RtFloat getFloatParam(GMANParameterList &pl, RtToken token,
			      RtFloat def) {
  RtFloat *p = tryGetFloatParam(pl, token);
  return p ? p[0] : def;
}

inline GMANColor getColorParam(GMANParameterList &pl, RtToken token,
				const GMANColor &def) {
  RtFloat *p = tryGetFloatParam(pl, token);
  return p ? GMANColor(p[0], p[1], p[2]) : def;
}

}  // namespace gmanshaders

#endif
