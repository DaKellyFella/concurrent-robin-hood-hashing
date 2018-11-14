#pragma once

////////////////////////////////////////////////////////////////////////////////
// ConcurrentHopscotchHashMap Class
//
////////////////////////////////////////////////////////////////////////////////
// TERMS OF USAGE
//------------------------------------------------------------------------------
//
//	Permission to use, copy, modify and distribute this software and
//	its documentation for any purpose is hereby granted without fee,
//	provided that due acknowledgments to the authors are provided and
//	this permission notice appears in all copies of the software.
//	The software is provided "as is". There is no warranty of any kind.
//
// Authors:
//	Maurice Herlihy
//	Brown University
//	and
//	Nir Shavit
//	Tel-Aviv University
//	and
//	Moran Tzafrir
//	Tel-Aviv University
//
//	Date: July 15, 2008.
//
////////////////////////////////////////////////////////////////////////////////
// Programmer : Moran Tzafrir (MoranTza@gmail.com)
//
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
// INCLUDE DIRECTIVES
////////////////////////////////////////////////////////////////////////////////
#include "hash_table_common.h"
#include "math.h"
#include "primitives/locks.h"
#include <atomic>
#include <cstdint>
#include <limits.h>
#include <mutex>
#include <stdio.h>
#include <thread>

////////////////////////////////////////////////////////////////////////////////
// CLASS: ConcurrentHopscotchHashMap
////////////////////////////////////////////////////////////////////////////////

namespace concurrent_data_structures {

template <class Allocator, template <class> class Reclaimer, class Lock,
          class K, class KT = KeyTraits<K>>
class HopscotchHashSet {
  static const std::int32_t _NULL_DELTA = INT_MIN;

private:
  // Inner Classes ............................................................
  struct Bucket {
    std::atomic<std::int32_t> _first_delta, _next_delta;
    std::atomic_size_t _hash;
    std::atomic<K> _key;

    void init() {
      _first_delta = _NULL_DELTA;
      _next_delta = _NULL_DELTA;
      _hash = 0;
      _key = KT::NullKey;
    }
  };

  struct Segment {
    std::uint32_t volatile _timestamp;
    Lock _lock;

    void init() {
      _lock = Lock();
      _timestamp = 0;
    }
  };

  inline int first_msb_bit_indx(std::uint32_t x) {
    if (0 == x)
      return -1;
    return __builtin_clz(x) - 1;
  }

  // Fields ...................................................................
  std::size_t m_size_mask;
  std::uint32_t m_segment_shift;
  CacheAligned<Segment> *volatile _segments;
  Bucket *volatile _table;

  const int _cache_mask;
  const bool _is_cacheline_alignment;

  // Constants ................................................................
  static const std::uint32_t _INSERT_RANGE = 1024 * 4;
  // static const std::uint32_t _NUM_SEGMENTS	= 1024*1;
  // static const std::uint32_t _SEGMENTS_MASK = _NUM_SEGMENTS-1;
  static const std::uint32_t _RESIZE_FACTOR = 2;

  // Small Utilities ..........................................................
  Bucket *get_start_cacheline_bucket(Bucket *const bucket) {
    return (bucket - ((bucket - _table) & _cache_mask)); // can optimize
  }

