# MESHDECK - desktop build: the portable core, the UI, the simulator and the unit tests.
#
#   cmake -S . -B build -G Ninja
#   cmake --build build
#   ctest --test-dir build --output-on-failure
#   build/platform/sim/meshdeck-sim          (the simulator; needs SDL2)
#
# SDL2 is found through CMake's normal search. If it lives somewhere unusual, add
# -DCMAKE_PREFIX_PATH=<folder containing lib/cmake/SDL2>. Without SDL2 everything except the
# simulator still builds and the tests still run.
#
# The Tab5 firmware is built separately with PlatformIO (platform/tab5).
cmake_minimum_required(VERSION 3.20)
project(meshdeck VERSION 2.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(MESHDECK_CR_BUILD_TESTS "Build the unit tests" ON)

find_package(SDL2 CONFIG QUIET)
set(MESHDECK_CR_HAVE_SDL ${SDL2_FOUND})
message(STATUS "MESHDECK simulator (SDL2): ${MESHDECK_CR_HAVE_SDL}")

add_subdirectory(third_party)

# Warnings on for our own code, so problems show up on the desktop before they reach the device.
add_compile_options(-Wall -Wextra -Wno-unused-parameter)

add_subdirectory(core)
add_subdirectory(platform/fake)

if(MESHDECK_CR_HAVE_SDL)
    add_subdirectory(platform/sim)
endif()

if(MESHDECK_CR_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
