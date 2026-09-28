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

#include <cstddef>
#include <list>
#include <map>
#include <memory>
#include <stack>
#include <string>

#include "gmanlinearworldmanager.h"
#include "gmanlog.h"
#include "gmanobjectmanager.h"
#include "gmanpatchpolyobjectmanager.h"
#include "gmanpolygonclipper.h"
#include "gmanrenderer.h"
#include "gmansamplebuffer.h"
#include "ri.h"

/*
 * RenderMan API GMANZBufferRenderer
 *
 * A ZBuffer renderer.
 *
 */

class GMAN_EXPORT GMANZBufferRenderer : public GMANRenderer {
private:
  // private types

  struct Point // integer screen position
  {
    int x, y;
  };

  struct VertexInfo // vertex information
  {

    Point screen;    // screen coord
    GMANPoint posn;  // scaled position
    GMANColor color; // vertex color
    GMANAlpha alpha; // vertex alpha
  };

  struct ScanInfo // a scan line intersection
  {
    RtFloat x;       //  x coord
    RtFloat z;       //  pseudo-depth
    GMANColor color; //  color
    GMANAlpha alpha; //  alpha
  };

  struct EdgeInfo {
    // How many of isect[] this scanline actually received, not merely
    // whether it received any. Edges are scan-converted over a half-open
    // y range, so the bottom scanline of every polygon -- y == ymax --
    // gets none at all; drawEdgeList needs both entries to have a span,
    // and read them unconditionally before this was a count.
    int nisect;

    ScanInfo isect[2]; // scan line intersection array
  };

  // private data
  RtFloat* zbuffer; // the depth buffer.

  // private methods

  int ymin; // minimum y-axis coordinate, in samples
  int ymax; // maximum y-axis coordinate, in samples

  int width;  // display width, in pixels
  int height; // display height, in pixels

  // PixelSamples, and the sample-resolution grid they imply
  // (width*xsamples, height*ysamples) -- getVertexInfo, scanEdges and
  // drawEdgeList all rasterize at this resolution; zbuffer/getDepth below
  // stay at pixel resolution regardless.
  int xsamples;
  int ysamples;
  int sampleWidth;
  int sampleHeight;

  // The real per-sample visibility test and colour store; resolved into
  // frameBuffer, and into zbuffer below, at the end of render().
  std::unique_ptr<GMANSampleBuffer> sampleBuffer;

  // The frame buffer holds only the CropWindow rectangle, but
  // GMANViewingSystem::screenToRaster (built from the full, uncropped
  // Format) always returns a position in the *full* raster grid.
  // getVertexInfo subtracts these -- the crop rectangle's own origin, from
  // GMANOptions::RasterInfo -- to land in this buffer's local space; every
  // downstream user of vert->screen/posn (scanEdges, drawEdgeList) already
  // assumes that offset has been applied.
  int rasterOriginX;
  int rasterOriginY;

  // The full (uncropped) Format's resolution, for getVertexInfo's
  // numerical-stability margin below -- that margin has to stay a
  // property of the frame's geometry, not of how large a CropWindow the
  // caller happened to ask for, or the same scene's near-singular
  // vertices pass in one crop and fail in another.
  int fullResX;
  int fullResY;

  int num_vert; // number of vertices

  EdgeInfo* edge_list;

  // Sutherland-Hodgman clipping a convex n-gon (faces are always
  // GMAN_NFACE_VERTS==4) against a convex region of k half-planes (the
  // clipper's 6) produces at most n+k vertices -- 10 here. Sized well
  // past that; getVertexInfo() still clamps defensively.
  static constexpr int kMaxClippedVerts = 16;
  VertexInfo v_info[kMaxClippedVerts];

  // a polygon clipper
  GMANPolygonClipper clipper;

  // the viewing system for the frame currently being rendered, needed by
  // getVertexInfo() to map already-projected NDC coordinates to raster
  // space via GMANViewingSystem::screenToRaster.
  GMANViewingSystem* viewingSys;

