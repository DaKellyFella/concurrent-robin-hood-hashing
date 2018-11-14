#pragma once

/*
A thread pinning class.
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

#include "cpuinfo.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <deque>
#include <iostream>
#include <pthread.h>
#include <thread>
#include <unordered_map>
#include <vector>

namespace concurrent_data_structures {

class ThreadPinner {
public:
  struct ProcessorInfo {
    std::int32_t user_id, linux_id;
    std::uint32_t L3_cache_id, L2_id, L2_index;
    bool taken;
  };

private:
  class Cluster {
  private:
    std::uint32_t num_processors, current_procesor = 0;
    ProcessorInfo *processors;
    std::size_t id;

  public:
    Cluster() {}
    Cluster(std::size_t size, const std::size_t id)
        : num_processors(size), id(id) {
      processors = new ProcessorInfo[num_processors];
    }
    void add_processor(const cpuinfo_processor *processor,
                       const std::uint32_t L3_cache_id,
                       const std::uint32_t L2_id,
                       const std::uint32_t L2_index) {
      // std::cout << "ADDING " << processor->linux_id << " to id: " << id << "
      // " << current_procesor << std::endl;
      processors[current_procesor++] = ProcessorInfo{
          0, processor->linux_id, L3_cache_id, L2_id, L2_index, false};
    }

    ProcessorInfo *get_next_processor() {
      for (std::uint32_t index = 0; index < 2; index++) {
        for (std::uint32_t processor = 0; true; processor++) {
          if (processor >= num_processors) {
            break;
          }
          if (processors[processor].L2_index != index) {
            continue;
          }
          if (!processors[processor].taken) {
            processors[processor].taken = true;
            return &processors[processor];
          }
        }
      }
      return nullptr;
    }
    friend class ThreadPinner;
  };

  void add_processor(const cpuinfo_processor *processor,
                     const std::uint32_t L2_id, const std::uint32_t L2_index) {
    if (m_hyperthreading_before_socket_switch) {
      m_clusters[m_cache_map[processor->cache.l3]]->add_processor(
          processor, m_cache_map[processor->cache.l3], L2_id, L2_index);
    } else {
      const std::uint32_t final_index =
          m_cache_map[processor->cache.l3] +
          (cpuinfo_get_l3_caches_count() * L2_index);
      m_clusters[final_index]->add_processor(
          processor, m_cache_map[processor->cache.l3], L2_id, L2_index);
    }
  }

  bool m_hyperthreading_before_socket_switch;
  std::unordered_map<const cpuinfo_cache *, std::uint32_t> m_cache_map;
  std::unordered_map<std::thread *, const ProcessorInfo *> m_scheduled;
  Cluster **m_clusters;

public:
  ThreadPinner(bool hyperthreading_before_socket_switch)
      : m_hyperthreading_before_socket_switch(
            hyperthreading_before_socket_switch) {
    cpuinfo_initialize();
    const std::uint32_t num_caches = cpuinfo_get_l3_caches_count();
    // std::cout << "Number of L3 caches: " << num_caches << std::endl;
    const std::uint32_t num_clusters =
        m_hyperthreading_before_socket_switch ? num_caches : num_caches << 1;
    m_clusters = new Cluster *[num_clusters];
    const cpuinfo_cache *L3_caches = cpuinfo_get_l3_caches();
    for (std::uint32_t current_L3 = 0; current_L3 < num_caches; current_L3++) {
      const std::uint32_t proc_count = L3_caches[current_L3].processor_count;
      // std::cout << "Proc count: " << proc_count << std::endl;
      m_cache_map[L3_caches + current_L3] = current_L3;
      if (m_hyperthreading_before_socket_switch) {
        m_clusters[current_L3] = new Cluster(proc_count, current_L3);
      } else {
        m_clusters[current_L3] = new Cluster(proc_count / 2, current_L3);
        m_clusters[current_L3 + num_caches] =
            new Cluster(proc_count / 2, current_L3 + num_caches);
      }
    }
    const cpuinfo_cache *L2_caches = cpuinfo_get_l2_caches();
    for (std::uint32_t current_L2 = 0;
         current_L2 < cpuinfo_get_l2_caches_count(); current_L2++) {
      for (std::uint32_t processor = 0;
           processor < L2_caches[current_L2].processor_count; processor++) {
        add_processor(cpuinfo_get_processor(
                          L2_caches[current_L2].processor_start + processor),
                      current_L2, processor);
      }
    }
  }

  bool schedule_thread(std::thread *thread, std::int32_t user_id) {
    const std::uint32_t num_clusters = cpuinfo_get_l3_caches_count();
    const std::uint32_t total_clusters = m_hyperthreading_before_socket_switch
                                             ? num_clusters
                                             : num_clusters << 1;
    for (std::uint32_t cluster = 0; cluster < total_clusters; cluster++) {
      ProcessorInfo *proc_info = m_clusters[cluster]->get_next_processor();

      if (proc_info == nullptr) {
        continue;
      }
      cpu_set_t cpu_set;
      CPU_ZERO(&cpu_set);
      CPU_SET(proc_info->linux_id, &cpu_set);
      // std::cout << "Pinning user id: " << user_id << " to core id: " <<
      // proc_info->linux_id << std::endl;
      if (pthread_setaffinity_np(thread->native_handle(), sizeof(cpu_set_t),
                                 &cpu_set) != 0) {
        return false;
      }
      proc_info->user_id = user_id;
      m_scheduled.insert(std::make_pair(thread, proc_info));
      return true;
    }
    // std::cout << "No more cores" << std::endl;
    return false;
  }

  std::vector<ProcessorInfo> join() {
    std::vector<ProcessorInfo> info;
    for (auto it : m_scheduled) {
      it.first->join();
      info.push_back(*it.second);
    }
    std::sort(info.begin(), info.end(),
              [](const ProcessorInfo &lhs, const ProcessorInfo &rhs) {
                if (lhs.L2_id < rhs.L2_id) {
                  return true;
                } else if (lhs.L2_id == rhs.L2_id) {
                  return lhs.L2_index < rhs.L2_index;
                } else {
                  return false;
                }
              });
    return info;
  }

  ~ThreadPinner() { cpuinfo_deinitialize(); }
};
}
