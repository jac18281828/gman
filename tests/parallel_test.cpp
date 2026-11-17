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

constexpr std::chrono::seconds kWaitDeadline{5};
constexpr std::chrono::milliseconds kPollInterval{1};

// One run's bookkeeping: which index landed in which worker's slot, and
// which thread ran each worker. Each vector entry belongs to exactly one
// worker for the whole call, so writing it from inside the body needs no
// lock -- that is the invariant gman::parallelFor exists to make true.
struct RunRecord {
  std::vector<int> indexHits;
  std::vector<int> perWorkerCount;
  std::vector<std::set<std::thread::id>> perWorkerThreadIds;
};

RunRecord runRecording(RtInt count, RtInt workers) {
  RtInt numWorkers = gman::parallelWorkers(count, workers);
  RunRecord record;
  record.indexHits.assign(static_cast<std::size_t>(count), 0);
  record.perWorkerCount.assign(static_cast<std::size_t>(numWorkers), 0);
  record.perWorkerThreadIds.resize(static_cast<std::size_t>(numWorkers));

  gman::parallelFor(
      count,
      [&](RtInt index, RtInt worker) {
        record.indexHits[static_cast<std::size_t>(index)]++;
        record.perWorkerCount[static_cast<std::size_t>(worker)]++;
        record.perWorkerThreadIds[static_cast<std::size_t>(worker)].insert(std::this_thread::get_id());
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
      check(allOnce, "count=" + std::to_string(count) + " workers=" + std::to_string(workers) +
                         ": every index ran exactly once");
    }
  }
}

// workers=1 forces the serial path: no thread created, everything runs on
// the calling thread as worker 0.
void testSerialPath() {
  const std::thread::id callingThread = std::this_thread::get_id();
  RunRecord record = runRecording(100, 1);

  check(gman::parallelWorkers(100, 1) == 1, "gman::parallelWorkers(100, 1) reports one worker");
  check(record.perWorkerThreadIds.size() == 1 && record.perWorkerThreadIds[0].size() == 1 &&
            *record.perWorkerThreadIds[0].begin() == callingThread,
        "workers=1 runs every body on the calling thread as worker 0");
}

// gman::parallelWorkers reports the count the call actually used; every
// worker index stays below it; a worker's indices all ran on one thread;
// per-worker counts sum to count.
void testWorkerIndices() {
  const std::vector<RtInt> counts = {0, 1, 7, 1000};
  const std::vector<RtInt> workerCounts = {1, 2, 8, 0};

  for (RtInt count : counts) {
    for (RtInt workers : workerCounts) {
      RtInt numWorkers = gman::parallelWorkers(count, workers);
      RunRecord record = runRecording(count, workers);

      check(static_cast<RtInt>(record.perWorkerCount.size()) == numWorkers,
            "count=" + std::to_string(count) + " workers=" + std::to_string(workers) +
                ": worker index stays below gman::parallelWorkers' report");

      int total = 0;
      bool oneThreadPerWorker = true;
      for (std::size_t w = 0; w < record.perWorkerThreadIds.size(); ++w) {
        total += record.perWorkerCount[w];
        if (record.perWorkerCount[w] > 0 && record.perWorkerThreadIds[w].size() != 1) {
          oneThreadPerWorker = false;
        }
      }
      check(oneThreadPerWorker, "count=" + std::to_string(count) + " workers=" + std::to_string(workers) +
                                    ": bodies sharing a worker index ran on one thread");
      check(total == count, "count=" + std::to_string(count) + " workers=" + std::to_string(workers) +
                                ": per-worker counts sum to count");
    }
  }
}

template <typename Done> bool waitUntil(Done const& done, std::chrono::steady_clock::time_point deadline) {
  while (!done()) {
    if (std::chrono::steady_clock::now() >= deadline) {
      return false;
    }
    std::this_thread::sleep_for(kPollInterval);
  }
  return true;
}

