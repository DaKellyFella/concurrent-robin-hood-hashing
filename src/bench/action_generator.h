#pragma once

/*
Generates random actions for benchmarking tools.
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

#include "benchmark_config.h"
#include "random/pcg_random.h"
#include <iostream>
#include <random>

namespace concurrent_data_structures {

enum class SetAction {
  Contains,
  Add,
  Remove,
};


template <class Key> class SetActionGenerator {
private:
  pcg32 m_random_generator;
  std::uniform_int_distribution<std::uint8_t> m_action_distribution;
  std::uniform_int_distribution<Key> m_data_distribution;
  const std::uint8_t m_read_limit, m_add_limit;

public:
  SetActionGenerator(const SetBenchmarkConfig &config)
      // If we want to run in Valgrind please remove.
      : m_random_generator(pcg_extras::seed_seq_from<std::random_device>()),
        m_action_distribution(0, 100),
        m_data_distribution(0, config.table_size - 1),
        m_read_limit(100 - config.updates),
        m_add_limit(m_read_limit + (config.updates / 2)) {}
  SetAction generate_action() {
    std::uint8_t action = m_action_distribution(m_random_generator);
    if (action <= m_read_limit) {
      return SetAction::Contains;
    } else if (action <= m_add_limit) {
      return SetAction::Add;
    } else {
      return SetAction::Remove;
    }
  }
  Key generate_key() { return m_data_distribution(m_random_generator); }
};

}
