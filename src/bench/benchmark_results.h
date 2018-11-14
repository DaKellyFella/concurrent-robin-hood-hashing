#pragma once

/*
Encodes results of hash table benchmarks.
Copyright (C) 2018 Robert Kelly

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "primitives/cache_utils.h"
#include "thread_papi_wrapper.h"
#include "thread_pinner.h"
#include <cstdint>

namespace concurrent_data_structures {
struct SetThreadBenchmarkResult {
  std::uint64_t query_attempts, query_successes;
  std::uint64_t addition_attempts, addition_successes;
  std::uint64_t removal_attempts, removal_successes;
  PapiCounters papi_counters;
  SetThreadBenchmarkResult()
      : query_attempts(0), query_successes(0), addition_attempts(0),
        addition_successes(0), removal_attempts(0), removal_successes(0) {}
};

struct QueueThreadBenchmarkResult {
  std::uint64_t addition_attempts, addition_successes;
  std::uint64_t removal_attempts, removal_successes;
  PapiCounters papi_counters;
  QueueThreadBenchmarkResult()
      : addition_attempts(0), addition_successes(0), removal_attempts(0),
        removal_successes(0) {}
};

struct SetBenchmarkResult {
  std::size_t num_threads;
  CacheAligned<SetThreadBenchmarkResult> *per_thread_benchmark_result;
  std::vector<ThreadPinner::ProcessorInfo> scheduling_info;
  SetBenchmarkResult(const std::size_t num_threads)
      : num_threads(num_threads),
        per_thread_benchmark_result(
            new CacheAligned<SetThreadBenchmarkResult>[num_threads]) {}

  //  SetBenchmarkResult(const SetBenchmarkResult &rhs) {
  //    this->num_threads = rhs.num_threads;
  //    this->per_thread_benchmark_result =
  //        new SetThreadBenchmarkResult[num_threads];
  //    for (std::size_t i = 0; i < num_threads; i++) {
  //      per_thread_benchmark_result[i] = rhs.per_thread_benchmark_result[i];
  //    }
  //  }

  //  SetBenchmarkResult &operator=(const SetBenchmarkResult &rhs) {
  //    if (this != &rhs) {
  //      delete[] this->per_thread_benchmark_result;
  //      this->num_threads = rhs.num_threads;
  //      this->per_thread_benchmark_result =
  //          new SetThreadBenchmarkResult[num_threads];
  //      for (std::size_t i = 0; i < num_threads; i++) {
  //        per_thread_benchmark_result[i] = rhs.per_thread_benchmark_result[i];
  //      }
  //    }
  //    return *this;
  //  }
  //  SetBenchmarkResult &operator=(SetBenchmarkResult &&rhs) = delete;
  //  SetBenchmarkResult(SetBenchmarkResult &&rhs) = delete;
  //  ~SetBenchmarkResult() { delete[] per_thread_benchmark_result; }

  const SetThreadBenchmarkResult collate_results() const {
    SetThreadBenchmarkResult results;
    for (std::size_t i = 0; i < num_threads; i++) {
      results.query_attempts += per_thread_benchmark_result[i].query_attempts;
      results.query_successes += per_thread_benchmark_result[i].query_successes;
      results.addition_attempts +=
          per_thread_benchmark_result[i].addition_attempts;
      results.addition_successes +=
          per_thread_benchmark_result[i].addition_successes;
      results.removal_attempts +=
          per_thread_benchmark_result[i].removal_attempts;
      results.removal_successes +=
          per_thread_benchmark_result[i].removal_successes;
      for (std::size_t event = 0; event < PAPI_EVENTS::TOTAL_PAPI_EVENTS;
           event++) {
        results.papi_counters.counters[event] +=
            per_thread_benchmark_result[i].papi_counters.counters[event];
      }
    }
    return results;
  }
};

struct QueueBenchmarkResult {
  std::size_t num_threads;
  CacheAligned<QueueThreadBenchmarkResult> *per_thread_benchmark_result;
  std::vector<ThreadPinner::ProcessorInfo> scheduling_info;
  QueueBenchmarkResult(const std::size_t num_threads)
      : num_threads(num_threads),
        per_thread_benchmark_result(
            new CacheAligned<QueueThreadBenchmarkResult>[num_threads]) {}

  const QueueThreadBenchmarkResult collate_results() const {
    QueueThreadBenchmarkResult results;
    for (std::size_t i = 0; i < num_threads; i++) {
      results.addition_attempts +=
          per_thread_benchmark_result[i].addition_attempts;
      results.addition_successes +=
          per_thread_benchmark_result[i].addition_successes;
      results.removal_attempts +=
          per_thread_benchmark_result[i].removal_attempts;
      results.removal_successes +=
          per_thread_benchmark_result[i].removal_successes;
      for (std::size_t event = 0; event < PAPI_EVENTS::TOTAL_PAPI_EVENTS;
           event++) {
        results.papi_counters.counters[event] +=
            per_thread_benchmark_result[i].papi_counters.counters[event];
      }
    }
    return results;
  }
};
}
