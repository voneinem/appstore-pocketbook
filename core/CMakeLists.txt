# pbstore_core: Qt-free core library (catalog, manifest, entry, package checks, signing).
# It MUST NOT depend on Qt, InkView or the PocketBook SDK (constitution Principle VI); it builds and
# is tested on the host and is meant to be linked by the store and the installer later.
# Sources are globbed so parallel work on separate files does not collide on this file.

find_package(OpenSSL REQUIRED)
if(PBSTORE_TARGET STREQUAL "ARM")
    # the reader's own libcurl is not the tested combination: use the static one (scripts/build-curl-arm.sh)
    include(${CMAKE_SOURCE_DIR}/cmake/curl_arm.cmake)
else()
    find_package(CURL REQUIRED)
endif()
find_package(Threads REQUIRED)

# catalog/reserved-data-dirs.txt and catalog/accepted-licenses.txt are the single sources of the
# reserved data folder names and the accepted licenses; they are compiled into the library so the
# store and the command line use the same lists. One entry per line, "#" starts a comment.
function(pbstore_embed_list source_file output_name pattern)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${source_file})
    file(STRINGS ${source_file} lines)
    set(items "")
    foreach(line IN LISTS lines)
        string(STRIP "${line}" line)
        if(NOT line STREQUAL "" AND NOT line MATCHES "^#")
            if(NOT line MATCHES "${pattern}")
                message(FATAL_ERROR "${source_file}: '${line}' is not a valid entry")
            endif()
            string(APPEND items "    \"${line}\",\n")
        endif()
    endforeach()
    file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/${output_name}.tmp "${items}")
    configure_file(${CMAKE_CURRENT_BINARY_DIR}/${output_name}.tmp ${CMAKE_CURRENT_BINARY_DIR}/generated/${output_name} COPYONLY)
endfunction()

pbstore_embed_list(${CMAKE_SOURCE_DIR}/catalog/reserved-data-dirs.txt reserved_dirs_data.inc "^[a-z0-9][a-z0-9_-]+$")
pbstore_embed_list(${CMAKE_SOURCE_DIR}/catalog/accepted-licenses.txt accepted_licenses_data.inc "^[A-Za-z0-9][A-Za-z0-9.+-]*$")

file(GLOB PBSTORE_CORE_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)
add_library(pbstore_core STATIC ${PBSTORE_CORE_SOURCES})
target_include_directories(pbstore_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(pbstore_core PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_link_libraries(pbstore_core PUBLIC OpenSSL::Crypto CURL::libcurl pbstore_miniz Threads::Threads)

# The installer engine (core/install) sits on top of pbstore_core; it is added when it exists.
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/install/CMakeLists.txt)
    add_subdirectory(install)
endif()

# The store's Qt-free policies and façade (core/store) sit on top of the installer engine.
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/store/CMakeLists.txt)
    add_subdirectory(store)
endif()
