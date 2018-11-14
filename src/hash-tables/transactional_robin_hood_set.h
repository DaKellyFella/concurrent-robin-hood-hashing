#pragma once

/*
Transactional Lock-Elision Robin Hood Hashing algorithm.
Copyright (C) 2018  Robert Kelly
This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.
This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
You should have received a copy of the GNU General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#include "hash_table_common.h"
#include "primitives/locks.h"
#include <atomic>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <unordered_set>

namespace concurrent_data_structures {

template <class Allocator, template <class> class Reclaimer,
          class K = std::uint64_t, class KT = KeyTraits<K>>
class TransactionalRobinHoodSet {
private:
  ElidedLock m_lock;
  std::atomic<std::size_t> m_size;
  K *m_table;

public:
  TransactionalRobinHoodSet(std::size_t size, const std::size_t threads)
      : m_size(nearest_power_of_two(size)),
        m_table(
            static_cast<K *>(Allocator::malloc(sizeof(K) * m_size.load()))) {
    K null_key = KT::NullKey;
    for (std::size_t i = 0; i < m_size.load(std::memory_order_relaxed); i++) {
      m_table[i] = null_key;
    }
  }

  ~TransactionalRobinHoodSet() { Allocator::free(m_table); }

  bool thread_init(const std::size_t thread_id) { return true; }

  bool contains(const K &key, const std::size_t thread_id) {
    std::size_t size = m_size.load(std::memory_order_relaxed);
    std::size_t size_mask = size - 1;
    std::size_t original_slot = KT::hash(key);
    std::lock_guard<ElidedLock> m_lock_guard(m_lock);
    for (std::size_t i = original_slot, cur_dist = 0;; i++, cur_dist++) {
      i &= size_mask;
      K current_key = m_table[i];
      if (current_key == KT::NullKey) {
        return false;
      }
      if (key == current_key) {
        return true;
      }
      std::size_t current_original_slot = KT::hash(current_key) & size_mask;
      if (distance_from_slot(size, current_original_slot, i) < cur_dist) {
        return false;
      }
    }
  }

  bool add(const K &key, const std::size_t thread_id) {

    std::size_t size = m_size.load(std::memory_order_relaxed);
    std::size_t size_mask = size - 1;

    std::size_t original_bucket = KT::hash(key) & size_mask;
    std::lock_guard<ElidedLock> m_lock_guard(m_lock);
    K active_key = key;
    std::size_t active_slot = original_bucket;

    for (std::size_t i = active_slot, active_dist = 0;; i++, active_dist++) {
      i &= size_mask;
      K current_key = m_table[i];

      // Found an empty slot
      if (current_key == KT::NullKey) {
        m_table[i] = active_key;
        return true;
      }
      if (active_key == current_key) {
        return false;
      }

      std::size_t current_original_slot = KT::hash(current_key) & size_mask;
      std::size_t current_dist =
          distance_from_slot(size, current_original_slot, i);
      if (current_dist < active_dist) {
        std::swap(active_key, m_table[i]);
        active_dist = current_dist;
      }
    }
  }

  bool remove(K key, const std::size_t thread_id) {
    std::size_t size = m_size.load(std::memory_order_relaxed);
    std::size_t size_mask = size - 1;
    std::size_t original_slot = KT::hash(key);

    std::lock_guard<ElidedLock> m_lock_guard(m_lock);

    for (std::size_t i = original_slot, cur_dist = 0;; i++, cur_dist++) {
      i &= size_mask;
      K current_key = m_table[i];
      if (current_key == KT::NullKey) {
        return false;
      }
      if (key == current_key) {
        for (std::size_t current = i + 1; true; current++, i++) {
          current &= size_mask;
          i &= size_mask;
          current_key = m_table[current];
          if (current_key == KT::NullKey or
              distance_from_slot(size, KT::hash(current_key) & size_mask,
                                 current) == 0) {
            break;
          }
          m_table[i] = current_key;
        }
        m_table[i] = KT::NullKey;
        return true;
      }
      std::size_t current_original_slot = KT::hash(current_key) & size_mask;
      if (distance_from_slot(size, current_original_slot, i) < cur_dist) {
        return false;
      }
    }
  }
  void print_table() {
    std::unordered_set<K> keys;
    for (std::size_t i = 0; i < m_size.load(std::memory_order_relaxed); i++) {
      K current_key = m_table[i];
      if (current_key == KT::NullKey) {
        continue;
      }
      if (keys.find(current_key) != keys.end()) {
        std::cout << "ERROR: DUPLCIATE COPY OF KEY: " << current_key
                  << std::endl;
        continue;
      }
      keys.insert(current_key);
    }
  }
};
}
