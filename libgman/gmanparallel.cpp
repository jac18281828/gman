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

#include <exception>
#include <mutex>
#include <stop_token>
#include <thread>
#include <vector>

#include "gmanparallel.h"
#include "ri.h"

namespace gman {

RtInt parallelWorkers(RtInt count, RtInt workers) {
  if (count <= 0) {
    return 0;
  }

  RtInt result = workers;
  if (result <= 0) {
    unsigned hardware = std::thread::hardware_concurrency();
    result = hardware == 0 ? 1 : static_cast<RtInt>(hardware);
  }
  if (result > count) {
    result = count;
  }
  return result;
}

void parallelFor(RtInt count, const std::function<void(RtInt, RtInt)>& body, RtInt workers) {
  if (count <= 0) {
    return;
  }

  const RtInt numWorkers = parallelWorkers(count, workers);

  // One shared stop_source: any worker's exception stops every worker, not
  // only the one that threw.
  std::stop_source stopSource;
  std::mutex exceptionMutex;
  std::exception_ptr firstException;

  auto runWorker = [&](RtInt worker) {
    std::stop_token token = stopSource.get_token();
    for (RtInt i = worker; i < count; i += numWorkers) {
      if (token.stop_requested()) {
        break;
      }
      try {
        body(i, worker);
      } catch (...) {
        std::lock_guard<std::mutex> guard(exceptionMutex);
        if (!firstException) {
          firstException = std::current_exception();
        }
        stopSource.request_stop();
        break;
      }
    }
  };

  if (numWorkers <= 1) {
    runWorker(0);
  } else {
    std::vector<std::jthread> threads;
    threads.reserve(numWorkers - 1);
    for (RtInt w = 1; w < numWorkers; ++w) {
      threads.emplace_back([&runWorker, w] { runWorker(w); });
    }
    runWorker(0);
    for (auto& t : threads) {
      t.join();
    }
  }

  if (firstException) {
    std::rethrow_exception(firstException);
  }
}

} // namespace gman
