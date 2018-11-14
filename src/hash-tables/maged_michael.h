#pragma once

/*
Implementation of lock-free separate chaingin based on
"High performance dynamic lock-free hash tables and list-based sets."
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
class MagedMichael {
private:
  typedef Reclaimer<Allocator> MemReclaimer;
  typedef typename MemReclaimer::RecordHandle RecordHandle;
  typedef typename MemReclaimer::RecordBase RecordBase;

  struct Cell : public RecordBase {
    std::atomic<K> key;
    std::atomic<Cell *> next;
    Cell(const K &key) : key(key) {}

    static Cell *mark(const Cell *ptr) {
      return reinterpret_cast<Cell *>(
          (reinterpret_cast<std::size_t>(ptr) | 0x1));
    }

    static bool is_marked(const Cell *ptr) {
      return (reinterpret_cast<std::size_t>(ptr) & 0x1) == 1;
    }

    static Cell *get_ptr(const Cell *ptr) {
      return reinterpret_cast<Cell *>(
          (reinterpret_cast<std::size_t>(ptr) & ~0x1));
    }
  };

  class LinkedList {
    MemReclaimer *m_reclaimer;
    std::atomic<Cell *> m_head;

  public:
    void init(MemReclaimer *reclaimer) { m_reclaimer = reclaimer; }
    ~LinkedList() {
      Cell *current = m_head.load(std::memory_order_consume);
      while (current != nullptr) {
        Cell *next = current->next.load();
        m_reclaimer->free(Cell::get_ptr(current));
        current = next;
      }
    }

    struct ListVars {
      RecordHandle *h0, *h1, *h2;
      std::atomic<Cell *> *previous;
      Cell *current, *next;
      ListVars(RecordHandle *h0, RecordHandle *h1, RecordHandle *h2)
          : h0(h2), h1(h1), h2(h2), previous(nullptr), current(nullptr),
            next(nullptr) {}
    };

    enum SearchResult {
      Found,
      NotFound,
    };

    SearchResult search(ListVars &vars, const K &key,
                        ReclaimerPin<MemReclaimer> &pin) {
    try_again:
      vars.previous = &m_head;
      vars.current = vars.previous->load(std::memory_order_consume);
      if (!vars.h1->try_protect(vars.current, m_head,
                                [](Cell *ptr) { return Cell::get_ptr(ptr); })) {
        goto try_again;
      }

      while (true) {
        if (Cell::get_ptr(vars.current) == nullptr) {
          return NotFound;
        }
        vars.next =
            Cell::get_ptr(vars.current)->next.load(std::memory_order_consume);
        bool is_cur_marked = Cell::is_marked(vars.next);
        Cell *next = Cell::get_ptr(vars.next);
        if (!vars.h0->try_protect(
                vars.next, Cell::get_ptr(vars.current)->next,
                [](Cell *ptr) { return Cell::get_ptr(ptr); })) {
          goto try_again;
        }
        K current_key =
            Cell::get_ptr(vars.current)->key.load(std::memory_order_relaxed);
        if (vars.previous->load(std::memory_order_consume) !=
            Cell::get_ptr(vars.current)) {
          goto try_again;
        }
        if (!is_cur_marked) {
          if (current_key >= key) {
            return current_key == key ? Found : NotFound;
          }
        } else {
          if (vars.previous->compare_exchange_weak(vars.current, next,
                                                   std::memory_order_release,
                                                   std::memory_order_relaxed)) {
            pin.retire(*vars.h1);
          } else {
            goto try_again;
          }
        }
        vars.previous = &Cell::get_ptr(vars.current)->next;
        vars.h2->set(vars.current);
        vars.current = vars.next;
        vars.h1->set(vars.next);
      }
    }

    bool find(const K &key, ReclaimerPin<MemReclaimer> &pin) {
      RecordHandle h0 = pin.get_rec(), h1 = pin.get_rec(), h2 = pin.get_rec();
      ListVars vars(&h0, &h1, &h2);
      return this->search(vars, key, pin) == Found;
    }

    bool add(Cell *cell, const K &key, ReclaimerPin<MemReclaimer> &pin) {
      while (true) {
        RecordHandle h0 = pin.get_rec(), h1 = pin.get_rec(), h2 = pin.get_rec();
        ListVars vars(&h0, &h1, &h2);
        SearchResult res = this->search(vars, key, pin);
        if (res == Found) {
          return false;
        }
        Cell *current = vars.current;
        cell->next.store(current);
        if (vars.previous->compare_exchange_weak(current, cell,
                                                 std::memory_order_release,
                                                 std::memory_order_relaxed)) {
          return true;
        }
      }
    }

    bool remove(const K &key, ReclaimerPin<MemReclaimer> &pin) {
      while (true) {
        RecordHandle h0 = pin.get_rec(), h1 = pin.get_rec(), h2 = pin.get_rec();
        ListVars vars(&h0, &h1, &h2);
        SearchResult res = this->search(vars, key, pin);
        if (res == NotFound) {
          return false;
        }
        Cell *next = vars.next;
        Cell *current = vars.current;
        if (!current->next.compare_exchange_weak(next, Cell::mark(next),
                                                 std::memory_order_relaxed,
                                                 std::memory_order_relaxed)) {
          continue;
        }
        if (vars.previous->compare_exchange_weak(current, Cell::get_ptr(next),
                                                 std::memory_order_release,
                                                 std::memory_order_relaxed)) {
          pin.retire(*vars.h1);
        } else {
          res = this->search(vars, key, pin);
        }
        return true;
      }
    }
  };

  MemReclaimer m_reclaimer;
  std::size_t m_size, m_size_mask;
  LinkedList *m_table;

public:
  MagedMichael(const std::size_t size, const std::size_t threads)
      : m_reclaimer(threads, 3), m_size(nearest_power_of_two(size)),
        m_size_mask(m_size - 1),
        m_table(static_cast<LinkedList *>(
            Allocator::malloc(sizeof(LinkedList) * m_size))) {
    for (std::size_t i = 0; i < m_size; i++) {
      m_table[i].init(&m_reclaimer);
    }
  }
  ~MagedMichael() {
    for (std::size_t i = 0; i < m_size; i++) {
      m_table[i].~LinkedList();
    }
    Allocator::free(m_table);
  }

  bool thread_init(const std::size_t thread_id) {
    return m_reclaimer.thread_init(thread_id);
  }

  bool contains(const K &key, const std::size_t thread_id) {
    ReclaimerPin<MemReclaimer> pin(&m_reclaimer, thread_id);
    std::size_t size_mask = m_size - 1;
    std::size_t i = KT::hash(key) & size_mask;
    return m_table[i].find(key, pin);
  }

  bool add(const K &key, const std::size_t thread_id) {
    ReclaimerPin<MemReclaimer> pin(&m_reclaimer, thread_id);
    std::size_t i = KT::hash(key) & m_size_mask;
    Cell *cell = new (m_reclaimer.malloc(sizeof(Cell))) Cell(key);
    bool ans = m_table[i].add(cell, key, pin);
    if (!ans) {
      m_reclaimer.free(cell);
    }
    return ans;
  }

  bool remove(const K &key, const std::size_t thread_id) {
    ReclaimerPin<MemReclaimer> pin(&m_reclaimer, thread_id);
    std::size_t i = KT::hash(key) & m_size_mask;
    return m_table[i].remove(key, pin);
  }

  void print_table() {}
};
} // namespace concurrent_data_structures
