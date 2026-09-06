/* SPDX-License-Identifier: LGPL-2.1-or-later */

/*---------------------------------------------------------
  Copyright (C) Lionel Joseph Lacour 2000, 2001
  2000/07/30  First release
  ---------------------------------------------------------
  GMAN Implementation Specific Options & Attributes
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

#ifndef __GMANISOA_H
#define __GMANISOA_H 1

#include <string>
#include "ri.h"
#include "gmanlog.h"

class GMAN_EXPORT GMANISO
{
  /* searchpath */
  std::string archive;
  std::string texture;
  std::string shader;
  std::string procedural;
public:
  RtVoid set (RtToken name, int n, RtToken *tk, RtPointer *dt);
  const std::string &getArchivePath() const { return archive; }
  const std::string &getTexturePath() const { return texture; }
  const std::string &getShaderPath() const { return shader; }
  const std::string &getProceduralPath() const { return procedural; }
};

class GMANISA
{
private:
public:
  GMANISA ();
  ~GMANISA ();
};

#endif

