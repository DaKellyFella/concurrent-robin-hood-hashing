# Concurrent Robin Hood Hashing

## Tables present
1. K-CAS Robin Hood Hashing
2. Maged Michael Lock-Free Separate Chaining
3. Lock-Free Linear Probing
4. Transactional Lock-Elision Robin Hood Hashing
5. Locked Hopscotch Hashing

## Build instruction
These benchmarks require a number of dependencies.
* [A CPU topology library for smart thread pinning](https://github.com/Maratyszcza/cpuinfo)
* [JeMalloc](https://github.com/jemalloc/jemalloc)
* CMake (Installed via system package manager)
* PAPI (Installed via system package manager)
* Intel Thread Building Blocks allocator (Installed via system package manager)

The Dockerfile provided automatically downloads, builds, and installs all dependencies in the image while building. We recommend using that for ease of testing and complete replication of OS and build environment. When building the docker ensure to have the CPUInfo and JeMalloc repo in the same directory - as they are added to image during building.

The code itself uses the CMake build system. When testing make sure to compile the code in release! Or at least *our code*...

## Run instructions
Once built the binary takes a number of arguments at command-line parameters and through standard input.

Brief explanation of the command.
* -T ==> Number of threads
* -L ==> Load factor 1.0 is full, 0.0 is empty, 0.4 is 40% full, etc.
* -S ==> Table size as a power of 2.
* -D ==> Number of seconds to run the benchmark procedure for.
* -U ==> Percentage updates
* -B ==> Table to benchmark
* -M ==> Memory reclaimer
* -A ==> What allocator to use.
* -P ==> Whether PAPI is turned on.
* -H ==> Whether to use HyperThreading to avoid socket switch.
* -V ==> Whether to run tests on table instead of benchmarking.

Here are some example commands. All parameters have default values if none are provided.
 
* ./concurrent_hash_tables -T 4 -L 0.4 -S 23 -D 20 -U 10 -P true -M leaky -A je -H false -V false -B rh_brown_set
* ./concurrent_hash_tables -T 4 -L 0.4 -S 23 -D 20 -U 10 -P true -M leaky -A je  -H false -V false -B mm_set
* ./concurrent_hash_tables -T 4 -L 0.8 -S 23 -D 20 -U 20 -P true -M leaky -A je  -H false -V false -B hopscotch_set

The results are put into two csv files, one containing the keys and the other containing the specific info.
