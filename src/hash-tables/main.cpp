#undef NDEBUG
#include "allocators/glib_allocator.h"
#include "allocators/intel_allocator.h"
#include "allocators/jemalloc_allocator.h"
#include "bench/arg_parsing.h"
#include "bench/benchmark_config.h"
#include "bench/benchmark_summary.h"
#include "bench/benchmark_table.h"
#include "hash-tables/kcas_rh_set.h"
#include "hash-tables/locked_hopscotch.h"
#include "hash-tables/lockfree_linear_probe_node.h"
#include "hash-tables/maged_michael.h"
#include "hash-tables/transactional_robin_hood_set.h"
#include "mem-reclaimer/epoch.h"
#include "mem-reclaimer/leaky.h"
#include <atomic>
#include <cassert>
#include <cstdint>
#include <sstream>
#include <thread>

using namespace concurrent_data_structures;

namespace {

// https://stackoverflow.com/a/3418285
void replaceAll(std::string &str, const std::string &from,
                const std::string &to) {
  if (from.empty())
    return;
  size_t start_pos = 0;
  while ((start_pos = str.find(from, start_pos)) != std::string::npos) {
    str.replace(start_pos, from.length(), to);
    start_pos += to.length(); // In case 'to' contains 'from', like replacing
                              // 'x' with 'yx'
  }
}
} // namespace

const std::string generate_file_name(const SetBenchmarkConfig &config) {
  std::stringstream file_name;
  file_name << std::string("Table:") + get_table_name(config.table) +
                   std::string(" Reclaimer:") +
                   get_reclaimer_name(config.base.reclaimer)
            << " A:" << get_allocator_name(config.base.allocator)
            << " T:" << config.base.num_threads << " S:" << config.table_size
            << " U:" << config.updates << " L:" << config.load_factor
            << std::string(".txt");
  std::string human_file_name = file_name.str();
  replaceAll(human_file_name, " ", "_");
  return human_file_name;
}

// Sick
template <template <class, template <class> class, class...> class Table,
          class Allocator, template <class> class Reclaimer>
bool run_and_save(const SetBenchmarkConfig &config) {
  typedef Table<Allocator, Reclaimer, std::size_t> HashTable;
  if (!config.base.verify) {
    TableBenchmark<HashTable, std::size_t> benchmark(config);
    produce_summary(config, benchmark.bench(), generate_file_name(config),
                    "set_keys.csv", "set_results.csv");
  }
  if (config.base.verify) {
    TableBenchmark<HashTable, std::size_t> benchmark(config);
    assert(benchmark.test());
  }
  return true;
}

template <class Allocator, template <class> class Reclaimer>
bool fix_table(const SetBenchmarkConfig &config) {
  switch (config.table) {
  case HashTable::RH_BROWN_SET:
    return run_and_save<RHSetBrownKCAS, Allocator, Reclaimer>(config);
  case HashTable::TRANS_ROBIN_HOOD_SET:
    return run_and_save<TransactionalRobinHoodSet, Allocator, Reclaimer>(
        config);
  case HashTable::HOPSCOTCH_SET:
    return run_and_save<SpinLockHopscotchSet, Allocator, Reclaimer>(config);
  case HashTable::LOCK_FREE_LINEAR_PROBING_NODE_SET:
    return run_and_save<LockFreeLinearProbingNodeSet, Allocator, Reclaimer>(
        config);
  case HashTable::MAGED_MICHAEL:
    return run_and_save<MagedMichael, Allocator, Reclaimer>(config);
  default:
    return false;
  }
}

template <template <class> class Reclaimer>
bool fix_allocator(const SetBenchmarkConfig &config) {
  switch (config.base.allocator) {
  case Allocator::Glibc:
    return fix_table<GlibcAllocator, Reclaimer>(config);
  case Allocator::JeMalloc:
    return fix_table<JeMallocAllocator, Reclaimer>(config);
  case Allocator::Intel:
    return fix_table<IntelAllocator, Reclaimer>(config);
  default:
    return false;
  }
}

bool run(const SetBenchmarkConfig &config) {
  switch (config.base.reclaimer) {
  case Reclaimer::Leaky:
    return fix_allocator<LeakyReclaimer>(config);
  case Reclaimer::Epoch:
    return fix_allocator<EpochReclaimer>(config);
  default:
    return false;
  }
}

std::int32_t main(std::int32_t argc, char *argv[]) {

  const SetBenchmarkConfig config = parse_set_args(argc, argv);
  if (config.base.papi_active) {
    assert(PAPI_library_init(PAPI_VER_CURRENT) == PAPI_VER_CURRENT and
           "Couldn't initialise PAPI library. Check installation.");
  }
  config.print(std::cout);
  if (!run(config)) {
    set_print_help_and_exit();
  }
  std::cout << "Finished." << std::endl;
  return 0;
}
