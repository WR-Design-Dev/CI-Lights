cmake_minimum_required(VERSION 3.22)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# version.txt records the last successful firmware build. Configure the next
# version before ESP-IDF generates the application descriptor.
set(_firmware_version_file "${CMAKE_CURRENT_LIST_DIR}/version.txt")
file(STRINGS "${_firmware_version_file}" _last_firmware_version LIMIT_COUNT 1)
if(NOT _last_firmware_version MATCHES "^1\\.0\\.[0-9]+$")
    message(FATAL_ERROR "Invalid firmware version in version.txt: ${_last_firmware_version}")
endif()
string(REGEX REPLACE "^1\\.0\\." "" _last_firmware_build "${_last_firmware_version}")
if(NOT "$ENV{CI_LIGHTS_RELEASE_VERSION}" STREQUAL "")
    if(NOT "$ENV{CI_LIGHTS_RELEASE_VERSION}" MATCHES "^1\\.0\\.[0-9]+$")
        message(FATAL_ERROR "Invalid release version: $ENV{CI_LIGHTS_RELEASE_VERSION}")
    endif()
    set(PROJECT_VER "$ENV{CI_LIGHTS_RELEASE_VERSION}")
else()
    math(EXPR _next_firmware_build "${_last_firmware_build} + 1")
    set(PROJECT_VER "1.0.${_next_firmware_build}")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_firmware_version_file}")

project(CI-Lights)

# Record the version only after the application binary passes the size check.
# This target runs for every `idf.py build`, including a build with no edits.
if("$ENV{CI_LIGHTS_RELEASE_VERSION}" STREQUAL "")
    add_custom_target(record_firmware_build ALL
        COMMAND "${CMAKE_COMMAND}" "-DBUILD_VERSION=${PROJECT_VER}"
                "-DVERSION_FILE=${_firmware_version_file}"
                -P "${CMAKE_CURRENT_LIST_DIR}/cmake/record_firmware_build.cmake"
        VERBATIM)
    add_dependencies(record_firmware_build app_check_size)
endif()