  void remove_key(Segment &segment, Bucket *const from_bucket,
                  Bucket *const key_bucket, Bucket *const prev_key_bucket,
                  const unsigned int hash) {
    key_bucket->_key.store(KT::NullKey, std::memory_order_relaxed);

    if (NULL == prev_key_bucket) {
      if (_NULL_DELTA ==
          key_bucket->_next_delta.load(std::memory_order_relaxed))
        from_bucket->_first_delta.store(_NULL_DELTA, std::memory_order_relaxed);
      else
        from_bucket->_first_delta.store(
            (from_bucket->_first_delta.load(std::memory_order_relaxed) +
             key_bucket->_next_delta.load(std::memory_order_relaxed)),
            std::memory_order_relaxed);
    } else {
      if (_NULL_DELTA ==
          key_bucket->_next_delta.load(std::memory_order_relaxed))
        prev_key_bucket->_next_delta.store(_NULL_DELTA,
                                           std::memory_order_relaxed);
      else
        prev_key_bucket->_next_delta.store(
            (prev_key_bucket->_next_delta.load(std::memory_order_relaxed) +
             key_bucket->_next_delta.load(std::memory_order_relaxed)),
            std::memory_order_relaxed);
    }

    ++(segment._timestamp);
    key_bucket->_next_delta = _NULL_DELTA;
    key_bucket->_hash.store(0, std::memory_order_release);
  }
  void add_key_to_begining_of_list(Bucket *const keys_bucket,
                                   Bucket *const free_bucket, const K &key) {
    free_bucket->_key.store(key, std::memory_order_relaxed);

    if (0 == keys_bucket->_first_delta.load(std::memory_order_relaxed)) {
      if (_NULL_DELTA ==
          keys_bucket->_next_delta.load(std::memory_order_relaxed))
        free_bucket->_next_delta.store(_NULL_DELTA, std::memory_order_relaxed);
      else
        free_bucket->_next_delta.store(
            (std::int32_t)(
                (keys_bucket +
                 keys_bucket->_next_delta.load(std::memory_order_relaxed)) -
                free_bucket),
            std::memory_order_relaxed);
      keys_bucket->_next_delta.store((std::int32_t)(free_bucket - keys_bucket));
    } else {
      if (_NULL_DELTA ==
          keys_bucket->_first_delta.load(std::memory_order_relaxed))
        free_bucket->_next_delta.store(_NULL_DELTA, std::memory_order_relaxed);
      else
        free_bucket->_next_delta.store(
            (std::int32_t)(
                (keys_bucket +
                 keys_bucket->_first_delta.load(std::memory_order_relaxed)) -
                free_bucket),
            std::memory_order_relaxed);
      keys_bucket->_first_delta.store((std::int32_t)(free_bucket - keys_bucket),
                                      std::memory_order_relaxed);
    }
  }

  void add_key_to_end_of_list(Bucket *const keys_bucket,
                              Bucket *const free_bucket, const K &key,
                              Bucket *const last_bucket) {
    free_bucket->_key.store(key, std::memory_order_relaxed);
    free_bucket->_next_delta.store(_NULL_DELTA, std::memory_order_relaxed);

    if (NULL == last_bucket)
      keys_bucket->_first_delta.store((std::int32_t)(free_bucket - keys_bucket),
                                      std::memory_order_relaxed);
    else
      last_bucket->_next_delta.store((std::int32_t)(free_bucket - last_bucket),
                                     std::memory_order_relaxed);
  }

