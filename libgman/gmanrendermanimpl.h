/* SPDX-License-Identifier: LGPL-2.1-or-later */

/* This is part of GMAN, a RenderMan-compatible renderer.
 *
 * Copyright (c) 2001, 2000, 1999 John Cairns
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

#include <memory>

#include "gman/export.h"
#include "gmangraphicstate.h"
#include "gmanobjectmanager.h"
#include "gmanoutput.h"
#include "gmanrenderer.h"
#include "gmanrenderman.h"
#include "gmanviewingsystem.h"
#include "gmanworldmanager.h"
#include "ri.h"

/*
 * RenderMan API RiRenderMan
 *
 * The Renderman object supporting the primary interface from the
 * underlying rendering system to the RenderMan API
 *
 */

// renderman is an instance of the graphics state machine
class GMAN_EXPORT GMANRenderManImpl : public GMANRenderMan, protected GMANGraphicState {
private:
  // the renderer that computes the lighting model and
  // simulates the environment
  std::unique_ptr<GMANRenderer> renderer;

  // the object manager that stores the objects in the environment; borrowed
  // from renderer, valid exactly while renderer holds a value
  GMANObjectManager* objectManager;

  // world manager; borrowed from renderer like objectManager
  GMANWorldManager* worldManager;

  // Viewing system
  std::unique_ptr<GMANViewingSystem> viewingSystem;

  std::unique_ptr<GMANOutput> output;

  // true once the missing-renderer report has been raised for the current
  // RiBegin, so a failed load reports once
  bool noRendererReported = false;

  // frees the renderer and everything borrowed from or built for it
  RtVoid releaseRenderer();

  // raises RIE_NOTSTARTED the first time it is called while no renderer is
  // loaded, and returns quietly thereafter
  RtVoid reportNoRendererOnce();

  // drops the frame's output and leaves the world, as every way out of
  // RiWorldEnd does
  RtVoid abandonWorld();

public:
  GMANRenderManImpl();  // default constructor
  ~GMANRenderManImpl(); // default destructor

  /* Declare Shading Language variable */
  RtToken RiDeclare(const char* name, const char* declaration) override;

  /* RenderMan State machine */
  RtVoid RiBegin(RtToken name) override;
  RtVoid RiEnd(RtVoid) override;

  // begin single frame, number frame
  RtVoid RiFrameBegin(RtInt frame) override;
  RtVoid RiFrameEnd(RtVoid) override;

  // begin world (declaration of objects)
  RtVoid RiWorldBegin(RtVoid) override;
  RtVoid RiWorldEnd(RtVoid) override;

  // create a new object
  RtObjectHandle RiObjectBegin(RtVoid) override;

  // commit the most recently created object
  RtVoid RiObjectEnd(RtVoid) override;

  RtVoid RiObjectInstance(RtObjectHandle handle) override;

  RtVoid RiAttributeBegin(RtVoid) override;
  RtVoid RiAttributeEnd(RtVoid) override;

  RtVoid RiTransformBegin(RtVoid) override;
  RtVoid RiTransformEnd(RtVoid) override;

  RtVoid RiSolidBegin(RtToken operation) override;
  RtVoid RiSolidEnd(RtVoid) override;

  RtVoid RiMotionBeginV(RtInt n, RtFloat times[]) override;
  RtVoid RiMotionEnd(RtVoid) override;

  /* CAMERA OPTIONS */
  RtVoid RiFormat(RtInt xres, RtInt yres, RtFloat aspect) override;
  RtVoid RiFrameAspectRatio(RtFloat aspect) override;
  RtVoid RiScreenWindow(RtFloat left, RtFloat right, RtFloat bot, RtFloat top) override;
  RtVoid RiCropWindow(RtFloat xmin, RtFloat xmax, RtFloat ymin, RtFloat ymax) override;
  RtVoid RiProjectionV(RtToken name, RtInt n, RtToken tokens[], RtPointer parms[]) override;
  RtVoid RiClipping(RtFloat hither, RtFloat yon) override;
  RtVoid RiDepthOfField(RtFloat fstop, RtFloat focallength, RtFloat focaldistance) override;
  RtVoid RiShutter(RtFloat min, RtFloat max) override;

