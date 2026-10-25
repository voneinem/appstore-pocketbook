# pbstore_cacert: the Mozilla CA bundle compiled into the program (trust-anchor fallback).
# The file is checked against the SHA-256 recorded in VERSION when CMake configures.

set(PBSTORE_CACERT_PEM ${CMAKE_CURRENT_SOURCE_DIR}/cacert.pem)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${PBSTORE_CACERT_PEM} ${CMAKE_CURRENT_SOURCE_DIR}/VERSION)

file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/VERSION version_lines)
set(expected_hash "")
foreach(line IN LISTS version_lines)
    if(line MATCHES "^sha256: ([0-9a-f]+)$")
        set(expected_hash ${CMAKE_MATCH_1})
    endif()
endforeach()
file(SHA256 ${PBSTORE_CACERT_PEM} actual_hash)
if(NOT expected_hash STREQUAL actual_hash)
    message(FATAL_ERROR "third_party/cacert/cacert.pem does not match the SHA-256 in VERSION (${actual_hash})")
endif()

file(READ ${PBSTORE_CACERT_PEM} pem_hex HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," pem_bytes "${pem_hex}")
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/cacert_bundle.inc.tmp "${pem_bytes}\n")
configure_file(${CMAKE_CURRENT_BINARY_DIR}/cacert_bundle.inc.tmp ${CMAKE_CURRENT_BINARY_DIR}/generated/cacert_bundle.inc COPYONLY)

add_library(pbstore_cacert STATIC cacert_bundle.cpp)
target_include_directories(pbstore_cacert PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(pbstore_cacert PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)
