#pragma once

/*
Implementation of Lock-Free Linear probing based on
"A scalable lock-free hash table with open address-ing.February 2016."
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
#include "mem-reclaimer/reclaimer.h"
#include <atomic>

namespace concurrent_data_structures {

template <class Allocator, template <class> class Reclaimer, class K,
          class KT = KeyTraits<K>>
class LockFreeLinearProbingNodeSet {

private:
  typedef Reclaimer<Allocator> MemReclaimer;
  typedef typename MemReclaimer::RecordHandle RecordHandle;
  typedef typename MemReclaimer::RecordBase RecordBase;

  struct Cell : public RecordBase {
    std::atomic<K> key;
    Cell(const K &key) : key(key) {}
    Cell() : Cell(KT::NullKey) {}

    static Cell *flag(const Cell *ptr) {
      return reinterpret_cast<Cell *>(
          (reinterpret_cast<std::size_t>(ptr) | 0x1));
    }

    static bool is_flagged(const Cell *ptr) {
      return (reinterpret_cast<std::size_t>(ptr) & 0x1) == 1;
    }

    static Cell *get_ptr(const Cell *ptr) {
      return reinterpret_cast<Cell *>(
          (reinterpret_cast<std::size_t>(ptr) & ~0x1));
    }
  };

  MemReclaimer m_reclaimer;
  std::atomic<std::size_t> m_size;
  std::atomic<std::size_t> m_size_mask;
  std::atomic<Cell *> *m_table;
  Cell *TOMBSTONE;

  Cell *upgrade(std::size_t original_slot, const K &upgrade_key,
                ReclaimerPin<MemReclaimer> &pin,
                RecordHandle &original_handle) {
    std::size_t size_mask = m_size_mask.load(std::memory_order_relaxed);
    bool found_non_flagged = false;
    bool found_closest_flagged = false;
    std::size_t closest_flagged_slot = 0;
    Cell *actual_cell = TOMBSTONE;

    RecordHandle closest_handle = pin.get_rec();

    for (std::size_t i = original_slot;; i++) {
      i &= size_mask;
    loadBegin:
      RecordHandle cur_handle = pin.get_rec();
      Cell *current_cell = m_table[i].load(std::memory_order_consume);
      if (!cur_handle.try_protect(current_cell, m_table[i], [](Cell *ptr) {
            return Cell::get_ptr(ptr);
          })) {
        goto loadBegin;
      }

      if (current_cell == nullptr) {
        // Commit phase
        if (found_non_flagged) {
          return actual_cell;
        } else if (found_closest_flagged) {
          Cell *to_commit =
              m_table[closest_flagged_slot].load(std::memory_order_consume);
          // It has been committed already
          if (to_commit == Cell::get_ptr(actual_cell)) {
            return Cell::get_ptr(actual_cell);
          }
          // It was changed from underneath us
          if (Cell::get_ptr(to_commit) != Cell::get_ptr(actual_cell)) {
            return TOMBSTONE;
          }
          // Try commit
          if (m_table[closest_flagged_slot].compare_exchange_weak(
                  to_commit, Cell::get_ptr(actual_cell))) {
            return Cell::get_ptr(actual_cell);
          }
          // See if someone committed it for us, or deleted it
          if (to_commit != Cell::get_ptr(actual_cell)) {
            return TOMBSTONE;
          } else {
            return to_commit;
          }
        }
      } else if (current_cell == TOMBSTONE) {
        continue;
      } else if (Cell::is_flagged(current_cell)) {
        Cell *potential_cell = Cell::get_ptr(current_cell);
        if (potential_cell->key.load(std::memory_order_relaxed) ==
            upgrade_key) {
          // We don't have a new slot, delete it.
          if (found_non_flagged) {
            if (m_table[i].compare_exchange_weak(current_cell, TOMBSTONE,
                                                 std::memory_order_relaxed)) {
              pin.retire(cur_handle);
            }
          } else if (!found_closest_flagged) { // Or we found our new best
            if (!closest_handle.try_protect(
                    current_cell, m_table[i],
                    [](Cell *ptr) { return Cell::get_ptr(ptr); })) {
              goto loadBegin;
            }
            closest_flagged_slot = i;
            found_closest_flagged = true;
            actual_cell = current_cell;
          } else {
            Cell *best =
                m_table[closest_flagged_slot].load(std::memory_order_consume);
            // Nothing has changed, remove second best
            if (best == actual_cell) {
              if (m_table[i].compare_exchange_weak(current_cell, TOMBSTONE,
                                                   std::memory_order_relaxed)) {
                pin.retire(cur_handle);
              }
            } else {
              return TOMBSTONE;
            }
          }
        }
      } else {
        K current_key = current_cell->key.load(std::memory_order_relaxed);
        if (current_key == upgrade_key) {
          found_non_flagged = true;
          if (found_closest_flagged) {
            Cell *to_remove =
                m_table[closest_flagged_slot].load(std::memory_order_relaxed);
            if (to_remove == TOMBSTONE)
              continue;
            if (Cell::get_ptr(to_remove)->key.load(std::memory_order_relaxed) !=
                upgrade_key)
              continue;
            if (m_table[closest_flagged_slot].compare_exchange_weak(
                    actual_cell, TOMBSTONE, std::memory_order_relaxed)) {
              pin.retire(closest_handle);
            }
            actual_cell = current_cell;
            // Removed the best one so far
            found_closest_flagged = false;
          }
        }
      }
    }
  }

public:
  LockFreeLinearProbingNodeSet(const std::size_t size,
                               const std::size_t threads)
      : m_reclaimer(threads, 3), m_size(nearest_power_of_two(size)),
        m_size_mask(m_size.load(std::memory_order_relaxed) - 1),
        m_table(static_cast<std::atomic<Cell *> *>(
            Allocator::malloc(m_size.load(std::memory_order_relaxed) *
                              sizeof(std::atomic<Cell *>)))) {
    K nk = KT::NullKey;
    TOMBSTONE = new (Allocator::malloc(sizeof(Cell))) Cell(nk);
  }

  ~LockFreeLinearProbingNodeSet() {
    std::size_t size = m_size.load(std::memory_order_relaxed);
    for (std::size_t i = 0; i < size; i++) {
      Cell *current_cell = m_table[i].load(std::memory_order_relaxed);
      if (current_cell != nullptr and current_cell != TOMBSTONE) {
        m_reclaimer.free(Cell::get_ptr(current_cell));
      }
    }
    Allocator::free(TOMBSTONE);
    Allocator::free(m_table);
  }

  bool thread_init(const std::size_t thread_id) {
    return m_reclaimer.thread_init(thread_id);
  }

  bool contains(const K &key, const std::size_t thread_id) {
    ReclaimerPin<MemReclaimer> pin(&m_reclaimer, thread_id);
    const std::size_t size_mask = m_size_mask.load(std::memory_order_relaxed);
    const std::size_t original_slot = KT::hash(key) & size_mask;

    for (std::size_t i = original_slot;; i++) {
      i &= size_mask;
    loadBegin:
      RecordHandle handle = pin.get_rec();
      Cell *current_cell = m_table[i].load(std::memory_order_consume);
      if (!handle.try_protect(current_cell, m_table[i],
                              [](Cell *ptr) { return Cell::get_ptr(ptr); })) {
        goto loadBegin;
      }
      if (current_cell == nullptr) {
        return false;
      } else if (current_cell == TOMBSTONE or Cell::is_flagged(current_cell)) {
        continue;
      }
      K current_key = current_cell->key.load(std::memory_order_relaxed);
      if (current_key == key) {
        return true;
      }
    }
  }

  bool add(const K &key, const std::size_t thread_id) {
    ReclaimerPin<MemReclaimer> pin(&m_reclaimer, thread_id);
    Cell *to_insert = new (m_reclaimer.malloc(sizeof(Cell))) Cell(key);
    const std::size_t size_mask = m_size_mask.load(std::memory_order_relaxed);
    const std::size_t original_slot = KT::hash(key) & size_mask;

    for (std::size_t i = original_slot;; i++) {
      i &= size_mask;

    loadBegin:
      RecordHandle handle = pin.get_rec();
      Cell *current_cell = m_table[i].load(std::memory_order_consume);
      if (!handle.try_protect(current_cell, m_table[i],
                              [](Cell *ptr) { return Cell::get_ptr(ptr); })) {
        goto loadBegin;
      }
      if (current_cell == nullptr or current_cell == TOMBSTONE) {
        // Attempt insert
        if (!m_table[i].compare_exchange_weak(current_cell,
                                              Cell::flag(to_insert))) {
          goto loadBegin;
        }
        current_cell = this->upgrade(original_slot, key, pin, handle);
        // Cell has been inserted and removed from under us.
        if (current_cell == TOMBSTONE) {
          return true;
        }
        return current_cell == to_insert;
      }
      const K current_key =
          Cell::get_ptr(current_cell)->key.load(std::memory_order_relaxed);
      if (current_key == key) {
        if (Cell::is_flagged(current_cell)) {
          current_cell = this->upgrade(original_slot, key, pin, handle);
        }
        m_reclaimer.free(to_insert);
        return false;
      }
    }
  }

  bool remove(const K &in_key, const std::size_t thread_id) {
    ReclaimerPin<MemReclaimer> pin(&m_reclaimer, thread_id);
    std::size_t size_mask = m_size_mask.load(std::memory_order_relaxed);
    std::size_t original_slot = KT::hash(in_key) & size_mask;

  loopBegin:
    for (std::size_t i = original_slot;; i++) {
      i &= size_mask;
    loadBegin:
      RecordHandle handle = pin.get_rec();
      Cell *current_cell = m_table[i].load(std::memory_order_consume);
      if (!handle.try_protect(current_cell, m_table[i],
                              [](Cell *ptr) { return Cell::get_ptr(ptr); })) {
        goto loadBegin;
      }
      if (current_cell == nullptr) {
        return false;
      } else if (current_cell == TOMBSTONE or Cell::is_flagged(current_cell)) {
        continue;
      }
      K current_key = current_cell->key.load(std::memory_order_relaxed);
      if (current_key == in_key) {
        if (m_table[i].compare_exchange_weak(current_cell, TOMBSTONE,
                                             std::memory_order_relaxed)) {
          pin.retire(handle);
          return true;
        } else {
          goto loopBegin;
        }
      }
    }
  }
  void print_table() {}
};
}
