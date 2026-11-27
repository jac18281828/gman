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

#pragma once

#include "gman/rayexport.h"
#include "gmanattributes.h"
#include "gmanobjectmanager.h"
#include "gmanoptions.h"
#include "gmanowned.h"
#include "gmanparameterlist.h"
#include "gmanprimitive.h"
#include "gmantransform.h"
#include "ri.h"

/*
 * RenderMan API GMANPatchPolyObjectManager
 *
 */

class GMANRAY_EXPORT GMANRayObjectManager : public GMANObjectManager {
public:
  GMANRayObjectManager(); // default constructor

  ~GMANRayObjectManager(); // default destructor

  /*
   * Renderer Specific primitives
   *
   * Each must provide its own object manager, and each
   * object manager must provide its own primitives.
   *
   */
  virtual gman::Owned<GMANPrimitive> getRSPolygonMesh(GMANPolygonMesh const& mesh, GMANOptions* opt,
                                                      GMANAttributes* attr, GMANTransform* t);

  virtual gman::Owned<GMANPrimitive> getRSPatch(RtToken type, GMANParameterList pl, GMANOptions* opt,
                                                GMANAttributes* attr, GMANTransform* t);

  virtual gman::Owned<GMANPrimitive> getRSPatchMesh(RtToken type, RtInt nu, RtToken uwrap, RtInt nv, RtToken vwrap,
                                                    GMANParameterList pl, GMANOptions* opt, GMANAttributes* attr,
                                                    GMANTransform* t);

  virtual gman::Owned<GMANPrimitive> getRSNuPatch(RtInt nu, RtInt uorder, RtFloat uknot[], RtFloat umin, RtFloat umax,
                                                  RtInt nv, RtInt vorder, RtFloat vknot[], RtFloat vmin, RtFloat vmax,
                                                  GMANParameterList pl, GMANOptions* opt, GMANAttributes* attr,
                                                  GMANTransform* t);

  virtual gman::Owned<GMANPrimitive> getRSSphere(RtFloat radius, RtFloat zmin, RtFloat zmax, RtFloat tmax,
                                                 GMANParameterList pl, GMANOptions* opt, GMANAttributes* attr,
                                                 GMANTransform* t);

  virtual gman::Owned<GMANPrimitive> getRSCone(RtFloat height, RtFloat radius, RtFloat tmax, GMANParameterList pl,
                                               GMANOptions* opt, GMANAttributes* attr, GMANTransform* t);

  virtual gman::Owned<GMANPrimitive> getRSCylinder(RtFloat radius, RtFloat zmin, RtFloat zmax, RtFloat tmax,
                                                   GMANParameterList pl, GMANOptions* opt, GMANAttributes* attr,
                                                   GMANTransform* t);

  virtual gman::Owned<GMANPrimitive> getRSHyperboloid(RtPoint point1, RtPoint point2, RtFloat tmax,
                                                      GMANParameterList pl, GMANOptions* opt, GMANAttributes* attr,
                                                      GMANTransform* t);

  virtual gman::Owned<GMANPrimitive> getRSParaboloid(RtFloat rmax, RtFloat zmin, RtFloat zmax, RtFloat tmax,
                                                     GMANParameterList pl, GMANOptions* opt, GMANAttributes* attr,
                                                     GMANTransform* t);

  virtual gman::Owned<GMANPrimitive> getRSDisk(RtFloat height, RtFloat radius, RtFloat tmax, GMANParameterList pl,
                                               GMANOptions* opt, GMANAttributes* attr, GMANTransform* t);

  virtual gman::Owned<GMANPrimitive> getRSTorus(RtFloat majrad, RtFloat minrad, RtFloat phimin, RtFloat phimax,
                                                RtFloat tmax, GMANParameterList pl, GMANOptions* opt,
                                                GMANAttributes* attr, GMANTransform* t);

  virtual gman::Owned<GMANPrimitive> getRSBlobby(RtInt nleaf, RtInt ncode, RtInt code[], RtInt nflt, RtFloat flt[],
                                                 RtInt nstr, RtToken str[], GMANParameterList pl, GMANOptions* opt,
                                                 GMANAttributes* attr, GMANTransform* t);

  virtual gman::Owned<GMANPrimitive> getRSPoints(RtInt npoints, GMANParameterList pl, GMANOptions* opt,
                                                 GMANAttributes* attr, GMANTransform* t);

  virtual gman::Owned<GMANPrimitive> getRSCurves(RtToken type, RtInt ncurves, RtInt nvertices[], RtToken wrap,
                                                 GMANParameterList pl, GMANOptions* opt, GMANAttributes* attr,
                                                 GMANTransform* t);

  virtual gman::Owned<GMANPrimitive> getRSSubdivisionMesh(RtToken mask, RtInt nf, RtInt nverts[], RtInt verts[],
                                                          RtInt ntags, RtToken tags[], RtInt numargs[], RtInt intargs[],
                                                          RtFloat floatargs[], GMANParameterList pl, GMANOptions* opt,
                                                          GMANAttributes* attr, GMANTransform* t);
};
