#pragma once

/*
Hash table benchmarking class.
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

#include "action_generator.h"
#include "benchmark_config.h"
#include "benchmark_results.h"
#include "hash-tables/table_init.h"
#include "primitives/barrier.h"
#include "primitives/cache_utils.h"
#include "thread_papi_wrapper.h"
#include "thread_pinner.h"
#include <atomic>
#include <cstdint>
#include <deque>
#include <papi.h>
#include <pthread.h>
#include <thread>
#include <vector>

namespace concurrent_data_structures {

template <class Table, class Key> class TableBenchmark {
private:
  enum class BenchmarkState { RUNNING, STOPPED };

  struct BenchmarkThreadData {
    const std::size_t thread_id;
    std::atomic<BenchmarkState> *state;
    ThreadBarrierWrapper *thread_barrier;
    BenchmarkThreadData(const std::size_t thread_id,
                        std::atomic<BenchmarkState> *state,
                        ThreadBarrierWrapper *thread_barrier)
        : thread_id(thread_id), state(state), thread_barrier(thread_barrier) {}
  };

  struct TestThreadData : public BenchmarkThreadData {
    std::deque<Key> starting_keys;
    TestThreadData(const std::size_t thread_id,
                   std::atomic<BenchmarkState> *state,
                   ThreadBarrierWrapper *thread_barrier,
                   const std::deque<Key> &starting_keys)
        : BenchmarkThreadData(thread_id, state, thread_barrier),
          starting_keys(starting_keys) {}
  };

  const SetBenchmarkConfig m_config;
  SetBenchmarkResult m_results;
  Table *m_table;

  void benchmark_routine(CacheAligned<BenchmarkThreadData> *thread_data) {
    SetActionGenerator<Key> action_generator(m_config);
    CacheAligned<SetThreadBenchmarkResult> *result =
        m_results.per_thread_benchmark_result + thread_data->thread_id;
    ThreadPapiWrapper papi_wrapper(m_config.base.papi_active);
    bool init = m_table->thread_init(thread_data->thread_id);
    thread_data->thread_barrier->wait();
    assert(init);
    assert(papi_wrapper.start());
    while (thread_data->state->load(std::memory_order_relaxed) ==
           BenchmarkState::RUNNING) {
      const SetAction current_action = action_generator.generate_action();
      const auto key = action_generator.generate_key();
      switch (current_action) {
      case SetAction::Contains:
        result->query_attempts++;
        if (m_table->contains(key, thread_data->thread_id)) {
          result->query_successes++;
        }
        break;
      case SetAction::Add:
        result->addition_attempts++;
        if (m_table->add(key, thread_data->thread_id)) {
          result->addition_successes++;
        }
        break;
      case SetAction::Remove:
        result->removal_attempts++;
        if (m_table->remove(key, thread_data->thread_id)) {
          result->removal_successes++;
        }
        break;
      }
    }
    assert(papi_wrapper.stop(result->papi_counters));
  }

  void test_routine(CacheAligned<TestThreadData> *thread_data) {
    SetActionGenerator<Key> action_generator(m_config);
    CacheAligned<SetThreadBenchmarkResult> *result =
        m_results.per_thread_benchmark_result + thread_data->thread_id;
    ThreadPapiWrapper papi_wrapper(m_config.base.papi_active);
    std::deque<Key> key_pool = thread_data->starting_keys;
    std::random_shuffle(key_pool.begin(), key_pool.end());
    bool init = m_table->thread_init(thread_data->thread_id);
    thread_data->thread_barrier->wait();
    assert(init);
    assert(papi_wrapper.start());
    while (thread_data->state->load(std::memory_order_relaxed) ==
           BenchmarkState::RUNNING) {
      const SetAction current_action = action_generator.generate_action();
      auto key = action_generator.generate_key();
      switch (current_action) {
      case SetAction::Contains:
        result->query_attempts++;
        if (m_table->contains(key, thread_data->thread_id)) {
          result->query_successes++;
        }
        break;
      case SetAction::Add:
        if (key_pool.size() > 0) {
          result->addition_attempts++;
          const Key key = key_pool.front();
          key_pool.pop_front();
          bool added = m_table->add(key, thread_data->thread_id);
          assert(added);
          if (added) {
            result->addition_successes++;
          }
        }
        break;
      case SetAction::Remove:
        result->removal_attempts++;
        while (!m_table->remove(key, thread_data->thread_id)) {
          key = action_generator.generate_key();
        }
        key_pool.push_back(key);
        assert(!m_table->contains(key, thread_data->thread_id));
        break;
      }
    }
    thread_data->starting_keys = key_pool;
    assert(papi_wrapper.stop(result->papi_counters));
  }

public:
  TableBenchmark(const SetBenchmarkConfig &config)
      : m_config(config), m_results(config.base.num_threads) {
    std::cout << "Initialising hash-table." << std::endl;
    m_table = TableInit<Table, Key>(config);
    std::cout << "Hash-table initialised." << std::endl;
  }
  ~TableBenchmark() { delete m_table; }

  SetBenchmarkResult bench() {
    std::cout << "Running benchmark...." << std::endl;
    ThreadBarrierWrapper barrier(m_config.base.num_threads + 1);
    std::vector<CacheAligned<BenchmarkThreadData>> thread_data;
    std::atomic<BenchmarkState> benchmark_state{BenchmarkState::RUNNING};
    for (std::size_t t = 0; t < m_config.base.num_threads; t++) {
      thread_data.push_back(
          CacheAligned<BenchmarkThreadData>(t, &benchmark_state, &barrier));
    }
    ThreadPinner pinner(m_config.base.hyperthreading);
    std::vector<std::thread *> threads;
    std::cout << "Launching threads." << std::endl;
    for (std::size_t t = 0; t < m_config.base.num_threads; t++) {
      threads.push_back(new std::thread(&TableBenchmark::benchmark_routine,
                                        this, &thread_data[t]));
      assert(pinner.schedule_thread(threads[t], t));
    }
    std::cout << "Waiting..." << std::endl;
    // Wait for other threads.
    barrier.wait();
    // Sleep.
    std::this_thread::sleep_for(m_config.base.duration);
    // End benchmark.
    benchmark_state.store(BenchmarkState::STOPPED);
    std::cout << "Joining threads." << std::endl;
    m_results.scheduling_info = pinner.join();
    for (std::size_t t = 0; t < m_config.base.num_threads; t++) {
      delete threads[t];
    }
    std::cout << "Collating benchmark data." << std::endl;
    return m_results;
  }

  bool test() {
    std::cout << "Running tests...." << std::endl;
    // Determine what keys are in the table...
    std::deque<Key> keys;
    for (std::size_t i = 0; i < m_config.table_size; i++) {
      if (!m_table->contains(i, i % m_config.base.num_threads)) {
        keys.push_back(i);
      }
    }

    const std::size_t unused_keys = keys.size();
    const std::size_t key_slice = unused_keys / m_config.base.num_threads;

    ThreadBarrierWrapper barrier(m_config.base.num_threads + 1);
    std::vector<CacheAligned<TestThreadData>> thread_data;
    std::atomic<BenchmarkState> benchmark_state{BenchmarkState::RUNNING};
    for (std::size_t t = 0; t < m_config.base.num_threads; t++) {
      std::deque<Key> thread_keys;
      const std::size_t base = t * key_slice;
      const std::size_t limit = base + key_slice;
      for (std::size_t i = base; i < limit; i++) {
        thread_keys.push_back(keys[i]);
      }
      thread_data.push_back(CacheAligned<TestThreadData>(
          t, &benchmark_state, &barrier, thread_keys));
    }

    ThreadPinner pinner(m_config.base.hyperthreading);
    std::vector<std::thread *> threads;
    std::cout << "Launching threads." << std::endl;
    for (std::size_t t = 0; t < m_config.base.num_threads; t++) {
      threads.push_back(new std::thread(&TableBenchmark::test_routine, this,
                                        &thread_data[t]));
      pinner.schedule_thread(threads[t], t);
    }
    std::cout << "Waiting..." << std::endl;
    // Wait for other threads.
    barrier.wait();
    // Sleep.
    std::this_thread::sleep_for(m_config.base.duration);
    // End benchmark.
    benchmark_state.store(BenchmarkState::STOPPED);
    std::cout << "Joining threads." << std::endl;
    m_results.scheduling_info = pinner.join();
    std::cout << "Gathering free keys." << std::endl;
    std::deque<Key> free_keys;
    for (std::size_t t = 0; t < m_config.base.num_threads; t++) {
      for (std::size_t i = 0; i < thread_data[t].starting_keys.size(); i++) {
        free_keys.push_back(thread_data[t].starting_keys[i]);
      }
      delete threads[t];
    }
    std::cout << "Testing table now." << std::endl;
    for (std::size_t i = 0; i < free_keys.size(); i++) {
      Key free_key = free_keys[i];
      assert(!m_table->contains(free_key, i % m_config.base.num_threads));
    }
    return true;
  }
};
}
