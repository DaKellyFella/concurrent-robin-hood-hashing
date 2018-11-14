#pragma once


/*
Simple interface for locks with some implementations.
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



#include <atomic>
#include <cstdlib>
#include <immintrin.h>
#include <pthread.h>

namespace concurrent_data_structures {

class alignas(128) PthreadSpinLock {
private:
  pthread_spinlock_t m_lock;

public:
  PthreadSpinLock() {
    assert(pthread_spin_init(&m_lock, PTHREAD_PROCESS_SHARED) == 0);
  }
  void lock() { pthread_spin_lock(&m_lock); }
  void unlock() { pthread_spin_unlock(&m_lock); }
};

class alignas(128) PthreadMutex {

private:
  pthread_mutex_t m_lock;

public:
  PthreadMutex() { pthread_mutex_init(&m_lock, nullptr); }
  void lock() { pthread_mutex_lock(&m_lock); }
  void unlock() { pthread_mutex_unlock(&m_lock); }
};
}

class alignas(128) ElidedLock {
private:
  static const std::size_t MAX_RETRIES = 20;
  std::atomic_bool m_lock;

public:
  ElidedLock() { m_lock.store(false, std::memory_order_relaxed); }
  ElidedLock &operator=(const ElidedLock &rhs) {
    m_lock.store(rhs.m_lock.load(std::memory_order_relaxed),
                 std::memory_order_relaxed);
    return *this;
  }
  void lock() {
    for (std::size_t i = 0; i < MAX_RETRIES; i++) {
      unsigned int status = _xbegin();
      if (status == _XBEGIN_STARTED) {
        if (!m_lock.load(std::memory_order_relaxed)) {
          return;
        } else {
          _xabort(0xff);
        }
      }
      if ((status & _XABORT_EXPLICIT) && _XABORT_CODE(status) == 0xff) {
        // Wait for lock to be free.
        while (m_lock.load(std::memory_order_relaxed)) {
          _mm_pause();
        }
      }
      if (!(status & _XABORT_RETRY) and
          !((status & _XABORT_EXPLICIT) and _XABORT_CODE(status) == 0xff)) {
        break;
      }
    }

    while (true) {
      bool value = m_lock.load(std::memory_order_relaxed);
      if (!value and
          m_lock.compare_exchange_weak(value, true, std::memory_order_acquire,
                                       std::memory_order_relaxed)) {
        return;
      }
      _mm_pause();
    }
  }

  void unlock() {
    if (!m_lock.load(std::memory_order_relaxed) and _xtest()) {
      _xend();
    } else {
      m_lock.store(false, std::memory_order_release);
    }
  }
};
