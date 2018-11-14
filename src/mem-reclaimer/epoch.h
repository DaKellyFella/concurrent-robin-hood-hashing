#pragma once

/*
Simple epoch-based memory reclamation.
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

#include "mem-reclaimer/reclaimer.h"
#include "primitives/cache_utils.h"
#include <atomic>
#include <cassert>
#include <cstdint>
#include <vector>

namespace concurrent_data_structures {

template <class Allocator>
class EpochReclaimer : public ReclaimerAllocator<Allocator> {
public:
  class EpochBase {
    static EpochBase *mask(EpochBase *ptr) {
      return reinterpret_cast<EpochBase *>(reinterpret_cast<std::size_t>(ptr) &
                                           (~0x3));
    }
    friend class EpochReclaimer;
  };

private:
  static const std::size_t S_NUM_EPOCHS = 3;

  const std::size_t m_num_threads;
  std::atomic_size_t m_global_epoch;
  CacheAligned<std::atomic_size_t> *m_thread_epochs;
  std::vector<EpochBase *> **m_garbage_list;

  bool try_increment_epoch(const std::size_t current) {
    for (std::size_t t = 0; t < m_num_threads; t++) {
      const std::size_t thread_epoch = m_thread_epochs[t].load();
      if (thread_epoch != current) {
        return false;
      }
    }
    std::size_t temp_epoch = current;
    return m_global_epoch.compare_exchange_weak(temp_epoch, current + 1);
  }

  void clear_garbage(const std::size_t safe_epoch,
                     const std::size_t thread_id) {
    std::size_t index = safe_epoch % S_NUM_EPOCHS;
    for (std::size_t i = 0; i < m_garbage_list[thread_id][index].size(); i++) {
      Allocator::free(m_garbage_list[thread_id][index][i]);
    }
    //    this->frees.fetch_add(m_garbage_list[thread_id][index].size(),
    //                          std::memory_order_relaxed);
    m_garbage_list[thread_id][index].clear();
  }

public:
  class EpochHandle {
  private:
    EpochBase *m_ptr;

  public:
    EpochHandle(EpochBase *ptr) : m_ptr(EpochBase::mask(ptr)) {}
    ~EpochHandle() {}
    EpochHandle(const EpochHandle &rhs) = delete;
    EpochHandle &operator=(const EpochHandle &rhs) = delete;
    EpochHandle(EpochHandle &&rhs) : m_ptr(rhs.m_ptr) { rhs.m_ptr = nullptr; }
    EpochHandle &operator=(EpochHandle &&rhs) {
      std::swap(m_ptr, rhs.m_ptr);
      return *this;
    }

    void set(EpochBase *ptr) { m_ptr = EpochBase::mask(ptr); }

    template <class PtrType, class SourceType, typename Func>
    bool try_protect(PtrType &ptr, const std::atomic<SourceType> &src, Func f) {
      m_ptr = EpochBase::mask(ptr);
      return true;
    }
    template <class PtrType>
    bool try_protect(PtrType &ptr, const std::atomic<PtrType> &src) noexcept {
      return this->try_protect(ptr, src, [](PtrType ptr) { return ptr; });
    }

    template <class PtrType, class SourceType, typename Func>
    PtrType get_protected(const std::atomic<SourceType> &src, Func f) {
      PtrType ptr = f(src.load());
      while (!try_protect(ptr, src, f))
        ;
      return ptr;
    }

    template <class PtrType>
    PtrType get_protected(const std::atomic<PtrType> &src) noexcept {
      return this->get_protected(src, [](PtrType ptr) { return ptr; });
    }
    friend class EpochReclaimer;
  };

  typedef EpochBase RecordBase;
  typedef EpochHandle RecordHandle;

  EpochReclaimer(const std::size_t num_threads,
                 const std::size_t refs_per_thread)
      : m_num_threads(num_threads), m_global_epoch(3),
        m_thread_epochs(
            static_cast<CacheAligned<std::atomic_size_t> *>(Allocator::malloc(
                sizeof(CacheAligned<std::atomic_size_t>) * num_threads))),
        m_garbage_list(
            static_cast<std::vector<EpochBase *> **>(Allocator::malloc(
                sizeof(std::vector<EpochBase *> *) * num_threads))) {
    for (std::size_t t = 0; t < num_threads; t++) {
      m_garbage_list[t] = static_cast<std::vector<EpochBase *> *>(
          Allocator::malloc(sizeof(std::vector<EpochBase *>) * S_NUM_EPOCHS));
      m_thread_epochs[t].store(3, std::memory_order_relaxed);
      for (std::size_t e = 0; e < S_NUM_EPOCHS; e++) {
        new (&m_garbage_list[t][e]) std::vector<EpochBase *>();
        m_garbage_list[t][e].reserve(200);
      }
    }
  }
  ~EpochReclaimer() {
    std::cout << "Before: " << this->mallocs.load() << " " << this->frees.load()
              << " " << (this->mallocs.load() == this->frees.load())
              << std::endl;
    for (std::size_t t = 0; t < m_num_threads; t++) {
      for (std::size_t e = 0; e < S_NUM_EPOCHS; e++) {
        //        this->frees.fetch_add(m_garbage_list[t][e].size(),
        //                              std::memory_order_relaxed);
        for (std::size_t j = 0; j < m_garbage_list[t][e].size(); j++) {
          Allocator::free(m_garbage_list[t][e][j]);
        }
      }
      Allocator::free(m_garbage_list[t]);
    }
    std::cout << "After: " << this->mallocs.load() << " " << this->frees.load()
              << " " << (this->mallocs.load() == this->frees.load())
              << std::endl;
    Allocator::free(m_garbage_list);
    Allocator::free(m_thread_epochs);
  }

  bool thread_init(const std::size_t thread_id) { return true; }

  void enter(const std::size_t thread_id) {
    const std::size_t epoch = m_thread_epochs[thread_id].load();
    const std::size_t global_epoch = m_global_epoch.load();
    if (epoch != global_epoch) {
      assert(global_epoch - epoch == 1);
      clear_garbage(global_epoch, thread_id);
      m_thread_epochs[thread_id].store(global_epoch);
    }
  }

  void exit(const std::size_t thread_id) {
    const std::size_t epoch = m_thread_epochs[thread_id].load();
    const std::size_t global_epoch = m_global_epoch.load();
    if (epoch == global_epoch) {
      try_increment_epoch(global_epoch);
    }
  }

  EpochHandle get_rec(const std::size_t thread_id) {
    return EpochHandle(nullptr);
  }

  void retire(const EpochHandle &handle, const std::size_t thread_id) {
    const std::size_t epoch = m_thread_epochs[thread_id].load();
    m_garbage_list[thread_id][epoch % S_NUM_EPOCHS].push_back(handle.m_ptr);
  }
};
}
