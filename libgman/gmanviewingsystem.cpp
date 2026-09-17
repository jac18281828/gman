/* SPDX-License-Identifier: LGPL-2.1-or-later */

/* This is part of GMAN, a RenderMan-compatible renderer.
 *
 * Copyright (c) 2001, 2000, 1999 by John Cairns
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

#include "gmanviewingsystem.h"

RtVoid GMANViewingSystem::screenToRaster(RtFloat& x, RtFloat& y) {
  x = xres * (x - sw.left) / (sw.right - sw.left);
  y = yres - yres * (y - sw.bottom) / (sw.top - sw.bottom);
}
RtVoid GMANViewingSystem::rasterToScreen(RtFloat& x, RtFloat& y) {
  x = sw.left + (sw.right - sw.left) * x / xres;
  y = sw.bottom + (sw.top - sw.bottom) * (yres - y) / yres;
}

GMANRay GMANViewingSystem::ray(RtFloat x, RtFloat y) {
  GMANRay cam = cameraRay(x, y);
  GMANPoint const origin = cam.getOrigin();
  GMANPoint const through = cam.pointAt(1.0);

  // p3m adds the translation row, so it transforms a point; carrying
  // origin and origin+direction through it as points and taking the
  // difference transforms the direction without picking up that
  // translation.
  RtFloat srcOrigin[] = {origin.getX(), origin.getY(), origin.getZ()};
  RtFloat srcThrough[] = {through.getX(), through.getY(), through.getZ()};
  RtFloat dstOrigin[3], dstThrough[3];
  GMANMatrix4 c2w = getCameraToWorld();
  c2w.p3m(1, srcOrigin, dstOrigin);
  c2w.p3m(1, srcThrough, dstThrough);

  GMANPoint const worldOrigin(dstOrigin[0], dstOrigin[1], dstOrigin[2]);
  GMANPoint const worldThrough(dstThrough[0], dstThrough[1], dstThrough[2]);
  return GMANRay(worldOrigin, GMANVector(worldOrigin, worldThrough), cam.getTMin(), cam.getTMax());
}
