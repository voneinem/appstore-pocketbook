cmake_minimum_required(VERSION 3.16)

# PB Store: the catalog and package contract, the installer for Linux and the store app on the reader.
#
# Build target:
#   HOST (default) - desktop build: the Qt-free core, the installer, their tests, and the store's Qt-free library
#                    and tests; the Qt app is built too when the Qt6 Quick/Test development packages exist.
#   ARM            - PocketBook device build with the PocketBook SDK: only third_party, core, bridge, app.
#                    The SDK location comes ONLY from -DSDK_PATH or $POCKETBOOK_SDK; no SDK or toolchain path
#                    may be hard-coded anywhere (constitution, Technology Stack & Build Constraints).
set(PBSTORE_TARGET "HOST" CACHE STRING "Build target: HOST (desktop, tests) or ARM (PocketBook device)")
set_property(CACHE PBSTORE_TARGET PROPERTY STRINGS HOST ARM)
if(NOT PBSTORE_TARGET STREQUAL "HOST" AND NOT PBSTORE_TARGET STREQUAL "ARM")
    message(FATAL_ERROR "PBSTORE_TARGET must be HOST or ARM (got '${PBSTORE_TARGET}')")
endif()

# The toolchain has to be selected before project().
if(PBSTORE_TARGET STREQUAL "ARM")
    include(${CMAKE_CURRENT_LIST_DIR}/cmake/sdk.cmake)
endif()

# The release versions live in the VERSION file (the installer) and app/VERSION (the store app).
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_CURRENT_LIST_DIR}/VERSION)
file(STRINGS ${CMAKE_CURRENT_LIST_DIR}/VERSION PBSTORE_FILE_VERSION LIMIT_COUNT 1)
if(NOT PBSTORE_FILE_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+")
    message(FATAL_ERROR "VERSION must start with MAJOR.MINOR.PATCH (got '${PBSTORE_FILE_VERSION}')")
endif()

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_CURRENT_LIST_DIR}/app/VERSION)
file(STRINGS ${CMAKE_CURRENT_LIST_DIR}/app/VERSION PBSTORE_FILE_APP_VERSION LIMIT_COUNT 1)
if(NOT PBSTORE_FILE_APP_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "app/VERSION must be MAJOR.MINOR.PATCH (got '${PBSTORE_FILE_APP_VERSION}')")
endif()

project(pbstore VERSION ${PBSTORE_FILE_VERSION} LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

add_compile_options(-Wall -Wextra)

# AddressSanitizer and UndefinedBehaviorSanitizer for the whole tree: scripts/pre-merge.sh --sanitize
option(PBSTORE_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
if(PBSTORE_SANITIZE)
    add_compile_options(-fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()
# Dev and test builds of the store carry a version below the release, so that the real release installs over them
# as an update. The override is accepted only with -DPBSTORE_DEV_BUILD=ON (which compiles the string constant
# PBSTORE_DEV_BUILD_MARKER into the program, so that scripts/check-store-binary.sh can refuse such a build in a
# release package) or in the host test build; never for a plain device build.
option(PBSTORE_DEV_BUILD "Mark the store program as a dev build (allows PBSTORE_VERSION_OVERRIDE for the device)" OFF)
set(PBSTORE_VERSION_OVERRIDE "" CACHE STRING "MAJOR.MINOR.PATCH used instead of app/VERSION in a dev or host test build")
set(PBSTORE_APP_VERSION "${PBSTORE_FILE_APP_VERSION}")
if(PBSTORE_VERSION_OVERRIDE)
    if(NOT PBSTORE_VERSION_OVERRIDE MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
        message(FATAL_ERROR "PBSTORE_VERSION_OVERRIDE must be MAJOR.MINOR.PATCH (got '${PBSTORE_VERSION_OVERRIDE}')")
    endif()
    if(PBSTORE_TARGET STREQUAL "ARM" AND NOT PBSTORE_DEV_BUILD)
        message(FATAL_ERROR "PBSTORE_VERSION_OVERRIDE for the device needs -DPBSTORE_DEV_BUILD=ON")
    endif()
    set(PBSTORE_APP_VERSION "${PBSTORE_VERSION_OVERRIDE}")
endif()
if(PBSTORE_DEV_BUILD)
    add_compile_definitions(PBSTORE_DEV_BUILD)
endif()

# PBSTORE_VERSION is the version of the program being built: the installer's on the host (VERSION), the store's
# on the reader (app/VERSION). PBSTORE_APP_VERSION is always the store's.
if(PBSTORE_TARGET STREQUAL "ARM")
    add_compile_definitions(PBSTORE_VERSION="${PBSTORE_APP_VERSION}")
else()
    add_compile_definitions(PBSTORE_VERSION="${PBSTORE_FILE_VERSION}")
endif()
add_compile_definitions(PBSTORE_APP_VERSION="${PBSTORE_APP_VERSION}")
# Files of 2 GiB up to the 4 GiB limit of FAT: 64-bit file offsets also on the 32-bit device.
add_compile_definitions(_FILE_OFFSET_BITS=64)
message(STATUS "pbstore ${PBSTORE_TARGET}: installer ${PBSTORE_FILE_VERSION}, store app ${PBSTORE_APP_VERSION}")

if(NOT PBSTORE_TARGET STREQUAL "ARM")
    enable_testing()
endif()

# The throw-away key pair of the installer's test build lives in the build directory only.
set(PBSTORE_TEST_KEY_DIR ${CMAKE_BINARY_DIR}/test_key)

# Subdirectories are added when they exist, so the tree configures at every stage of the work.
# The device build needs neither the command line tools nor the tests.
if(PBSTORE_TARGET STREQUAL "ARM")
    set(PBSTORE_DIRS third_party/miniz third_party/cacert core)
else()
    set(PBSTORE_DIRS third_party/miniz third_party/cacert core tools/pbstore-catalog tools/pbstore-install-ui tools/pbstore-install)
endif()
foreach(dir ${PBSTORE_DIRS})
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${dir}/CMakeLists.txt)
        add_subdirectory(${dir})
    endif()
endforeach()

# The store app: the Qt layer (bridge/), the reader's interface (platform/pocketbook/), the program (app/).
# The manual moc/rcc helpers are only needed for the device.
if(PBSTORE_TARGET STREQUAL "ARM")
    include(${CMAKE_CURRENT_LIST_DIR}/cmake/qt_manual.cmake)
endif()
foreach(dir bridge platform/pocketbook app)
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${dir}/CMakeLists.txt)
        add_subdirectory(${dir})
    endif()
endforeach()

if(NOT PBSTORE_TARGET STREQUAL "ARM" AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt)
    add_subdirectory(tests)
endif()
