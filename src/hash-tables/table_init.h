#pragma once
#include "bench/benchmark_config.h"
#include "random/pcg_random.h"
#include <algorithm>
#include <random>

namespace concurrent_data_structures {

template <class Table, class Key>
static Table *TableInit(const SetBenchmarkConfig &config) {
  Table *table = new Table(config.table_size, config.base.num_threads);
  std::size_t amount =
      static_cast<std::size_t>(config.table_size * config.load_factor);
  std::vector<Key> keys(config.table_size);
  for (std::size_t i = 0; i < keys.size(); i++) {
    keys[i] = i;
  }
  pcg32 random(0); // pcg_extras::seed_seq_from<std::random_device>()
  std::shuffle(keys.begin(), keys.end(), random);
  for (std::size_t i = 0; i < amount; i++) {
    //    std::cout << "Adding key: " << keys[i] << std::endl;
    if (!table->add(keys[i], i % config.base.num_threads)) {
      // Shouldn't be here anymore...
      assert(false);
      i--;
    }
  }
  return table;
}
}