  /* DISPLAY OPTIONS */
  RtVoid RiPixelVariance(RtFloat variation) override;
  RtVoid RiPixelSamples(RtFloat xsamples, RtFloat ysamples) override;
  RtVoid RiPixelFilter(RtFilterFunc filterfunc, RtFloat xwidth, RtFloat ywidth) override;
  RtVoid RiExposure(RtFloat gain, RtFloat gamma) override;
  RtVoid RiImagerV(RtToken name, RtInt n, RtToken tokens[], RtPointer parms[]) override;
  RtVoid RiQuantize(RtToken type, RtInt one, RtInt min, RtInt max, RtFloat ampl) override;
  RtVoid RiDisplayV(char* name, RtToken type, RtToken mode, RtInt n, RtToken tokens[], RtPointer parms[]) override;

  /* ADDITIONAL OPTIONS */
  RtVoid RiHiderV(RtToken type, RtInt n, RtToken tokens[], RtPointer parms[]) override;
  RtVoid RiColorSamples(RtInt n, RtFloat nRGB[], RtFloat RGBn[]) override;
  RtInt colorSampleCount();
  RtVoid RiRelativeDetail(RtFloat relativedetail) override;
  RtVoid RiOptionV(RtToken name, RtInt n, RtToken tokens[], RtPointer parms[]) override;

  /* SHADING ATTRIBUTES */
  RtVoid RiColor(RtColor color) override;
  RtVoid RiOpacity(RtColor color) override;
  RtVoid RiTextureCoordinates(RtFloat s1, RtFloat t1, RtFloat s2, RtFloat t2, RtFloat s3, RtFloat t3, RtFloat s4,
                              RtFloat t4) override;
  RtLightHandle RiLightSourceV(RtToken name, RtInt n, RtToken tokens[], RtPointer parms[]) override;
  RtLightHandle RiAreaLightSourceV(RtToken name, RtInt n, RtToken tokens[], RtPointer parms[]) override;
  RtVoid RiIlluminate(RtLightHandle light, RtBoolean onoff) override;
  RtVoid RiSurfaceV(RtToken name, RtInt n, RtToken tokens[], RtPointer parms[]) override;
  RtVoid RiAtmosphereV(RtToken name, RtInt n, RtToken tokens[], RtPointer parms[]) override;
  RtVoid RiInteriorV(RtToken name, RtInt n, RtToken tokens[], RtPointer parms[]) override;
  RtVoid RiExteriorV(RtToken name, RtInt n, RtToken tokens[], RtPointer parms[]) override;
  RtVoid RiShadingRate(RtFloat size) override;
  RtVoid RiShadingInterpolation(RtToken type) override;
  RtVoid RiMatte(RtBoolean onoff) override;

  /* GEOMETRY ATTRIBUTES */
  RtVoid RiBound(RtBound) override;
  RtVoid RiDetail(RtBound) override;
  RtVoid RiDetailRange(RtFloat minvis, RtFloat lowtran, RtFloat uptran, RtFloat maxvis) override;
  RtVoid RiGeometricApproximation(RtToken type, RtFloat value) override;
  RtVoid RiBasis(RtBasis ubasis, RtInt ustep, RtBasis vbasis, RtInt vstep) override;
  RtVoid RiTrimCurve(RtInt nloops, RtInt ncurves[], RtInt order[], RtFloat knot[], RtFloat min[], RtFloat max[],
                     RtInt n[], RtFloat u[], RtFloat v[], RtFloat w[]) override;
  RtVoid RiOrientation(RtToken orientation) override;
  RtVoid RiReverseOrientation(RtVoid) override;
  RtVoid RiSides(RtInt sides) override;
  RtVoid RiDisplacementV(RtToken name, RtInt n, RtToken tokens[], RtPointer parms[]) override;

  /* TRANSFORMATIONS */
  RtVoid RiIdentity(RtVoid) override;
  RtVoid RiTransform(RtMatrix transform) override;
  RtVoid RiConcatTransform(RtMatrix transform) override;
  RtVoid RiPerspective(RtFloat fov) override;
  RtVoid RiTranslate(RtFloat dx, RtFloat dy, RtFloat dz) override;
  RtVoid RiRotate(RtFloat angle, RtFloat dx, RtFloat dy, RtFloat dz) override;
  RtVoid RiScale(RtFloat sx, RtFloat sy, RtFloat sz) override;
  RtVoid RiSkew(RtFloat angle, RtFloat dx1, RtFloat dy1, RtFloat dz1, RtFloat dx2, RtFloat dy2, RtFloat dz2) override;
  RtVoid RiDeformationV(RtToken name, RtInt n, RtToken tokens[], RtPointer parms[]) override;
  RtVoid RiCoordinateSystem(RtToken space) override;
  RtVoid RiCoordSysTransform(RtToken space) override;

