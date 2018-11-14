#pragma once

/*
Collection of hash table names
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

#include <string>

namespace concurrent_data_structures {

enum class HashTable {
  RH_BROWN_SET,
  TRANS_ROBIN_HOOD_SET,
  HOPSCOTCH_SET,
  LOCK_FREE_LINEAR_PROBING_NODE_SET,
  MAGED_MICHAEL,
};

const std::string get_table_name(const HashTable table);
}
