# --------------------------------------------------------------------------
# guard-core — public API, business rules, embedded SQLite, job runner.
#
# Standalone-buildable: cmake -B build -S core
# --------------------------------------------------------------------------
cmake_minimum_required(VERSION 3.16)
project(GuardCore VERSION 2.5.3 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
add_compile_options(-Wall -Wextra)

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/../third_party)
  set(GUARD_TP ${CMAKE_CURRENT_SOURCE_DIR}/../third_party)
else()
  message(FATAL_ERROR "third_party/ (cpp-httplib + nlohmann/json) not found")
endif()

find_package(SQLite3 REQUIRED)
find_package(Threads REQUIRED)

# Everything except main.cpp, so tests link the same code.
add_library(guard_core_lib STATIC
  src/common/config.cpp
  src/common/log.cpp
  src/domain/plate.cpp
  src/domain/face_matcher.cpp
  src/domain/access_policy.cpp
  src/domain/visit_tracker.cpp
  src/infra/crypto.cpp
  src/infra/sqlite_db.cpp
  src/infra/repositories.cpp
  src/infra/inference_client.cpp
  src/app/event_bus.cpp
  src/app/serialization.cpp
  src/app/fleet_service.cpp
  src/app/job_service.cpp
  src/app/auth_service.cpp
  src/app/frame_annotator.cpp
  src/app/access_recorder.cpp
  src/app/camera_service.cpp
  src/api/http_api.cpp)
target_include_directories(guard_core_lib PUBLIC src ${GUARD_TP}/httplib ${GUARD_TP}/nlohmann)
target_link_libraries(guard_core_lib PUBLIC SQLite3::SQLite3 Threads::Threads)
target_compile_definitions(guard_core_lib PUBLIC GUARD_VERSION="${PROJECT_VERSION}")

add_executable(guard-core src/main.cpp)
target_link_libraries(guard-core PRIVATE guard_core_lib)
install(TARGETS guard-core RUNTIME DESTINATION bin)

option(GUARD_BUILD_TESTS "Build guard-core tests" ON)
if(GUARD_BUILD_TESTS)
  enable_testing()
  add_executable(guard-core-tests tests/test_main.cpp tests/test_domain.cpp tests/test_storage.cpp tests/test_live.cpp)
  target_link_libraries(guard-core-tests PRIVATE guard_core_lib)
  target_compile_definitions(guard-core-tests PRIVATE
    MIGRATIONS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/migrations")
  add_test(NAME guard-core-tests COMMAND guard-core-tests)
endif()