// A body that throws stops every worker from starting another index;
// after every worker returns, the exception is rethrown on the calling
// thread and the process survives.
void testExceptionsStopTheCall() {
  const RtInt count = 400;
  const RtInt workers = 4;

  std::atomic<int> started{0};
  std::atomic<int> completed{0};
  bool threw = false;
  bool rightType = false;

  try {
    gman::parallelFor(
        count,
        [&](RtInt index, RtInt /*worker*/) {
          ++started;
          // The first index claimed throws before doing any work, so the
          // stop request goes out as close to immediately as this thread
          // can manage. Every other index sleeps first: on an empty loop
          // body, a worker with no exception can race through the whole
          // range before the stop request becomes visible, which is not
          // what this test means to measure.
          if (index == 0) {
            throw GMANError(RIE_BUG, RIE_SEVERE, "parallel_test probe");
          }
          std::this_thread::sleep_for(kPollInterval);
          ++completed;
        },
        workers);
  } catch (GMANError&) {
    threw = true;
    rightType = true;
  } catch (...) {
    threw = true;
  }

  check(threw, "a throwing body's exception reaches the calling thread");
  check(rightType, "the rethrown exception is the GMANError the body threw");
  check(started.load() < count, "fewer than every index started after the throw requested a stop");
  check(completed.load() < count / 2, "the workers that never threw stopped well short of the full range (" +
                                          std::to_string(completed.load()) + " of " + std::to_string(count) +
                                          " completed)");
}

// A body stuck on index 0 holds back only itself: the other workers claim
// every remaining index while it waits.
void testSlowIndexHoldsNothingBack() {
  const RtInt count = 64;
  const RtInt workers = 4;

  std::vector<std::atomic<int>> hits(static_cast<std::size_t>(count));
  std::atomic<int> finishedOthers{0};
  bool waitEndedOnCompletion = false;

  gman::parallelFor(
      count,
      [&](RtInt index, RtInt /*worker*/) {
        if (index == 0) {
          const auto deadline = std::chrono::steady_clock::now() + kWaitDeadline;
          waitEndedOnCompletion = waitUntil([&] { return finishedOthers.load() == count - 1; }, deadline);
          ++hits[0];
          return;
        }
        ++hits[static_cast<std::size_t>(index)];
        ++finishedOthers;
      },
      workers);

  bool allOnce = true;
  for (auto const& h : hits) {
    allOnce = allOnce && h.load() == 1;
  }
  check(waitEndedOnCompletion, "a blocked index 0 saw every other index finish before its deadline");
  check(allOnce, "with index 0 blocked, every index still ran exactly once");
}

// With as many indices as workers and every body waiting for all the
// others to start, the call completes only if every worker really runs at
// once, each on its own thread.
void testEveryWorkerRunsAtOnce() {
  const RtInt workers = 4;
  const RtInt count = workers;

  std::atomic<int> started{0};
  std::vector<int> sawEveryStart(static_cast<std::size_t>(count), 0);
  std::vector<RtInt> workerOfIndex(static_cast<std::size_t>(count), -1);
  std::vector<std::thread::id> threadOfIndex(static_cast<std::size_t>(count));

  const auto deadline = std::chrono::steady_clock::now() + kWaitDeadline;
  gman::parallelFor(
      count,
      [&](RtInt index, RtInt worker) {
        const auto slot = static_cast<std::size_t>(index);
        workerOfIndex[slot] = worker;
        threadOfIndex[slot] = std::this_thread::get_id();
        ++started;
        sawEveryStart[slot] = waitUntil([&] { return started.load() == count; }, deadline) ? 1 : 0;
      },
      workers);

  bool allSawEveryStart = true;
  for (int saw : sawEveryStart) {
    allSawEveryStart = allSawEveryStart && saw == 1;
  }
  const std::set<RtInt> distinctWorkers(workerOfIndex.begin(), workerOfIndex.end());
  const std::set<std::thread::id> distinctThreads(threadOfIndex.begin(), threadOfIndex.end());

  check(allSawEveryStart, "every body saw all four bodies started before the deadline");
  check(distinctWorkers == std::set<RtInt>({0, 1, 2, 3}), "the four bodies ran as worker ids 0, 1, 2 and 3");
  check(distinctThreads.size() == static_cast<std::size_t>(count), "the four bodies ran on four distinct threads");
}

// count <= 0 calls nothing, at all.
void testCountZeroOrNegative() {
  int calls = 0;
  gman::parallelFor(0, [&](RtInt, RtInt) { ++calls; });
  gman::parallelFor(-5, [&](RtInt, RtInt) { ++calls; });
  check(calls == 0, "count <= 0 calls the body zero times");
}

} // namespace

int main() {
  testExactlyOnce();
  testSerialPath();
  testWorkerIndices();
  testExceptionsStopTheCall();
  testSlowIndexHoldsNothingBack();
  testEveryWorkerRunsAtOnce();
  testCountZeroOrNegative();

  return checkSummary("gman::parallelFor holds its contract");
}
