cmake_minimum_required(VERSION 3.12 FATAL_ERROR)
project(valhalla_tools LANGUAGES CXX C)

if(NOT MSVC_IDE) 
  if(NOT CMAKE_BUILD_TYPE)
    message(STATUS "No build type specified, defaulting to Release")
    set(CMAKE_BUILD_TYPE Release)
  endif()
endif()

if(CMAKE_BUILD_TYPE STREQUAL Debug)
  message(STATUS "Configuring in debug mode")
elseif(CMAKE_BUILD_TYPE STREQUAL Release)
  message(STATUS "Configuring in release mode")
else()
  message(FATAL_ERROR "Unrecognized build type. Use one of Debug, Release, RelWithDebInfo, MinRelSize, None")
endif()

# some options
set(CMAKE_CXX_STANDARD 17 CACHE STRING "C++ language version to use (default is 17)")
set(LOGGING_LEVEL "INFO" CACHE STRING "Logging level, default is INFO")

include(FindPkgConfig)

set(CMAKE_CXX_STANDARD 17 CACHE STRING "C++ language version to use (default is 17)")

pkg_check_modules(libprime_server IMPORTED_TARGET libprime_server>=0.6.3)
pkg_check_modules(libvalhalla REQUIRED IMPORTED_TARGET libvalhalla>=3.4.0)

# GDAL 
  find_package(GDAL)
  if (GDAL_FOUND)
    set(GDAL_TARGET GDAL::GDAL)
  endif()

find_package(Boost CONFIG)
target_compile_definitions(PkgConfig::libvalhalla INTERFACE ENABLE_DATA_TOOLS)

set(programs valhalla_remove_predicted_traffic valhalla_decode_buckets valhalla_get_tile_ids valhalla_export_tiles)
set(lib_sources traffic.cc)
list(TRANSFORM lib_sources PREPEND ${CMAKE_SOURCE_DIR}/src/)

# lib
set(lib valhalla_tools)
add_library(${lib} ${lib_sources})
target_include_directories(${lib} PUBLIC
  PkgConfig::libvalhalla
  ${libvalhalla_INCLUDE_DIRS}/third_party
  ${CMAKE_SOURCE_DIR}/include) 


foreach(program_ ${programs})
  add_executable(${program_} ${CMAKE_SOURCE_DIR}/src/${program_}.cc)
  target_link_libraries(${program_} PRIVATE PkgConfig::libvalhalla Boost::boost GDAL::GDAL ${lib})
  target_include_directories(${program_} 
  PRIVATE 
    PkgConfig::libvalhalla 
    GDAL::GDAL
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/cxxopts/include)
endforeach()




install(TARGETS ${programs}  DESTINATION "${CMAKE_INSTALL_BINDIR}" COMPONENT runtime)