  void optimize_cacheline_use(Segment &segment, Bucket *const free_bucket) {
    Bucket *const start_cacheline_bucket =
        get_start_cacheline_bucket(free_bucket);
    Bucket *const end_cacheline_bucket(start_cacheline_bucket + _cache_mask);
    Bucket *opt_bucket = start_cacheline_bucket;

    do {
      if (_NULL_DELTA !=
          opt_bucket->_first_delta.load(std::memory_order_relaxed)) {
        Bucket *relocate_key_last(NULL);
        int curr_delta(
            opt_bucket->_first_delta.load(std::memory_order_relaxed));
        Bucket *relocate_key(opt_bucket + curr_delta);
        do {
          if (curr_delta < 0 || curr_delta > _cache_mask) {
            free_bucket->_key.store(
                relocate_key->_key.load(std::memory_order_relaxed),
                std::memory_order_relaxed);
            free_bucket->_hash.store(
                relocate_key->_hash.load(std::memory_order_relaxed),
                std::memory_order_release);

            if (_NULL_DELTA ==
                relocate_key->_next_delta.load(std::memory_order_relaxed))
              free_bucket->_next_delta.store(_NULL_DELTA,
                                             std::memory_order_relaxed);
            else
              free_bucket->_next_delta.store(
                  (std::int32_t)((relocate_key + relocate_key->_next_delta) -
                                 free_bucket),
                  std::memory_order_relaxed);

            if (NULL == relocate_key_last)
              opt_bucket->_first_delta.store(
                  (std::int32_t)(free_bucket - opt_bucket),
                  std::memory_order_relaxed);
            else
              relocate_key_last->_next_delta.store(
                  (std::int32_t)(free_bucket - relocate_key_last),
                  std::memory_order_relaxed);

            ++(segment._timestamp);
            relocate_key->_key.store(KT::NullKey, std::memory_order_relaxed);
            relocate_key->_next_delta.store(_NULL_DELTA,
                                            std::memory_order_relaxed);
            relocate_key->_hash.store(0, std::memory_order_release);
            return;
          }

          if (_NULL_DELTA ==
              relocate_key->_next_delta.load(std::memory_order_relaxed))
            break;
          relocate_key_last = relocate_key;
          curr_delta +=
              relocate_key->_next_delta.load(std::memory_order_relaxed);
          relocate_key +=
              relocate_key->_next_delta.load(std::memory_order_relaxed);
        } while (true); // for on list
      }
      ++opt_bucket;
    } while (opt_bucket <= end_cacheline_bucket);
  }

public
    : // Ctors ................................................................
  HopscotchHashSet(
      std::uint32_t inCapacity = 32 * 1024, // init capacity
      std::uint32_t concurrencyLevel =
          std::thread::hardware_concurrency(), // num of updating threads
      std::uint32_t cache_line_size = 64,      // Cache-line size of machine
      bool is_optimize_cacheline = true)
      : _cache_mask((cache_line_size / sizeof(Bucket)) - 1),
        _is_cacheline_alignment(is_optimize_cacheline) {
    concurrencyLevel = nearest_power_of_two(concurrencyLevel);

    std::size_t num_timestamps = concurrencyLevel;

    std::uint8_t num_timestamp_bits = 0;
    for (std::size_t timestamp = num_timestamps; timestamp > 0;
         timestamp >>= 1, num_timestamp_bits++) {
    }
    std::uint8_t num_size_bits = 0;
    for (std::size_t size = inCapacity; size > 0; size >>= 1, num_size_bits++) {
    }
    std::uint8_t timestamp_shift = num_size_bits - num_timestamp_bits;
    m_segment_shift = timestamp_shift;

    // ADJUST INPUT ............................
    const std::uint32_t adjInitCap = (inCapacity);
    m_size_mask = inCapacity - 1;
    //    const std::uint32_t adjConcurrencyLevel =
    //        NearestPowerOfTwo(concurrencyLevel);
    const std::uint32_t num_buckets(adjInitCap + _INSERT_RANGE + 1);
    // ALLOCATE THE SEGMENTS ...................
    _segments = static_cast<CacheAligned<Segment> *>(
        Allocator::malloc(sizeof(CacheAligned<Segment>) * concurrencyLevel));
    _table =
        static_cast<Bucket *>(Allocator::malloc(sizeof(Bucket) * num_buckets));

    CacheAligned<Segment> *curr_seg = _segments;
    for (std::uint32_t iSeg = 0; iSeg < concurrencyLevel; ++iSeg, ++curr_seg) {
      curr_seg->init();
    }

    Bucket *curr_bucket = _table;
    for (std::uint32_t iElm = 0; iElm < num_buckets; ++iElm, ++curr_bucket) {
      curr_bucket->init();
    }
  }

  ~HopscotchHashSet() {
    Allocator::free(_table);
    Allocator::free(_segments);
  }

  bool thread_init(const std::size_t thread_id) { return true; }

