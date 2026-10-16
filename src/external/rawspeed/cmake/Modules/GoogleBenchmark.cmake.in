cmake_minimum_required(VERSION 3.22) # FIXME: 3.24.2 / 3.25

project(googlebenchmark-download NONE)

# Enable ExternalProject CMake module
include(ExternalProject)

option(ALLOW_DOWNLOADING_GOOGLEBENCHMARK "If GOOGLEBENCHMARK src tree is not found in location specified by GOOGLEBENCHMARK_PATH, do fetch the archive from internet" OFF)
set(GOOGLEBENCHMARK_PATH "/usr/src/googlebenchmark" CACHE PATH "Path to the googlebenchmark root tree.")

# Download and install GOOGLEBENCHMARK

message(STATUS "Looking for GOOGLEBENCHMARK sources")
message(STATUS "Looking for GOOGLEBENCHMARK sources in ${GOOGLEBENCHMARK_PATH}")
if(EXISTS "${GOOGLEBENCHMARK_PATH}" AND IS_DIRECTORY "${GOOGLEBENCHMARK_PATH}" AND EXISTS "${GOOGLEBENCHMARK_PATH}/CMakeLists.txt")
  message(STATUS "Found GOOGLEBENCHMARK in ${GOOGLEBENCHMARK_PATH}")

  ExternalProject_Add(
    googlebenchmark
    PREFIX            "${CMAKE_BINARY_DIR}"
    DOWNLOAD_DIR      "${CMAKE_BINARY_DIR}/download"
    SOURCE_DIR        "${GOOGLEBENCHMARK_PATH}" # use existing src dir.
    BINARY_DIR        "${CMAKE_BINARY_DIR}/build"
    CONFIGURE_COMMAND ""
    BUILD_COMMAND     ""
    INSTALL_COMMAND   ""
    TEST_COMMAND      ""
  )
else()
  if(NOT ALLOW_DOWNLOADING_GOOGLEBENCHMARK)
    message(SEND_ERROR "Did not find GOOGLEBENCHMARK sources! Either pass correct path in GOOGLEBENCHMARK_PATH, or enable ALLOW_DOWNLOADING_GOOGLEBENCHMARK, or disable BUILD_BENCHMARKING.")
  else()
    message(WARNING "Did not find GOOGLEBENCHMARK sources! Fetching from web...")
    ExternalProject_Add(
      googlebenchmark
      URL               https://github.com/google/benchmark/archive/v1.9.3.tar.gz
      URL_HASH          SHA512=bd1bc103c89ec74a7dfdb09aa4b308496f79ccfe0a728e31eed2814b9ff0f19aa930c58827d044dac07e2e776f990f74ebc4c7cd9c86b37871f8868e1581d4e1
      PREFIX            "${CMAKE_BINARY_DIR}"
      STAMP_DIR         "${CMAKE_BINARY_DIR}/stamp"
      DOWNLOAD_DIR      "${CMAKE_BINARY_DIR}/download"
      SOURCE_DIR        "${CMAKE_BINARY_DIR}/src"
      BINARY_DIR        "${CMAKE_BINARY_DIR}/build"
      CONFIGURE_COMMAND ""
      BUILD_COMMAND     ""
      INSTALL_COMMAND   ""
      TEST_COMMAND      ""
    )
  endif()
endif()

ExternalProject_Get_Property(googlebenchmark SOURCE_DIR BINARY_DIR)
file(WRITE googlebenchmark-paths.cmake
"set(GOOGLEBENCHMARK_SOURCE_DIR \"${SOURCE_DIR}\")
set(GOOGLEBENCHMARK_BINARY_DIR \"${BINARY_DIR}\")
")
