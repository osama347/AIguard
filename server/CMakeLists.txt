# --------------------------------------------------------------------------
# Guard++ — top-level orchestrator.
#
# Builds both services as independent executables:
#   guard-inference   (inference/)
#   guard-core        (core/)
#
# Usage:
#   cmake -B build -S .                          # default (no TensorRT)
#   cmake -B build -S . -DAI_USE_TENSORRT=ON    # Jetson with GPU
#   cmake --build build -j
#
# Each subdirectory is also standalone-buildable:
#   cmake -B build-inf   -S inference   -DAI_USE_TENSORRT=ON
#   cmake -B build-core  -S core
# --------------------------------------------------------------------------
cmake_minimum_required(VERSION 3.16)
project(GuardPlusPlus VERSION 0.5.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(AI_USE_TENSORRT "Enable NVIDIA TensorRT backend" ON)
option(AI_USE_OPENCV "Enable OpenCV image handling" ON)

add_compile_options(-Wall -Wextra -O2)
add_compile_definitions(NDEBUG=$<CONFIG:Release>)

# Pass options down to subdirectories
set(AI_USE_TENSORRT ${AI_USE_TENSORRT} CACHE BOOL "" FORCE)
set(AI_USE_OPENCV ${AI_USE_OPENCV} CACHE BOOL "" FORCE)

# ---------------------------------------------------------------------------
# Build each service as an independent sub-project
# ---------------------------------------------------------------------------
add_subdirectory(inference)
add_subdirectory(core)
