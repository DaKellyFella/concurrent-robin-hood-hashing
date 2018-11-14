project(concurrent_data_structurres)
cmake_minimum_required(VERSION 3.1)

set(CMAKE_C_COMPILER gcc)
set(CMAKE_CXX_COMPILER g++)
set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake;${CMAKE_MODULE_PATH}")
set(CMAKE_CXX_STANDARD 11)

include_directories(.)

find_package(Threads)
find_package(PAPI REQUIRED)
FILE(GLOB_RECURSE HeaderCFiles "*.h")

add_definitions(-D__cplusplus=201103L)


aux_source_directory(allocators COMMON_LIST)
aux_source_directory(bench COMMON_LIST)
aux_source_directory(hash-tables HASH_TABLE)
aux_source_directory(mem-reclaimer COMMON_LIST)
aux_source_directory(primitives COMMON_LIST)


set(HASH_TABLE_EXE "concurrent_hash_tables")
add_executable(${HASH_TABLE_EXE} ${HASH_TABLE} ${COMMON_LIST} ${HeaderCFiles})

if(CMAKE_COMPILER_IS_GNUCC OR CMAKE_COMPILER_IS_GNUCXX)
    # Update if necessary
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mrtm -Wall -Wno-long-long -pedantic -latomic -flto")
endif()

TARGET_LINK_LIBRARIES(${HASH_TABLE_EXE} ${CMAKE_THREAD_LIBS_INIT} ${PAPI_LIBRARIES} -ljemalloc -lrt -lcpuinfo -ltbbmalloc)
