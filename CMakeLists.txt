cmake_minimum_required(VERSION 3.22)

set(CI_LIGHTS_PROFILE "standard" CACHE STRING "Firmware profile: standard or secure")
if(NOT CI_LIGHTS_PROFILE MATCHES "^(standard|secure)$")
    message(FATAL_ERROR "Unknown CI-Lights profile: ${CI_LIGHTS_PROFILE}")
endif()
set(SDKCONFIG "${CMAKE_BINARY_DIR}/sdkconfig" CACHE FILEPATH "Isolated profile configuration")
set(SDKCONFIG_DEFAULTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults;${CMAKE_CURRENT_LIST_DIR}/sdkconfig.${CI_LIGHTS_PROFILE}")
set(CI_LIGHTS_KEYRING "${CMAKE_CURRENT_LIST_DIR}/security/secure-keyring.json" CACHE FILEPATH "Public secure key registry")
set(CI_LIGHTS_SIGNING_KEY "" CACHE FILEPATH "Override signing key for a protected local vault")
if(NOT CI_LIGHTS_SIGNING_KEY STREQUAL "")
    file(TO_CMAKE_PATH "${CI_LIGHTS_SIGNING_KEY}" _signing_key_path)
    file(WRITE "${CMAKE_BINARY_DIR}/signing-key.defaults" "CONFIG_SECURE_BOOT_SIGNING_KEY=\"${_signing_key_path}\"\n")
    list(APPEND SDKCONFIG_DEFAULTS "${CMAKE_BINARY_DIR}/signing-key.defaults")
endif()
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

if(CI_LIGHTS_PROFILE STREQUAL "secure")
    project(CI-Lights-Secure)
else()
    project(CI-Lights)
endif()

if(CI_LIGHTS_PROFILE STREQUAL "secure")
    if(NOT CONFIG_APP_SECURITY_SECURE OR NOT CONFIG_SECURE_BOOT_V2_ENABLED
       OR NOT CONFIG_SECURE_FLASH_ENCRYPTION_MODE_RELEASE OR NOT CONFIG_NVS_ENCRYPTION)
        message(FATAL_ERROR "Secure profile has a conflicting cached sdkconfig. Use a clean build-secure directory.")
    endif()
else()
    if(CONFIG_APP_SECURITY_SECURE OR CONFIG_SECURE_BOOT OR CONFIG_SECURE_FLASH_ENC_ENABLED OR CONFIG_NVS_ENCRYPTION)
        message(FATAL_ERROR "Standard profile must not activate hardware security. Use a clean build-standard directory.")
    endif()
endif()

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
