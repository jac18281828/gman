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

#include "gmandefaults.h"
#include "gmanoutput.h"
#include "gmanoutputx11.h"
#include "ri.h"

namespace gman {

/*
 * RenderMan API gman::OutputX11
 *
 */

// default constructor
OutputX11::OutputX11(const char* /*name*/, int /*width*/, int /*height*/) : WindowOutput() {};

// default destructor
OutputX11::~OutputX11() {};

RtVoid OutputX11::save(GMANOutput::DisplayMode /*mode*/, RtFloat /*gain*/, RtFloat /*gamma*/) {
  debug("sorry framebuffer display is not currently supported");
}

} // namespace gman
