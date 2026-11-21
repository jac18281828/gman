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

#include <functional>

#include "gman/export.h"
#include "ri.h"

namespace gman {

/*
 * GMAN parallelism
 *
 * One seam: parallelFor runs a body once per index, spread over a
 * bounded set of workers, and returns only once every body has returned.
 * Each worker takes the next unclaimed index when it finishes its last,
 * so a slow body delays only itself: the other workers carry on with the
 * remaining indices. A worker may therefore run no index, or most of
 * them. A caller partitions its own state by worker index -- each
 * worker's indices run on one thread, one at a time, so state a worker
 * owns outright needs no lock. Anything a body reads that it does not own
 * outright must already be read-only for the call; logging is the one
 * sanctioned exception. parallelWorkers reports the worker count a
 * call with the same arguments would use, so a caller sizes its
 * per-worker state by it before making the call; state no worker
 * touched stays as the caller left it.
 *
 * A body that throws requests every worker to stop before starting
 * another index; once every worker has returned, the first exception
 * caught is rethrown on the calling thread.
 */

// The worker count a parallelFor call with the same arguments uses.
// workers <= 0 means the hardware's reported concurrency, floored at 1 if
// that reports 0; the result never exceeds count. count <= 0 returns 0.
GMAN_EXPORT RtInt parallelWorkers(RtInt count, RtInt workers = 0);

// Runs body(index, worker) exactly once for each index in [0, count), in
// unspecified order. worker lies in [0, parallelWorkers(count,
// workers)); every body sharing a worker index runs on the same thread,
// one at a time. count <= 0 calls nothing. When parallelWorkers
// reports one worker, every body runs inline on the calling thread as
// worker 0 and no thread is created.
GMAN_EXPORT void parallelFor(RtInt count, std::function<void(RtInt index, RtInt worker)> const& body,
                             RtInt workers = 0);

} // namespace gman
