cmake_minimum_required(VERSION 3.12 FATAL_ERROR)
project(valhalla_tools LANGUAGES CXX C)

include(GNUInstallDirs)

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

function(add_tool)
  set(oneValueArgs NAME)
  set(multiValueArgs DEPENDS INCLUDE_DIRECTORIES)
  cmake_parse_arguments(TOOL "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})


  add_executable(${TOOL_NAME} ${CMAKE_SOURCE_DIR}/src/${TOOL_NAME}.cc)
  target_link_libraries(${TOOL_NAME} PRIVATE ${TOOL_DEPENDS})
  target_include_directories(${TOOL_NAME} 
  PRIVATE 
    ${TOOL_INCLUDE_DIRECTORIES}
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/cxxopts/include)
  install(TARGETS ${TOOL_NAME} DESTINATION "${CMAKE_INSTALL_BINDIR}" COMPONENT runtime)
endfunction()

set(programs valhalla_remove_predicted_traffic valhalla_decode_buckets valhalla_get_tile_ids valhalla_export_tiles valhalla_tile_stats)
set(lib_sources traffic.cc)
list(TRANSFORM lib_sources PREPEND ${CMAKE_SOURCE_DIR}/src/)

# lib
set(lib valhalla_tools)
add_library(${lib} ${lib_sources})
target_include_directories(${lib} PUBLIC
  PkgConfig::libvalhalla
  ${libvalhalla_INCLUDE_DIRS}/third_party
  ${CMAKE_SOURCE_DIR}/include) 


add_tool(
  NAME valhalla_remove_predicted_traffic 
  DEPENDS
    PkgConfig::libvalhalla 
    ${lib}
)

add_tool(
  NAME valhalla_decode_buckets 
  DEPENDS
    PkgConfig::libvalhalla 
)

add_tool(
  NAME valhalla_get_tile_ids 
  DEPENDS
    PkgConfig::libvalhalla 
)

add_tool(
  NAME valhalla_export_tiles 
  INCLUDE_DIRECTORIES
  PUBLIC 
      ${CMAKE_SOURCE_DIR}/third_party/rapidjson/include
  DEPENDS
    PkgConfig::libvalhalla 
    GDAL::GDAL 
)

# scripts 
configure_file(scripts/valhalla_remote_extract ${CMAKE_BINARY_DIR}/valhalla_remote_extract COPYONLY)
install(FILES scripts/valhalla_remote_extract DESTINATION "${CMAKE_INSTALL_BINDIR}"
  PERMISSIONS
      OWNER_READ OWNER_WRITE OWNER_EXECUTE
      GROUP_READ GROUP_EXECUTE
      WORLD_READ WORLD_EXECUTE
      COMPONENT runtime
  )