  RtPoint* RiTransformPoints(RtToken fromspace, RtToken tospace, RtInt n, RtPoint points[]) override;

  RtVoid RiAttributeV(RtToken name, RtInt n, RtToken tokens[], RtPointer parms[]) override;

  /* PRIMITIVES */
  RtVoid RiPolygonV(RtInt nverts, RtInt n, RtToken tokens[], RtPointer parms[]) override;
  RtVoid RiGeneralPolygonV(RtInt nloops, RtInt nverts[], RtInt n, RtToken tokens[], RtPointer parms[]) override;
  RtVoid RiPointsPolygonsV(RtInt npolys, RtInt nverts[], RtInt verts[], RtInt n, RtToken tokens[],
                           RtPointer parms[]) override;
  RtVoid RiPointsGeneralPolygonsV(RtInt npolys, RtInt nloops[], RtInt nverts[], RtInt verts[], RtInt n,
                                  RtToken tokens[], RtPointer parms[]) override;
  RtVoid RiPatchV(RtToken type, RtInt n, RtToken tokens[], RtPointer parms[]) override;
  RtVoid RiPatchMeshV(RtToken type, RtInt nu, RtToken uwrap, RtInt nv, RtToken vwrap, RtInt n, RtToken tokens[],
                      RtPointer parms[]) override;

  // Beside the two RI-mandated overloads above, not reachable through them:
  // the RIB-parsed path calls these directly, with its own supplied-length
  // count per array, so a short "P" clamps instead of reading past its
  // allocation. The public overloads forward NULL, trusting the caller.
  RtVoid RiPolygonV(RtInt nverts, RtInt n, RtToken tokens[], RtPointer parms[], RtInt const* counts);
  RtVoid RiPatchV(RtToken type, RtInt n, RtToken tokens[], RtPointer parms[], const RtInt* counts);
  RtVoid RiPatchMeshV(RtToken type, RtInt nu, RtToken uwrap, RtInt nv, RtToken vwrap, RtInt n, RtToken tokens[],
                      RtPointer parms[], const RtInt* counts);
  RtVoid RiGeneralPolygonV(RtInt nloops, RtInt nverts[], RtInt n, RtToken tokens[], RtPointer parms[],
                           const RtInt* counts);
  RtVoid RiPointsPolygonsV(RtInt npolys, RtInt nverts[], RtInt verts[], RtInt n, RtToken tokens[], RtPointer parms[],
                           RtInt const* counts);
  RtVoid RiPointsGeneralPolygonsV(RtInt npolys, RtInt nloops[], RtInt nverts[], RtInt verts[], RtInt n,
                                  RtToken tokens[], RtPointer parms[], RtInt const* counts);
  RtVoid RiNuPatchV(RtInt nu, RtInt uorder, RtFloat uknot[], RtFloat umin, RtFloat umax, RtInt nv, RtInt vorder,
                    RtFloat vknot[], RtFloat vmin, RtFloat vmax, RtInt n, RtToken tokens[], RtPointer parms[]) override;
  RtVoid RiNuPatchV(RtInt nu, RtInt uorder, RtFloat uknot[], RtFloat umin, RtFloat umax, RtInt nv, RtInt vorder,
                    RtFloat vknot[], RtFloat vmin, RtFloat vmax, RtInt n, RtToken tokens[], RtPointer parms[],
                    RtInt const* counts);

  RtVoid RiSphereV(RtFloat radius, RtFloat zmin, RtFloat zmax, RtFloat tmax, RtInt n, RtToken tokens[],
                   RtPointer parms[]) override;
  RtVoid RiConeV(RtFloat height, RtFloat radius, RtFloat tmax, RtInt n, RtToken tokens[], RtPointer parms[]) override;
  RtVoid RiCylinderV(RtFloat radius, RtFloat zmin, RtFloat zmax, RtFloat tmax, RtInt n, RtToken tokens[],
                     RtPointer parms[]) override;
  RtVoid RiHyperboloidV(RtPoint point1, RtPoint point2, RtFloat tmax, RtInt n, RtToken tokens[],
                        RtPointer parms[]) override;
  RtVoid RiParaboloidV(RtFloat rmax, RtFloat zmin, RtFloat zmax, RtFloat tmax, RtInt n, RtToken tokens[],
                       RtPointer parms[]) override;
  RtVoid RiDiskV(RtFloat height, RtFloat radius, RtFloat tmax, RtInt n, RtToken tokens[], RtPointer parms[]) override;
  RtVoid RiTorusV(RtFloat majrad, RtFloat minrad, RtFloat phimin, RtFloat phimax, RtFloat tmax, RtInt n,
                  RtToken tokens[], RtPointer parms[]) override;

