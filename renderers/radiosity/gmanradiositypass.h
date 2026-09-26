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

#include "gmanindirectpass.h"
#include "gmanradiositymesh.h"
#include "gmanradiositysolver.h"

/*
 * The ray tracer's own indirect-light pass, over a GMANRadiosityMesh and
 * one GMANRadiositySolver solve. Not GMAN_EXPORT, for the mesh's own
 * reason (gmanradiositymesh.h): compiled into gman_radiosity_objects and
 * linked into gman_radiosity's own module rather than an installed
 * library.
 *
 * prepare() dices world at the element size Option "radiosity" "float
 * elementsize" set, or, unset, the largest side of the box bounding every
 * finite-bounded primitive divided by kDefaultElementDivisions -- 0 with
 * no such primitive, in which case prepare() builds nothing and every
 * hit answers black. Each element's reflectance is gman::albedo at the
 * point the ray tracer would shade its receiver, or, failing that, its
 * first form-factor sample that hits, or a synthetic hit at the receiver
 * with u = v = 0. One GMANRadiositySolver::solve, at its own default
 * sample count, then answers every element side's H_ind. A GMANError
 * anywhere in this -- the build, an albedo or the solve -- logs one
 * warning naming the cause and leaves this pass answering black; the
 * mesh's own skipped-primitive count, its capped-element count and a
 * shot-capped solve each log a warning of their own but keep their
 * answer. A second prepare() replaces the first's.
 *
 * irradiance() locates hit's element and corner weights, picks the side
 * facing -I, and answers the weighted mean of that side's node H_ind --
 * black before prepare(), after a failed one, or where locate() finds
 * nothing. const and re-entrant: it reads only what prepare() built, and
 * holds no mutable member. Nothing here reads a clock, a random number or
 * an address order, so two renders of one scene answer byte-identical.
 */
class GMANRadiosityPass : public gman::IndirectPass {
public:
  void prepare(GMANWorldManager& world, GMANRayOccluder const& occluder, GMANOptions const& options) override;
  GMANColor irradiance(GMANHit const& hit, GMANVector const& I) const override;

private:
  GMANRadiosityMesh mesh;
  GMANRadiositySolution solution;
  bool ready = false;
};
