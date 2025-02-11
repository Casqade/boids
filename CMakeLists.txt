cmake_minimum_required(VERSION 3.20)

set(TARGET Boids)
project(${TARGET} LANGUAGES CXX)


set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_BINDIR}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_BINDIR}")


option(BUILD_SHARED_LIBS "Build using shared libraries" ON)
option(BUILD_VULKAN_LIBS "Build Vulkan libs from source" OFF)


option(GLM_DISABLE_SIMD "Disable SIMD instructions usage in GLM" ON)
option(GLM_FORCE_INLINE "Force GLM functions inlining" OFF)


add_executable(${TARGET})

target_sources(
  ${TARGET} PRIVATE
    src/main.cpp

    src/Allocators.cpp
    src/Containers.cpp
    src/Frontend.cpp
    src/Logger.cpp
    src/ThreadAffinity.cpp
    src/ThreadPool.cpp
    src/Vector.cpp
)

set_target_properties(
  ${TARGET} PROPERTIES
    CXX_STANDARD_REQUIRED ON
    CXX_STANDARD 17
)


if(${BUILD_SHARED_LIBS})
  set(BUILD_SHARED_LIBS ON CACHE BOOL "" FORCE)
  set(SPDLOG_BUILD_SHARED ON CACHE BOOL "" FORCE)
  set(BUILD_STATIC_LIBS OFF CACHE BOOL "" FORCE)
else()
  set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
  set(SPDLOG_BUILD_SHARED OFF CACHE BOOL "" FORCE)
  set(BUILD_STATIC_LIBS ON CACHE BOOL "" FORCE)

  if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    set(USE_MSVC_RUNTIME_LIBRARY_DLL OFF CACHE BOOL "" FORCE)
    set(USE_STATIC_CRT ON CACHE BOOL "" FORCE)
  endif()
endif()

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)

set(GLM_BUILD_LIBRARY ON CACHE BOOL "" FORCE)
set(GLM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLM_BUILD_INSTALL OFF CACHE BOOL "" FORCE)
set(GLM_DISABLE_AUTO_DETECTION ON CACHE BOOL "" FORCE)

set(SPDLOG_NO_ATOMIC_LEVELS ON CACHE BOOL "" FORCE)
set(SPDLOG_NO_EXCEPTIONS ON CACHE BOOL "" FORCE)
set(SPDLOG_DISABLE_DEFAULT_LOGGER ON CACHE BOOL "" FORCE)


include(FetchContent)