  // GMANObject, this object manager's own tessellated primitive, carries
  // no Appearance -- unlike a ray primitive, it has nowhere to hold one
  // past its own declaring RiXxxV call, where attr is always the current
  // GMANAttributes (gmanrendermanimpl.cpp's RiSphereV, RiDiskV and every
  // other geometry request all pass &getAttributes(), never null).
  // Counting a pending area light here, before tessellation discards it,
  // is this renderer's only chance to see one, so every geometry request
  // that can carry one is overridden; GMANPatchPolyObjectManager's own
  // getRSBlobby/getRSPoints/getRSCurves/getRSSubdivisionMesh already
  // answer an empty placeholder, rendering no geometry to count.
  class AreaLightCountingObjectManager : public GMANPatchPolyObjectManager {
  public:
    GMANPrimitive* getRSPolygon(RtInt nverts, GMANParameterList pl, GMANOptions* opt, GMANAttributes* attr,
                                GMANTransform* t) override;
    GMANPrimitive* getRSGeneralPolygon(RtInt nloops, RtInt nverts[], GMANParameterList pl, GMANOptions* opt,
                                       GMANAttributes* attr, GMANTransform* t) override;
    GMANPrimitive* getRSPointsPolygon(RtInt npolys, RtInt nverts[], RtInt verts[], GMANParameterList pl,
                                      GMANOptions* opt, GMANAttributes* attr, GMANTransform* t) override;
    GMANPrimitive* getRSPointsGeneralPolygons(RtInt npolys, RtInt nloops[], RtInt nverts[], RtInt verts[],
                                              GMANParameterList pl, GMANOptions* opt, GMANAttributes* attr,
                                              GMANTransform* t) override;
    GMANPrimitive* getRSPatch(RtToken type, GMANParameterList pl, GMANOptions* opt, GMANAttributes* attr,
                              GMANTransform* t) override;
    GMANPrimitive* getRSPatchMesh(RtToken type, RtInt nu, RtToken uwrap, RtInt nv, RtToken vwrap, GMANParameterList pl,
                                  GMANOptions* opt, GMANAttributes* attr, GMANTransform* t) override;
    GMANPrimitive* getRSNuPatch(RtInt nu, RtInt uorder, RtFloat uknot[], RtFloat umin, RtFloat umax, RtInt nv,
                                RtInt vorder, RtFloat vknot[], RtFloat vmin, RtFloat vmax, GMANParameterList pl,
                                GMANOptions* opt, GMANAttributes* attr, GMANTransform* t) override;
    GMANPrimitive* getRSSphere(RtFloat radius, RtFloat zmin, RtFloat zmax, RtFloat tmax, GMANParameterList pl,
                               GMANOptions* opt, GMANAttributes* attr, GMANTransform* t) override;
    GMANPrimitive* getRSCone(RtFloat height, RtFloat radius, RtFloat tmax, GMANParameterList pl, GMANOptions* opt,
                             GMANAttributes* attr, GMANTransform* t) override;
    GMANPrimitive* getRSCylinder(RtFloat radius, RtFloat zmin, RtFloat zmax, RtFloat tmax, GMANParameterList pl,
                                 GMANOptions* opt, GMANAttributes* attr, GMANTransform* t) override;
    GMANPrimitive* getRSHyperboloid(RtPoint point1, RtPoint point2, RtFloat tmax, GMANParameterList pl,
                                    GMANOptions* opt, GMANAttributes* attr, GMANTransform* t) override;
    GMANPrimitive* getRSParaboloid(RtFloat rmax, RtFloat zmin, RtFloat zmax, RtFloat tmax, GMANParameterList pl,
                                   GMANOptions* opt, GMANAttributes* attr, GMANTransform* t) override;
    GMANPrimitive* getRSDisk(RtFloat height, RtFloat radius, RtFloat tmax, GMANParameterList pl, GMANOptions* opt,
                             GMANAttributes* attr, GMANTransform* t) override;
    GMANPrimitive* getRSTorus(RtFloat majrad, RtFloat minrad, RtFloat phimin, RtFloat phimax, RtFloat tmax,
                              GMANParameterList pl, GMANOptions* opt, GMANAttributes* attr, GMANTransform* t) override;

    std::size_t areaLightCount = 0;

  private:
    // Counts once when attr's own appearance carries a pending, illuminated
    // area light -- every override above calls this before tessellating.
    void countIfAreaLight(GMANAttributes* attr);
  };

  AreaLightCountingObjectManager objectManager;

  GMANLinearWorldManager worldManager;

  // private methods

  inline void setDepth(int x, int y, RtFloat f) { zbuffer[y * width + x] = f; }

  bool initZBuffer(void);

  RtVoid setZBuffer(RtInt x, RtInt y, RtFloat val);

  RtFloat getZBuffer(RtInt x, RtInt y) const;

  // populate vertex info array. Returns false (and leaves num_vert/edge
  // state untouched) if any vertex's raster position is non-finite or
  // wildly out of frame -- near-zero-distance geometry (e.g. an
  // unclipped point arbitrarily close to the near plane) can still
  // produce a finite but enormous NDC coordinate after perspective
  // divide, and scanEdges/drawEdgeList index into fixed-size arrays with
  // it unchecked.
  bool getVertexInfo(GMANOutputPolygon& out);
  // edge scanning alg
  void scanEdges(void);
  // draw each of the scanned edges, sample-testing and storing into
  // sampleBuffer
  void drawEdgeList(void);

  // render each outpolygon
  void render(GMANOutputPolygon& out);

public:
  GMANZBufferRenderer(int w, int h);

  GMANZBufferRenderer(); // default constructor

  ~GMANZBufferRenderer(); // default destructor

  RtVoid illuminance(RtInt /*i*/, GMANPoint const& /*p*/, GMANVector const& /*axis*/, RtFloat /*angle*/) {}
  RtVoid illuminate(RtInt /*i*/, GMANPoint const& /*p*/, GMANVector const& /*axis*/, RtFloat /*angle*/) {}
  RtVoid solar(RtInt /*i*/, GMANVector const& /*axis*/, RtFloat /*angle*/) {}

  inline RtFloat getDepth(int x, int y) const { return zbuffer[y * width + x]; }

  /*
   * Apply a zbuffer environment to the objects in object manager
   * to generate a frameBuffer output.
   */
  virtual RtVoid render(GMANFrameBuffer* frameBuffer, GMANViewingSystem* viewingSys, const GMANOptions& options,
                        const GMANAttributes& attributes);

  void setHeight(int h) { height = h; };

  int getHeight(void) { return height; };

  void setWidth(int w) { width = w; };

  int getWidth(void) { return width; };

  // return its world manager
  virtual GMANWorldManager* getWorldManager(void);

  // return its object manager
  virtual GMANObjectManager* getObjectManager(void);
};
