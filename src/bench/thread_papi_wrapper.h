#pragma once

/*
A wrapper to collect per-thread info in PAPI.
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

#include <cassert>
#include <cstdint>
#include <functional>
#include <papi.h>
#include <string>
#include <thread>

namespace concurrent_data_structures {

struct PAPI_EVENTS {
  enum EVENTS {
    L1_CACHE_MISSES = 0,
    L2_CACHE_MISSES = 1,
    INSTRUCTION_STALLS = 2,
    TOTAL_INSTRUCTIONS = 3,
    L1_DATA_CACHE_MISSES = 4,
    TOTAL_PAPI_EVENTS = 5,
  };
  static const std::string get_event_name(EVENTS event);
};

struct PapiCounters {
  long long counters[PAPI_EVENTS::TOTAL_PAPI_EVENTS];
  PapiCounters();
};

class ThreadPapiWrapper {
private:
  bool m_active;

public:
  ThreadPapiWrapper(bool active);

  bool start();
  bool stop(PapiCounters &counters);
};
}
