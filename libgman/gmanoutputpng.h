/* SPDX-License-Identifier: LGPL-2.1-or-later */

/* This is part of GMAN, a RenderMan-compatible renderer.
 *
 * Copyright (c) 2002, 2001, 2000, 1999 by John Cairns
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

#pragma once

#include <list>
#include <map>
#include <stack>
#include <string>
#include <vector>

#include "gmanlog.h"
#include "gmanoutput.h"
#include "ri.h"

namespace gman {

/*
 * RenderMan API gman::OutputPNG
 *
 */

class OutputPNG : public GMANOutput {
public:
  OutputPNG(const char* path, int width, int height); // default constructor

  ~OutputPNG(); // default destructor

protected:
  RtVoid writeImage(GMANOutput::DisplayMode mode, std::vector<GMANColor> const& image, RtFloat gamma) override;
};

} // namespace gman
