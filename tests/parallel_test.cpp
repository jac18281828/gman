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

#include <atomic>
#include <chrono>
#include <cstddef>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "check.h"
#include "gmanerror.h"
#include "gmanparallel.h"

namespace {

// One run's bookkeeping: which index landed in which worker's slot, and
// which thread ran each worker. Each vector entry belongs to exactly one
// worker for the whole call, so writing it from inside the body needs no
// lock -- that is the invariant gmanParallelFor exists to make true.
struct RunRecord {
  std::vector<int> indexHits;
  std::vector<int> perWorkerCount;
  std::vector<std::set<std::thread::id>> perWorkerThreadIds;
};

RunRecord runRecording(RtInt count, RtInt workers) {
  RtInt numWorkers = gmanParallelWorkers(count, workers);
  RunRecord record;
  record.indexHits.assign(static_cast<std::size_t>(count), 0);
  record.perWorkerCount.assign(static_cast<std::size_t>(numWorkers), 0);
  record.perWorkerThreadIds.resize(static_cast<std::size_t>(numWorkers));

  gmanParallelFor(
      count,
      [&](RtInt index, RtInt worker) {
        record.indexHits[static_cast<std::size_t>(index)]++;
        record.perWorkerCount[static_cast<std::size_t>(worker)]++;
        record.perWorkerThreadIds[static_cast<std::size_t>(worker)].insert(
            std::this_thread::get_id());
      },
      workers);

  return record;
}

// Every index in [0, count) hit exactly once, for every count/workers
// combination the contract names.
void testExactlyOnce() {
  const std::vector<RtInt> counts = {0, 1, 7, 1000};
  const std::vector<RtInt> workerCounts = {1, 2, 8, 0};

  for (RtInt count : counts) {
    for (RtInt workers : workerCounts) {
      RunRecord record = runRecording(count, workers);
      bool allOnce = true;
      for (int hits : record.indexHits) {
        if (hits != 1) {
          allOnce = false;
          break;
        }
      }
      check(allOnce, "count=" + std::to_string(count) +
                         " workers=" + std::to_string(workers) +
                         ": every index ran exactly once");
    }
  }
}

// workers=1 forces the serial path: no thread created, everything runs on
// the calling thread as worker 0.
void testSerialPath() {
  const std::thread::id callingThread = std::this_thread::get_id();
  RunRecord record = runRecording(100, 1);

  check(gmanParallelWorkers(100, 1) == 1,
        "gmanParallelWorkers(100, 1) reports one worker");
  check(record.perWorkerThreadIds.size() == 1 &&
            record.perWorkerThreadIds[0].size() == 1 &&
            *record.perWorkerThreadIds[0].begin() == callingThread,
        "workers=1 runs every body on the calling thread as worker 0");
}

// gmanParallelWorkers reports the count the call actually used; every
// worker index stays below it; a worker's indices all ran on one thread;
// per-worker counts sum to count.
void testWorkerIndices() {
  const std::vector<RtInt> counts = {0, 1, 7, 1000};
  const std::vector<RtInt> workerCounts = {1, 2, 8, 0};

  for (RtInt count : counts) {
    for (RtInt workers : workerCounts) {
      RtInt numWorkers = gmanParallelWorkers(count, workers);
      RunRecord record = runRecording(count, workers);

      check(static_cast<RtInt>(record.perWorkerCount.size()) == numWorkers,
            "count=" + std::to_string(count) +
                " workers=" + std::to_string(workers) +
                ": worker index stays below gmanParallelWorkers' report");

      int total = 0;
      bool oneThreadPerWorker = true;
      for (std::size_t w = 0; w < record.perWorkerThreadIds.size(); ++w) {
        total += record.perWorkerCount[w];
        if (record.perWorkerCount[w] > 0 &&
            record.perWorkerThreadIds[w].size() != 1) {
          oneThreadPerWorker = false;
        }
      }
      check(oneThreadPerWorker,
            "count=" + std::to_string(count) +
                " workers=" + std::to_string(workers) +
                ": bodies sharing a worker index ran on one thread");
      check(total == count, "count=" + std::to_string(count) +
                                " workers=" + std::to_string(workers) +
                                ": per-worker counts sum to count");
    }
  }
}

// A body that throws stops every worker from starting another index;
// after every worker returns, the exception is rethrown on the calling
// thread and the process survives.
void testExceptionsStopTheCall() {
  const RtInt count = 400;
  const RtInt workers = 4;
  const RtInt numWorkers = gmanParallelWorkers(count, workers);

  // Disjoint per-worker slots: each is only ever incremented by the one
  // thread that owns that worker index, so no lock is needed here either.
  std::vector<int> perWorkerCount(static_cast<std::size_t>(numWorkers), 0);
  bool threw = false;
  bool rightType = false;

  try {
    gmanParallelFor(
        count,
        [&](RtInt index, RtInt worker) {
          // index 0 throws before doing any work, so the stop request
          // goes out as close to immediately as this thread can manage.
          // Every other index sleeps first: on an empty loop body, a
          // worker with no exception can race through its entire share
          // before the stop request becomes visible, which is not what
          // this test means to measure.
          if (index == 0) {
            throw GMANError(RIE_BUG, RIE_SEVERE, "parallel_test probe");
          }
          std::this_thread::sleep_for(std::chrono::milliseconds(1));
          perWorkerCount[static_cast<std::size_t>(worker)]++;
        },
        workers);
  } catch (GMANError &) {
    threw = true;
    rightType = true;
  } catch (...) {
    threw = true;
  }

  int total = 0;
  for (int c : perWorkerCount) {
    total += c;
  }

  // Worker 0 throws on its first index; every other worker's fair share
  // is count/numWorkers. If the stop request actually reaches them, at
  // least one stops before finishing that share.
  const int fairShare = static_cast<int>(count / numWorkers);
  bool someWorkerStoppedEarly = false;
  for (std::size_t w = 1; w < perWorkerCount.size(); ++w) {
    if (perWorkerCount[w] < fairShare) {
      someWorkerStoppedEarly = true;
      break;
    }
  }

  check(threw, "a throwing body's exception reaches the calling thread");
  check(rightType, "the rethrown exception is the GMANError the body threw");
  check(total < static_cast<int>(count),
        "fewer than every index ran after the throw requested a stop");
  check(someWorkerStoppedEarly,
        "a worker that never threw also stopped before finishing its share");
}

// count <= 0 calls nothing, at all.
void testCountZeroOrNegative() {
  int calls = 0;
  gmanParallelFor(0, [&](RtInt, RtInt) { ++calls; });
  gmanParallelFor(-5, [&](RtInt, RtInt) { ++calls; });
  check(calls == 0, "count <= 0 calls the body zero times");
}

}  // namespace

int main() {
  testExactlyOnce();
  testSerialPath();
  testWorkerIndices();
  testExceptionsStopTheCall();
  testCountZeroOrNegative();

  return checkSummary("gmanParallelFor holds its contract");
}
