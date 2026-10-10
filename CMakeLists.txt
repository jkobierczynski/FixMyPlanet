cmake_minimum_required(VERSION 3.16)

project(fixmyplanet VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(FIXMYPLANET_BUILD_GUI "Build the Qt 6 desktop application" ON)
option(FIXMYPLANET_BUILD_TESTS "Build the test suite" ON)
option(FIXMYPLANET_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

if(FIXMYPLANET_SANITIZE AND NOT MSVC)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

find_package(Threads REQUIRED)

# Put the compiler's runtime DLLs next to the programs on Windows, so that the
# zip works on a PC without Visual Studio.
if(WIN32)
  set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION bin)
  include(InstallRequiredSystemLibraries)
endif()

# --- third-party code, built without our warning flags ----------------------
add_library(fixmyplanet_thirdparty STATIC
  third_party/lodepng/lodepng.cpp
  third_party/vsop87/vsop87a_small.cpp)
target_include_directories(fixmyplanet_thirdparty PUBLIC
  third_party/lodepng third_party/vsop87 third_party/stb)
if(MSVC)
  target_compile_options(fixmyplanet_thirdparty PRIVATE /w)
else()
  target_compile_options(fixmyplanet_thirdparty PRIVATE -w)
endif()

# --- core library (no Qt) ----------------------------------------------------
add_library(fixmyplanet_core STATIC
  src/core/derotate.cpp
  src/core/ephem.cpp
  src/core/fits.cpp
  src/core/geometry.cpp
  src/core/image.cpp
  src/core/imageio.cpp
  src/core/json.cpp
  src/core/measure.cpp
  src/core/overlay.cpp
  src/core/planets.cpp
  src/core/session.cpp
  src/core/synth.cpp
  src/core/tiff.cpp
  src/core/timeutil.cpp)
target_include_directories(fixmyplanet_core PUBLIC src/core)
target_link_libraries(fixmyplanet_core PUBLIC fixmyplanet_thirdparty Threads::Threads)
target_compile_definitions(fixmyplanet_core PUBLIC FIXMYPLANET_VERSION="${PROJECT_VERSION}")
if(MSVC)
  target_compile_options(fixmyplanet_core PRIVATE /W4 /utf-8)
  target_compile_definitions(fixmyplanet_core PUBLIC _CRT_SECURE_NO_WARNINGS)
else()
  target_compile_options(fixmyplanet_core PRIVATE -Wall -Wextra)
endif()

if(WIN32)
  target_compile_definitions(fixmyplanet_core PUBLIC NOMINMAX)
endif()

# --- command-line tool -------------------------------------------------------
add_executable(fixmyplanet src/cli/main.cpp)
target_link_libraries(fixmyplanet PRIVATE fixmyplanet_core)
if(WIN32)
  target_link_libraries(fixmyplanet PRIVATE shell32)
endif()
if(MSVC)
  target_compile_options(fixmyplanet PRIVATE /W4 /utf-8)
else()
  target_compile_options(fixmyplanet PRIVATE -Wall -Wextra)
endif()
install(TARGETS fixmyplanet RUNTIME DESTINATION bin)

# --- desktop application -----------------------------------------------------
if(FIXMYPLANET_BUILD_GUI)
  add_subdirectory(src/gui)
endif()

# --- tests ---------------------------------------------------------------------
if(FIXMYPLANET_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