FetchContent_Declare(spdlog
  GIT_REPOSITORY https://github.com/gabime/spdlog
  GIT_TAG        v1.15.1
  GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(spdlog)


FetchContent_Declare(TimeUtils
  GIT_REPOSITORY https://github.com/Casqade/TimeUtils
  GIT_TAG        main
  GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(TimeUtils)


FetchContent_Declare(glfw
  GIT_REPOSITORY https://github.com/glfw/glfw
  GIT_TAG        3.4
  GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(glfw)


FetchContent_Declare(glm
  GIT_REPOSITORY https://github.com/g-truc/glm
  GIT_TAG        1.0.1
  GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(glm)


if(${BUILD_VULKAN_LIBS})
  set(SPIRV_SKIP_TESTS ON CACHE BOOL "" FORCE)
  set(SPIRV_SKIP_EXECUTABLES ON CACHE BOOL "" FORCE)

  FetchContent_Declare(vulkan-headers
    GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers
    GIT_TAG        vulkan-sdk-1.4.304.0
    GIT_SHALLOW    TRUE
  )
  FetchContent_MakeAvailable(vulkan-headers)


  FetchContent_Declare(vulkan-loader
    GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Loader
    GIT_TAG        vulkan-sdk-1.4.304.0
    GIT_SHALLOW    TRUE
  )
  FetchContent_MakeAvailable(vulkan-loader)


  FetchContent_Declare(vulkan-utility-libraries
    GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Utility-Libraries
    GIT_TAG        vulkan-sdk-1.4.304.0
    GIT_SHALLOW    TRUE
  )
  FetchContent_MakeAvailable(vulkan-utility-libraries)


  FetchContent_Declare(vulkan-validation-layers
    GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-ValidationLayers
    GIT_TAG        vulkan-sdk-1.4.304.0
    GIT_SHALLOW    TRUE
  )
  FetchContent_MakeAvailable(vulkan-validation-layers)


  FetchContent_Declare(spirv-headers
    GIT_REPOSITORY https://github.com/KhronosGroup/SPIRV-Headers
    GIT_TAG        vulkan-sdk-1.4.304.0
    GIT_SHALLOW    TRUE
  )
  FetchContent_MakeAvailable(spirv-headers)


  FetchContent_Declare(spirv-tools
    GIT_REPOSITORY https://github.com/KhronosGroup/SPIRV-Tools
    GIT_TAG        vulkan-sdk-1.4.304.0
    GIT_SHALLOW    TRUE
  )
  FetchContent_MakeAvailable(spirv-tools)
endif()


target_compile_definitions(glm PUBLIC
  GLM_FORCE_DEPTH_ZERO_TO_ONE
)

if(${GLM_DISABLE_SIMD})
  target_compile_definitions(glm PUBLIC
    GLM_FORCE_PURE
  )
else()
  if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(glm PUBLIC
      -msse4.1
    )
  endif()

  target_compile_definitions(glm PUBLIC
    GLM_FORCE_INTRINSICS
    GLM_FORCE_DEFAULT_ALIGNED_GENTYPES
  )
endif()

if(${GLM_FORCE_INLINE})
  target_compile_definitions(glm PUBLIC
    GLM_FORCE_INLINE
  )
endif()


target_compile_definitions(${TARGET}
PRIVATE
  NOMINMAX

PUBLIC
  VULKAN_HPP_NO_EXCEPTIONS
)

target_compile_definitions(glfw INTERFACE
  GLFW_INCLUDE_VULKAN
)


find_package(Threads REQUIRED)

if(${BUILD_VULKAN_LIBS})
  target_link_libraries(${TARGET} PUBLIC
    Vulkan::Headers
    Vulkan::Loader
    Vulkan::LayerSettings
    Vulkan::UtilityHeaders
    VkLayer_utils
  )
else()
  find_package(Vulkan REQUIRED)
  find_package(VulkanLoader REQUIRED)
  find_package(VulkanUtilityLibraries REQUIRED)

  target_link_libraries(${TARGET} PUBLIC
    Vulkan::Vulkan
    Vulkan::Headers
    Vulkan::Loader
  )
endif()

target_link_libraries(${TARGET} PUBLIC
  Threads::Threads
  glfw
  TimeUtils::TimeUtils
  glm::glm
  spdlog::spdlog
)


include(GNUInstallDirs)

set_target_properties(${TARGET} PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_BINDIR}"
)

if(CMAKE_BUILD_TYPE MATCHES "Release")

  set_target_properties(${TARGET} PROPERTIES
    WIN32_EXECUTABLE FALSE
  )

else()

  if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
#    target_compile_options(${TARGET} PUBLIC -O0)
  endif()

  set_target_properties(${TARGET} PROPERTIES
    WIN32_EXECUTABLE FALSE
  )
endif()

if(NOT ${BUILD_SHARED_LIBS})
  target_link_libraries(${TARGET} PUBLIC -static)

  if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_link_libraries(${TARGET} PRIVATE
      -static-libgcc
      -static-libstdc++
    )
  endif()
endif()


target_include_directories(
  ${TARGET} PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/${CMAKE_INSTALL_INCLUDEDIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
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

execute_process(
  COMMAND glslc triangle.vert -o triangle_vs.spv
  WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/src/shaders"
  COMMAND_ERROR_IS_FATAL ANY
)

execute_process(
  COMMAND glslc triangle.frag -o triangle_fs.spv
  WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/src/shaders"
  COMMAND_ERROR_IS_FATAL ANY
)

install(
  DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/src/shaders
  DESTINATION ${CMAKE_INSTALL_BINDIR}
  FILES_MATCHING PATTERN "*.spv"
)

file(
  COPY src/shaders
  DESTINATION ${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_BINDIR}
  FILES_MATCHING PATTERN "*.spv"
)
