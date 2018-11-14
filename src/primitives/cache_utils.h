#pragma once


/*
Some utilities to cache align classes and structs.
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


#include <cstdint>
namespace concurrent_data_structures {

static const std::size_t S_CACHE_ALIGNMENT = 128;

namespace {

template <class Object> class DummyCacheAligned : public Object {
private:
  char padding[S_CACHE_ALIGNMENT - (sizeof(Object) % S_CACHE_ALIGNMENT)];

public:
  template <typename... _Args>
  DummyCacheAligned(_Args &&... __args) : Object(__args...) {}
};
}

template <class Object>
class alignas(alignof(DummyCacheAligned<Object>) > S_CACHE_ALIGNMENT
                  ? alignof(DummyCacheAligned<Object>)
                  : S_CACHE_ALIGNMENT) CacheAligned : public Object {
private:
  char padding[S_CACHE_ALIGNMENT - (sizeof(Object) % S_CACHE_ALIGNMENT)];

public:
  template <typename... _Args>
  CacheAligned(_Args &&... __args) : Object(__args...) {}
};
}
