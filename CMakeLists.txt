cmake_minimum_required(VERSION 3.16)

# PB Store: catalog and package contract (host build).
#
# The device build (TARGET_TYPE=ARM, PocketBook SDK) belongs to the store app feature. When it is
# added, the SDK location MUST come ONLY from -DSDK_PATH or $POCKETBOOK_SDK; no SDK or toolchain
# path may be hard-coded anywhere (constitution, Technology Stack & Build Constraints).

# The release version lives in the VERSION file (single source of truth).
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_CURRENT_LIST_DIR}/VERSION)
file(STRINGS ${CMAKE_CURRENT_LIST_DIR}/VERSION PBSTORE_FILE_VERSION LIMIT_COUNT 1)
if(NOT PBSTORE_FILE_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+")
    message(FATAL_ERROR "VERSION must start with MAJOR.MINOR.PATCH (got '${PBSTORE_FILE_VERSION}')")
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
add_compile_definitions(PBSTORE_VERSION="${PBSTORE_FILE_VERSION}")

enable_testing()

# The throw-away key pair of the installer's test build lives in the build directory only.
set(PBSTORE_TEST_KEY_DIR ${CMAKE_BINARY_DIR}/test_key)

# Subdirectories are added when they exist, so the tree configures at every stage of the work.
foreach(dir third_party/miniz third_party/cacert core tools/pbstore-catalog tools/pbstore-install-ui tools/pbstore-install tests)
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${dir}/CMakeLists.txt)
        add_subdirectory(${dir})
    endif()
endforeach()
