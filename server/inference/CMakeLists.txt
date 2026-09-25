# --------------------------------------------------------------------------
# guard-inference — GPU perception service (TensorRT / CUDA / Jetson).
#
# Standalone-buildable: cmake -B build -S inference -DAI_USE_TENSORRT=ON
# Also added via add_subdirectory() from the top-level orchestrator.
# --------------------------------------------------------------------------
cmake_minimum_required(VERSION 3.16)
project(GuardInference VERSION 2.5.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(AI_USE_TENSORRT "Enable NVIDIA TensorRT backend" OFF)
option(AI_USE_OPENCV "Enable OpenCV image handling" ON)

add_compile_options(-Wall -Wextra -O2)
add_compile_definitions(NDEBUG=$<CONFIG:Release>)

# ---------------------------------------------------------------------------
# Third-party headers (vendored at repo top level)
# ---------------------------------------------------------------------------
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/../third_party)
  set(GUARD_TP ${CMAKE_CURRENT_SOURCE_DIR}/../third_party)
elseif(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
  set(GUARD_TP ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
else()
  message(FATAL_ERROR "third_party/ (cpp-httplib + nlohmann/json) not found")
endif()

# cpp-httplib + nlohmann/json are header-only
if(NOT EXISTS ${GUARD_TP}/httplib/httplib.h)
  message(STATUS "Fetching cpp-httplib...")
  file(DOWNLOAD https://raw.githubusercontent.com/yhirose/cpp-httplib/master/httplib.h
       ${GUARD_TP}/httplib/httplib.h TLS_VERIFY OFF)
endif()
if(NOT EXISTS ${GUARD_TP}/nlohmann/nlohmann/json.hpp)
  message(STATUS "Fetching nlohmann/json...")
  file(MAKE_DIRECTORY ${GUARD_TP}/nlohmann)
  file(DOWNLOAD https://github.com/nlohmann/json/releases/download/v3.11.3/json.hpp
       ${GUARD_TP}/nlohmann/nlohmann/json.hpp TLS_VERIFY OFF)
endif()

# ---------------------------------------------------------------------------
# OpenCV
# ---------------------------------------------------------------------------
if(AI_USE_OPENCV)
  find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs videoio)
  add_compile_definitions(AI_HAS_OPENCV=1)
endif()

# ---------------------------------------------------------------------------
# TensorRT (Jetson: set AI_USE_TENSORRT=ON, and TRT_ROOT if not in default path)
# ---------------------------------------------------------------------------
if(AI_USE_TENSORRT)
  add_compile_definitions(AI_HAS_TENSORRT=1)
  find_package(CUDAToolkit REQUIRED)
  if(NOT DEFINED TRT_ROOT)
    set(TRT_ROOT "/usr/src/tensorrt")
  endif()
  find_path(TRT_INCLUDE_DIRS NvInfer.h
    PATHS ${TRT_ROOT}/include /usr/include/x86_64-linux-gnu /usr/include/aarch64-linux-gnu)
  find_library(TRT_LIBRARY nvinfer
    PATHS ${TRT_ROOT}/lib /usr/lib/x86_64-linux-gnu /usr/lib/aarch64-linux-gnu)
  find_library(TRT_ONNX_LIBRARY nvonnxparser
    PATHS ${TRT_ROOT}/lib /usr/lib/x86_64-linux-gnu /usr/lib/aarch64-linux-gnu)
  if(NOT TRT_INCLUDE_DIRS OR NOT TRT_LIBRARY OR NOT TRT_ONNX_LIBRARY)
    message(FATAL_ERROR "TensorRT not found. Set TRT_ROOT to your TensorRT install (e.g. /usr/src/tensorrt).")
  endif()
endif()

# ---------------------------------------------------------------------------
# Core engine library
# ---------------------------------------------------------------------------
set(INFERENCE_SRCS
  src/main.cpp
  src/config.cpp
  src/logging.cpp
  src/timing.cpp
  src/image_io.cpp
  src/model_manager.cpp
  src/engine_resolver.cpp
  src/perception.cpp
  src/job_queue.cpp
  src/live_streams.cpp)

if(AI_USE_TENSORRT)
  list(APPEND INFERENCE_SRCS src/trt_engine.cpp src/trt_models.cpp)
endif()

add_executable(guard-inference ${INFERENCE_SRCS})
target_include_directories(guard-inference PRIVATE
  include ${GUARD_TP}/httplib ${GUARD_TP}/nlohmann)
if(AI_USE_TENSORRT)
  target_include_directories(guard-inference PRIVATE ${TRT_INCLUDE_DIRS})
endif()

find_package(Threads REQUIRED)
target_link_libraries(guard-inference PRIVATE Threads::Threads)
if(AI_USE_OPENCV)
  target_link_libraries(guard-inference PRIVATE ${OpenCV_LIBS})
endif()
if(AI_USE_TENSORRT)
  target_link_libraries(guard-inference PRIVATE ${TRT_LIBRARY} ${TRT_ONNX_LIBRARY} CUDA::cudart)
endif()

install(TARGETS guard-inference RUNTIME DESTINATION bin)
