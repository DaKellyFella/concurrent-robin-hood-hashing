#include "table.h"

/*
Mapping enums to string descriptions of hash tables.
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

#include <map>

namespace concurrent_data_structures {

namespace {
static const std::map<HashTable, std::string> table_map{
    std::make_pair(HashTable::RH_BROWN_SET, "Brown K-CAS Robin Hood Set"),
    std::make_pair(HashTable::TRANS_ROBIN_HOOD_SET,
                   "Transactional Robin Hood Set"),
    std::make_pair(HashTable::HOPSCOTCH_SET, "Hopscotch Hashing"),
    std::make_pair(HashTable::LOCK_FREE_LINEAR_PROBING_NODE_SET,
                   "Lock-Free Linear Probing Node"),
    std::make_pair(HashTable::MAGED_MICHAEL, "Maged Michael Separate Chaining"),
};
}

const std::string get_table_name(const HashTable table) {
  std::string table_name = "ERROR: Incorrect hash-table name.";
  auto it = table_map.find(table);
  if (it != table_map.end()) {
    table_name = it->second;
  }
  return table_name;
}
}