  // Query Operations .........................................................
  bool contains(const K &key, const std::size_t thread_id) {

    // CALCULATE HASH ..........................
    const unsigned int hash(KT::hash(key));

    // CHECK IF ALREADY CONTAIN ................
    const Segment &segment(_segments[(hash & m_size_mask) >> m_segment_shift]);

    // go over the list and look for key
    unsigned int start_timestamp;
    do {
      start_timestamp = segment._timestamp;
      const Bucket *curr_bucket(&(_table[hash & m_size_mask]));
      std::int32_t next_delta(
          curr_bucket->_first_delta.load(std::memory_order_relaxed));
      while (_NULL_DELTA != next_delta) {
        curr_bucket += next_delta;
        if (key == curr_bucket->_key.load(std::memory_order_relaxed)) {
          return true;
        }
        next_delta = curr_bucket->_next_delta;
      }
    } while (start_timestamp != segment._timestamp);
    return false;
  }

  // modification Operations ...................................................
  inline bool add(const K &key, const std::size_t thread_id) {
    const unsigned int hash(KT::hash(key));
    //    Segment &segment(_segments[(hash & m_size_mask) >> m_segment_shift]);

    // go over the list and look for key
    std::lock_guard<Lock> guard(
        this->_segments[(hash & m_size_mask) >> m_segment_shift]._lock);
    Bucket *const start_bucket(&(_table[hash & m_size_mask]));

    Bucket *last_bucket(NULL);
    Bucket *compare_bucket = start_bucket;
    std::int32_t next_delta(
        compare_bucket->_first_delta.load(std::memory_order_relaxed));
    while (_NULL_DELTA != next_delta) {
      compare_bucket += next_delta;
      if (hash == compare_bucket->_hash.load(std::memory_order_acquire) &&
          key == compare_bucket->_key.load(std::memory_order_relaxed)) {
        return false;
      }
      last_bucket = compare_bucket;
      next_delta = compare_bucket->_next_delta.load(std::memory_order_relaxed);
    }

    // try to place the key in the same cache-line
    if (_is_cacheline_alignment) {
      Bucket *free_bucket = start_bucket;
      Bucket *start_cacheline_bucket = get_start_cacheline_bucket(start_bucket);
      Bucket *end_cacheline_bucket(start_cacheline_bucket + _cache_mask);
      do {
        std::size_t cur_hash =
            free_bucket->_hash.load(std::memory_order_acquire);
        if (0 == cur_hash) {
          if (!free_bucket->_hash.compare_exchange_weak(
                  cur_hash, hash, std::memory_order_acquire,
                  std::memory_order_acquire)) {
            continue;
          }
          add_key_to_begining_of_list(start_bucket, free_bucket, key);
          return true;
        }
        ++free_bucket;
        if (free_bucket > end_cacheline_bucket)
          free_bucket = start_cacheline_bucket;
      } while (start_bucket != free_bucket);
    }

    // place key in arbitrary free forward bucket
    Bucket *max_bucket(start_bucket + (INT_MAX - 1));
    Bucket *last_table_bucket(_table + m_size_mask);
    if (max_bucket > last_table_bucket)
      max_bucket = last_table_bucket;
    Bucket *free_max_bucket(start_bucket + (_cache_mask + 1));
    while (free_max_bucket <= max_bucket) {
      std::size_t cur_hash =
          free_max_bucket->_hash.load(std::memory_order_acquire);
      if (0 == cur_hash) {
        if (!free_max_bucket->_hash.compare_exchange_weak(
                cur_hash, hash, std::memory_order_acquire,
                std::memory_order_acquire)) {
          continue;
        }
        add_key_to_end_of_list(start_bucket, free_max_bucket, key, last_bucket);
        return true;
      }
      ++free_max_bucket;
    }

    // place key in arbitrary free backward bucket
    Bucket *min_bucket(start_bucket - (INT_MAX - 1));
    if (min_bucket < _table)
      min_bucket = _table;
    Bucket *free_min_bucket(start_bucket - (_cache_mask + 1));
    while (free_min_bucket >= min_bucket) {
      std::size_t cur_hash =
          free_min_bucket->_hash.load(std::memory_order_relaxed);
      if (0 == cur_hash) {
        if (!free_min_bucket->_hash.compare_exchange_weak(
                cur_hash, hash, std::memory_order_acquire,
                std::memory_order_acquire)) {
          continue;
        }
        add_key_to_end_of_list(start_bucket, free_min_bucket, key, last_bucket);
        return true;
      }
      --free_min_bucket;
    }

    // NEED TO RESIZE ..........................
    fprintf(stderr, "ERROR - RESIZE is not implemented - size %u\n", size());
    exit(1);
    return false;
  }

