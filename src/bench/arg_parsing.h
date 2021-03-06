#pragma once

/*
Arg parser for main program.
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

#include "allocators.h"
#include "benchmark_config.h"
#include "mem-reclaimer/reclaimer.h"
#include "table.h"
#include <cstdint>
#include <getopt.h>
#include <map>

namespace concurrent_data_structures {

SetBenchmarkConfig parse_set_args(std::int32_t argc, char *argv[]);
void set_print_help_and_exit();
} // namespace concurrent_data_structures
