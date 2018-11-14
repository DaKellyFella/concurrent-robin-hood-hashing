#pragma once

/*
K-CAS Robin Hood Hashing implementation.
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

#include "hash_table_common.h"
#include "primitives/brown_kcas.h"
#include "primitives/cache_utils.h"
#include "primitives/harris_kcas.h"
#include <atomic>
#include <fstream>
#include <iostream>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace concurrent_data_structures {

template <class Allocator, template <class> class Reclaimer,
          template <class, class> class KCASSystem, class K,
          class KT = KeyTraits<K>>
class RHSetKCAS {

private:
  typedef Reclaimer<Allocator> MemReclaimer;
  typedef KCASSystem<Allocator, MemReclaimer> KCAS;
  typedef typename KCAS::template KCASEntry<K> Bucket;
  typedef typename KCAS::template KCASEntry<std::uintptr_t> Timestamp;
  typedef typename KCAS::KCASDescriptor Descriptor;

  static const std::size_t S_MAX_KCAS = 3000;
  //  static const std::size_t S_MAX_THREADS = 144;
  static const std::size_t S_MAX_TIMESTAMPS = 2048;

  std::size_t m_size, m_size_mask, m_num_timestamps;
  std::uint8_t m_timestamp_shift;
  CacheAligned<Timestamp> *m_timestamps;
  Bucket *m_table;
  MemReclaimer m_reclaimer;
  KCAS m_kcas;

public:
  RHSetKCAS(const std::size_t size, const std::size_t threads)
      : m_size(nearest_power_of_two(size)), m_size_mask(m_size - 1),
        m_timestamps(static_cast<CacheAligned<Timestamp> *>(
            Allocator::malloc(sizeof(CacheAligned<Timestamp>) *
                              nearest_power_of_two(threads << 7)))),
        m_table(
            static_cast<Bucket *>(Allocator::malloc(sizeof(Bucket) * m_size))),
        m_reclaimer(threads, 4), m_kcas(threads, &m_reclaimer) {

    std::size_t num_timestamps = nearest_power_of_two(threads << 7);
    const K null_key = KT::NullKey;

    std::uint8_t num_timestamp_bits = 0;
    for (std::size_t timestamp = num_timestamps; timestamp > 0;
         timestamp >>= 1, num_timestamp_bits++) {
    }
    std::uint8_t num_size_bits = 0;
    for (std::size_t size = m_size; size > 0; size >>= 1, num_size_bits++) {
    }
    std::uint8_t timestamp_shift = num_size_bits - num_timestamp_bits;
    m_timestamp_shift = timestamp_shift;

    for (std::size_t i = 0; i < m_size; i++) {
      m_kcas.write_value(KT::NullKey, &m_table[i], null_key);
    }
    for (std::size_t i = 0; i < num_timestamps; i++) {
      m_kcas.write_value(0, &m_timestamps[i], std::uintptr_t());
    }
  }
  ~RHSetKCAS() {
    Allocator::free(m_timestamps);
    Allocator::free(m_table);
  }

  bool thread_init(const std::size_t thread_id) { return true; }

  bool contains(const K &key, const std::size_t thread_id) {

    const std::size_t original_hash = KT::hash(key);
    const std::size_t original_bucket = original_hash & m_size_mask;
    ReclaimerPin<MemReclaimer> pin(&m_reclaimer, thread_id);
  loopBegin:
    std::size_t timestamp_index = 0;
    std::uintptr_t timestamps[S_MAX_TIMESTAMPS];
    std::size_t last_timestamp_bucket = std::numeric_limits<std::size_t>::max();

    for (std::size_t current_bucket = original_bucket, cur_dist = 0;;
         current_bucket++, cur_dist++) {
      current_bucket &= m_size_mask;

      const std::size_t current_timestamp_bucket =
          current_bucket >> m_timestamp_shift;

      if (current_timestamp_bucket != last_timestamp_bucket) {
        last_timestamp_bucket = current_timestamp_bucket;
        timestamps[timestamp_index++] = m_kcas.read_value(
            thread_id, pin, &m_timestamps[last_timestamp_bucket]);
      }

      const K current_key =
          m_kcas.read_value(thread_id, pin, &m_table[current_bucket]);
      if (current_key == KT::NullKey) {
        goto counter_check;
      }
      if (key == current_key) {
        return true;
      }
      const std::size_t original_bucket = KT::hash(current_key) & m_size_mask;
      const std::size_t distance =
          distance_from_slot(m_size, original_bucket, current_bucket);
      if (distance < cur_dist) {
        goto counter_check;
      }
    }
  counter_check:
    last_timestamp_bucket = std::numeric_limits<std::size_t>::max();
    for (std::size_t current_bucket = original_bucket, check_index = 0;
         check_index < timestamp_index; current_bucket++) {
      current_bucket &= m_size_mask;
      const std::size_t current_timestamp_bucket =
          current_bucket >> m_timestamp_shift;
      if (current_timestamp_bucket != last_timestamp_bucket) {
        last_timestamp_bucket = current_timestamp_bucket;
        if (timestamps[check_index++] !=
            m_kcas.read_value(thread_id, pin,
                              &m_timestamps[last_timestamp_bucket])) {
          goto loopBegin;
        }
      }
    }
    return false;
  }

  bool add(const K &key, const std::size_t thread_id) {

    const std::size_t original_hash = KT::hash(key);
    const std::size_t original_bucket = original_hash & m_size_mask;
    ReclaimerPin<MemReclaimer> pin(&m_reclaimer, thread_id);
  loopBegin:
    K active_key = key;
    std::size_t last_timestamp_bucket = std::numeric_limits<std::size_t>::max();
    bool inced_active = false;
    std::uintptr_t active_timestamp =
        std::numeric_limits<std::uintptr_t>::max();
    Descriptor *desc = m_kcas.create_descriptor(S_MAX_KCAS, thread_id);

    for (std::size_t current_bucket = original_bucket, active_dist = 0;;
         current_bucket++, active_dist++) {
      current_bucket &= m_size_mask;
      const std::size_t current_timestamp_bucket =
          current_bucket >> m_timestamp_shift;
      if (current_timestamp_bucket != last_timestamp_bucket) {
        last_timestamp_bucket = current_timestamp_bucket;
        active_timestamp = m_kcas.read_value(
            thread_id, pin, &m_timestamps[last_timestamp_bucket]);
        inced_active = false;
      }
      const K current_key =
          m_kcas.read_value(thread_id, pin, &m_table[current_bucket]);
      if (current_key == KT::NullKey) { // Found an empty slot
        desc->add_value(&m_table[current_bucket], current_key, active_key);
        if (!inced_active) {
          desc->add_value(&m_timestamps[last_timestamp_bucket],
                          active_timestamp, active_timestamp + 1);
          inced_active = true;
        }
        bool result = m_kcas.cas(thread_id, pin, desc);
        if (!result) {
          goto loopBegin;
        }
        return true;
      }

      if (current_key == key) {
        m_kcas.free_descriptor(desc);
        return false;
      }

      // Concurrent addition moved our stuff.
      if (current_key == active_key) {
        m_kcas.free_descriptor(desc);
        goto loopBegin;
      }

      const std::size_t original_bucket = KT::hash(current_key) & m_size_mask;
      const std::size_t current_dist =
          distance_from_slot(m_size, original_bucket, current_bucket);
      // SWAP!
      if (current_dist < active_dist) {
        desc->add_value(&m_table[current_bucket], current_key, active_key);
        if (!inced_active) {
          desc->add_value(&m_timestamps[last_timestamp_bucket],
                          active_timestamp, active_timestamp + 1);
          inced_active = true;
        }
        active_key = current_key;
        active_dist = current_dist;
      }
    }
  }

  bool remove(const K &key, const std::size_t thread_id) {

    const std::size_t original_hash = KT::hash(key);
    const std::size_t original_bucket = original_hash & m_size_mask;
    ReclaimerPin<MemReclaimer> pin(&m_reclaimer, thread_id);
  loopBegin:
    std::size_t timestamp_index = 0;
    std::uintptr_t timestamps[S_MAX_TIMESTAMPS];
    std::size_t last_timestamp_bucket = std::numeric_limits<std::size_t>::max();
    Descriptor *desc = m_kcas.create_descriptor(S_MAX_KCAS, thread_id);

    for (std::size_t current_bucket = original_bucket, active_dist = 0;;
         current_bucket++, active_dist++) {
      current_bucket &= m_size_mask;
      const std::size_t current_timestamp_bucket =
          current_bucket >> m_timestamp_shift;
      if (current_timestamp_bucket != last_timestamp_bucket) {
        last_timestamp_bucket = current_timestamp_bucket;
        timestamps[timestamp_index++] = m_kcas.read_value(
            thread_id, pin, &m_timestamps[last_timestamp_bucket]);
      }

      const K current_key =
          m_kcas.read_value(thread_id, pin, &m_table[current_bucket]);
      if (current_key == KT::NullKey) {
        goto counter_check;
      }

      if (current_key == key) {
        bool inced_active = false;
        std::size_t dest_bucket = current_bucket;
        K dest_key = current_key;
        std::uintptr_t dest_timestamp = timestamps[timestamp_index - 1];
        std::size_t dest_timestamp_bucket =
            std::numeric_limits<std::size_t>::max();
        for (std::size_t shuffle_bucket = dest_bucket + 1;; shuffle_bucket++) {
          shuffle_bucket &= m_size_mask;
          const std::size_t shuffle_timestamp_bucket =
              shuffle_bucket >> m_timestamp_shift;
          if (dest_timestamp_bucket != shuffle_timestamp_bucket) {
            dest_timestamp_bucket = shuffle_timestamp_bucket;
            dest_timestamp = m_kcas.read_value(
                thread_id, pin, &m_timestamps[dest_timestamp_bucket]);
            inced_active = false;
          }

          const K shuffle_key =
              m_kcas.read_value(thread_id, pin, &m_table[shuffle_bucket]);
          if (shuffle_key == KT::NullKey) {
            break;
          }
          const std::size_t shuffle_idx = KT::hash(shuffle_key) & m_size_mask;
          const std::size_t shuffle_dist =
              distance_from_slot(m_size, shuffle_idx, shuffle_bucket);
          if (shuffle_dist == 0) {
            break;
          }
          desc->add_value(&m_table[dest_bucket], dest_key, shuffle_key);
          if (!inced_active) {
            desc->add_value(&m_timestamps[dest_timestamp_bucket],
                            dest_timestamp, dest_timestamp + 1);
            inced_active = true;
          }

          dest_key = shuffle_key;
          dest_bucket = shuffle_bucket;
        }
        if (!inced_active) {
          desc->add_value(&m_timestamps[dest_timestamp_bucket], dest_timestamp,
                          dest_timestamp + 1);
          inced_active = true;
        }
        const K null_key = KT::NullKey;
        desc->add_value(&m_table[dest_bucket], dest_key, null_key);
        bool result = m_kcas.cas(thread_id, pin, desc);
        if (!result) {
          goto loopBegin;
        }
        return true;
      }
      const std::size_t original_idx = KT::hash(current_key) & m_size_mask;
      const std::size_t current_dist =
          distance_from_slot(m_size, original_idx, current_bucket);
      if (current_dist < active_dist) {
        goto counter_check;
      }
    }
  counter_check:
    m_kcas.free_descriptor(desc);
    last_timestamp_bucket = std::numeric_limits<std::size_t>::max();
    for (std::size_t current_bucket = original_bucket, check_index = 0;
         check_index < timestamp_index; current_bucket++) {
      current_bucket &= m_size_mask;
      const std::size_t current_timestamp_bucket =
          current_bucket >> m_timestamp_shift;
      if (current_timestamp_bucket != last_timestamp_bucket) {
        last_timestamp_bucket = current_timestamp_bucket;
        if (timestamps[check_index++] !=
            m_kcas.read_value(thread_id, pin,
                              &m_timestamps[last_timestamp_bucket])) {
          goto loopBegin;
        }
      }
    }
    return false;
  }
};

template <class Allocator, template <class> class Reclaimer, class K>
using RHSetHarrisKCAS = RHSetKCAS<Allocator, Reclaimer, HarrisKCAS, K>;

template <class Allocator, template <class> class Reclaimer, class K>
using RHSetBrownKCAS = RHSetKCAS<Allocator, Reclaimer, BrownKCAS, K>;
}