  inline bool remove(const K &key, const std::size_t thread_id) {
    // CALCULATE HASH ..........................
    const unsigned int hash(KT::hash(key));

    // CHECK IF ALREADY CONTAIN ................
    std::lock_guard<Lock> guard(
        this->_segments[(hash & m_size_mask) >> m_segment_shift]._lock);
    Bucket *const start_bucket = &_table[hash & m_size_mask];
    Bucket *last_bucket(NULL);
    Bucket *curr_bucket = start_bucket;
    std::int32_t next_delta =
        curr_bucket->_first_delta.load(std::memory_order_relaxed);
    do {
      if (_NULL_DELTA == next_delta) {
        return false;
      }
      curr_bucket += next_delta;

      if (hash == curr_bucket->_hash.load(std::memory_order_acquire) &&
          (key == curr_bucket->_key.load(std::memory_order_relaxed))) {
        remove_key(this->_segments[(hash & m_size_mask) >> m_segment_shift],
                   start_bucket, curr_bucket, last_bucket, hash);
        if (_is_cacheline_alignment)
          optimize_cacheline_use(
              this->_segments[(hash & m_size_mask) >> m_segment_shift],
              curr_bucket);
        return true;
      }
      last_bucket = curr_bucket;
      next_delta = curr_bucket->_next_delta.load(std::memory_order_relaxed);
    } while (true);
    return false;
  }

  // status Operations .........................................................
  unsigned int size() {
    std::uint32_t counter = 0;
    const std::uint32_t num_elm(m_size_mask + _INSERT_RANGE);
    for (std::uint32_t iElm = 0; iElm < num_elm; ++iElm) {
      if (0 != _table[iElm]._hash) {
        ++counter;
      }
    }
    return counter;
  }

  double percentKeysInCacheline() {
    unsigned int total_in_cache(0);
    unsigned int total(0);

    Bucket *curr_bucket(_table);
    for (int iElm(0); iElm <= m_size_mask; ++iElm, ++curr_bucket) {

      if (_NULL_DELTA != curr_bucket->_first_delta) {
        Bucket *const startCacheLineBucket =
            get_start_cacheline_bucket(curr_bucket);
        Bucket *check_bucket = curr_bucket + curr_bucket->_first_delta;
        int currDist(curr_bucket->_first_delta);
        do {
          ++total;
          if ((check_bucket - startCacheLineBucket) >= 0 &&
              (check_bucket - startCacheLineBucket) <= _cache_mask)
            ++total_in_cache;
          if (_NULL_DELTA == check_bucket->_next_delta)
            break;
          currDist += check_bucket->_next_delta;
          check_bucket += check_bucket->_next_delta;
        } while (true);
      }
    }

    // return percent in cache
    return (((double)total_in_cache) / ((double)total) * 100.0);
  }

  void print_table() {}

private:
  // Private Static Utilities .................................................
  static std::uint32_t NearestPowerOfTwo(const std::uint32_t value) {
    std::uint32_t rc(1);
    while (rc < value) {
      rc <<= 1;
    }
    return rc;
  }

  static unsigned int CalcDivideShift(const unsigned int _value) {
    unsigned int numShift(0);
    unsigned int curr(1);
    while (curr < _value) {
      curr <<= 1;
      ++numShift;
    }
    return numShift;
  }
};

template <class Allocator, template <class> class Reclaimer, class K>
using SpinLockHopscotchSet =
    HopscotchHashSet<Allocator, Reclaimer, PthreadSpinLock, K>;
template <class Allocator, template <class> class Reclaimer, class K>
using MuxtexHopscotchSet =
    HopscotchHashSet<Allocator, Reclaimer, PthreadMutex, K>;
}
