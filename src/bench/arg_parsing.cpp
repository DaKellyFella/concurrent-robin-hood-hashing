#include "arg_parsing.h"
#include <iostream>

/*
Body for parsing args to main.
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

extern int optind;
extern char *optarg;

namespace concurrent_data_structures {

namespace {
static const std::map<std::string, HashTable> table_map{
    std::make_pair("rh_brown_set", HashTable::RH_BROWN_SET),
    std::make_pair("trans_rh_set", HashTable::TRANS_ROBIN_HOOD_SET),
    std::make_pair("hopscotch_set", HashTable::HOPSCOTCH_SET),
    std::make_pair("lf_lp_node_set",
                   HashTable::LOCK_FREE_LINEAR_PROBING_NODE_SET),
    std::make_pair("mm_set", HashTable::MAGED_MICHAEL),
};

static const std::map<std::string, Reclaimer> reclaimer_map{
    std::make_pair("leaky", Reclaimer::Leaky),
    std::make_pair("epoch", Reclaimer::Epoch),
};

static const std::map<std::string, Allocator> allocator_map{
    std::make_pair("je", Allocator::JeMalloc),
    std::make_pair("glibc", Allocator::Glibc),
    std::make_pair("intel", Allocator::Intel),
};

enum class BenchmarkType { Set };
} // namespace

bool parse_base_arg(BenchmarkConfig &base, const int current_option, char *arg,
                    BenchmarkType type) {
  switch (current_option) {
  case 'D':
    base.duration = std::chrono::seconds(std::atoi(arg));
    return true;
  case 'T':
    base.num_threads = std::size_t(std::atoi(arg));
    return true;
  case 'M': {
    auto reclaimer_res = reclaimer_map.find(std::string(optarg));
    if (reclaimer_map.end() == reclaimer_res) {
      std::cout << "Invalid reclaimer choice." << std::endl;
      set_print_help_and_exit();
    } else {
      base.reclaimer = reclaimer_res->second;
    }
  }
    return true;
  case 'A': {
    auto allocator_res = allocator_map.find(std::string(optarg));
    if (allocator_map.end() == allocator_res) {
      std::cout << "Invalid allocator choice." << std::endl;
      set_print_help_and_exit();
    } else {
      base.allocator = allocator_res->second;
    }
  }
    return true;
  case 'P':
    base.papi_active = std::string(optarg) == "true";
    return true;
  case 'V':
    base.verify = std::string(optarg) == "true";
    return true;
  case 'H':
    base.hyperthreading = std::string(optarg) == "true";
    return true;
  }
  return false;
}

SetBenchmarkConfig parse_set_args(std::int32_t argc, char *argv[]) {
  SetBenchmarkConfig config = {
      BenchmarkConfig{1, std::chrono::seconds(1), Reclaimer::Leaky,
                      Allocator::JeMalloc, true, false, true},
      1 << 23, 10, 0.4, HashTable::RH_BROWN_SET};
  int current_option;
  while ((current_option = getopt(argc, argv, ":L:S:D:T:U:B:M:P:V:A:H:")) !=
         -1) {
    if (parse_base_arg(config.base, current_option, optarg,
                       BenchmarkType::Set)) {
      continue;
    }
    switch (current_option) {
    case 'L':
      config.load_factor = std::stod(optarg);
      break;
    case 'S':
      config.table_size = std::size_t(1 << std::atoi(optarg));
      break;
    case 'U':
      config.updates = std::size_t(std::atoi(optarg));
      break;
    case 'B': { // C++
      auto table_res = table_map.find(std::string(optarg));
      if (table_map.end() == table_res) {
        std::cout << "Invalid benchmark choice." << std::endl;
        set_print_help_and_exit();
      } else {
        config.table = table_res->second;
      }
    } break;
    case 'H':
    case ':':
    default:
      set_print_help_and_exit();
    }
  }
  return config;
}

void set_print_help_and_exit() {
  std::cout
      << "L: Load Factor. Default = 40%.\n"
      << "S: Power of two size. Default = 1 << 23.\n"
      << "D: Duration of benchmark in seconds. Default = 1 second.\n"
      << "T: Number of concurrent threads. Default = 1.\n"
      << "U: Updates as a percentage of workload. Default = 10%.\n"
      << "B: Table being benchmarked. Default = rh_brown_set.\n"
      << "M: Memory reclaimer using within table (if needed). Default = None.\n"
      << "A: Allocator used within the table. Default = JeMalloc.\n"
      << "P: Whether PAPI is turned on or not. Default = True.\n"
      << "H: Whether to employ HT or move to new socket. Default = True.\n"
      << "V: Whether to run the tests on the table. Default = False."
      << std::endl;
  exit(0);
}

} // namespace concurrent_data_structures
