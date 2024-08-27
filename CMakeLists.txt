cmake_minimum_required(VERSION 3.20)

set(TARGET Boids)
project(${TARGET} LANGUAGES CXX)

add_executable(${TARGET})

set_target_properties(
  ${TARGET} PROPERTIES
    CXX_STANDARD_REQUIRED ON
    CXX_STANDARD 17
)

target_sources(
  ${TARGET} PRIVATE
    src/main.cpp
)


include(GNUInstallDirs)

target_include_directories(
  ${TARGET} PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/${CMAKE_INSTALL_INCLUDEDIR}>
)

target_compile_definitions(
  ${TARGET} PRIVATE
  PERFORMANCE_COUNTERS_ENABLED
)

target_compile_options(
  ${TARGET} PRIVATE
  -fno-exceptions
  -ffast-math
#  -fno-math-errno
)

target_link_options(
  ${TARGET} PRIVATE
  -static-libgcc
#  -static-libstdc++
)
