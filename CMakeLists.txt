cmake_minimum_required(VERSION 3.5)

set(CMAKE_C_COMPILER "/usr/bin/gcc")

project(StrangeMachine LANGUAGES C)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(BUILD_EXAMPLES "build examples (default on)" ON)
option(BUILD_TOOLS "build tools (default on)" ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()


add_subdirectory(StrangeMachine)
if (BUILD_EXAMPLES)
  # add_subdirectory(TestSites/terror-em-sl)
  add_subdirectory(TestSites/renderer)
  # add_subdirectory(TestSites/world3D)
endif()

if (BUILD_TOOLS)
  add_subdirectory(Tools/conv)
endif()