  // Counts-aware, mirroring RiPatchV and the polygon requests above -- see
  // their comment.
  RtVoid RiSphereV(RtFloat radius, RtFloat zmin, RtFloat zmax, RtFloat tmax, RtInt n, RtToken tokens[],
                   RtPointer parms[], const RtInt* counts);
  RtVoid RiConeV(RtFloat height, RtFloat radius, RtFloat tmax, RtInt n, RtToken tokens[], RtPointer parms[],
                 RtInt const* counts);
  RtVoid RiCylinderV(RtFloat radius, RtFloat zmin, RtFloat zmax, RtFloat tmax, RtInt n, RtToken tokens[],
                     RtPointer parms[], const RtInt* counts);
  RtVoid RiHyperboloidV(RtPoint point1, RtPoint point2, RtFloat tmax, RtInt n, RtToken tokens[], RtPointer parms[],
                        RtInt const* counts);
  RtVoid RiParaboloidV(RtFloat rmax, RtFloat zmin, RtFloat zmax, RtFloat tmax, RtInt n, RtToken tokens[],
                       RtPointer parms[], const RtInt* counts);
  RtVoid RiDiskV(RtFloat height, RtFloat radius, RtFloat tmax, RtInt n, RtToken tokens[], RtPointer parms[],
                 RtInt const* counts);
  RtVoid RiTorusV(RtFloat majrad, RtFloat minrad, RtFloat phimin, RtFloat phimax, RtFloat tmax, RtInt n,
                  RtToken tokens[], RtPointer parms[], RtInt const* counts);

  RtVoid RiBlobbyV(RtInt nleaf, RtInt ncode, RtInt code[], RtInt nflt, RtFloat flt[], RtInt nstr, RtToken str[],
                   RtInt n, RtToken tokens[], RtPointer parms[]) override;
  RtVoid RiPointsV(RtInt npoints, RtInt n, RtToken tokens[], RtPointer parms[]) override;
  RtVoid RiCurvesV(RtToken type, RtInt ncurves, RtInt nvertices[], RtToken wrap, RtInt n, RtToken tokens[],
                   RtPointer parms[]) override;
  RtVoid RiSubdivisionMeshV(RtToken mask, RtInt nf, RtInt nverts[], RtInt verts[], RtInt ntags, RtToken tags[],
                            RtInt numargs[], RtInt intargs[], RtFloat floatargs[], RtInt n, RtToken tokens[],
                            RtPointer parms[]) override;

  RtVoid RiProcedural(RtPointer data, RtBound bound, RtVoid (*subdivfunc)(RtPointer, RtFloat),
                      RtVoid (*freefunc)(RtPointer)) override;
  RtVoid RiGeometryV(RtToken type, RtInt n, RtToken tokens[], RtPointer parms[]) override;

  /* MISC */
  RtVoid RiMakeTextureV(char* pic, char* tex, RtToken swrap, RtToken twrap, RtFilterFunc filterfunc, RtFloat swidth,
                        RtFloat twidth, RtInt n, RtToken tokens[], RtPointer parms[]) override;
  /* ERROR HANDLER */
  RtVoid RiErrorHandler(RtErrorHandler handler) override;

  RtVoid RiMakeBumpV(char* pic, char* tex, RtToken swrap, RtToken twrap, RtFilterFunc filterfunc, RtFloat swidth,
                     RtFloat twidth, RtInt n, RtToken tokens[], RtPointer parms[]) override;
  RtVoid RiMakeLatLongEnvironmentV(char* pic, char* tex, RtFilterFunc filterfunc, RtFloat swidth, RtFloat twidth,
                                   RtInt n, RtToken tokens[], RtPointer parms[]) override;
  RtVoid RiMakeCubeFaceEnvironmentV(char* px, char* nx, char* py, char* ny, char* pz, char* nz, char* tex, RtFloat fov,
                                    RtFilterFunc filterfunc, RtFloat swidth, RtFloat ywidth, RtInt n, RtToken tokens[],
                                    RtPointer parms[]) override;
  RtVoid RiMakeShadowV(char* pic, char* tex, RtInt n, RtToken tokens[], RtPointer parms[]) override;
};
