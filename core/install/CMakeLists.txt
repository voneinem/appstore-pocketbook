# pbstore_install: the installer engine. Qt-free, display-free, SDK-free (constitution Principle VI).
# It reuses pbstore_core for every check and puts the operating system behind small interfaces
# (reader_fs.h, mount_source.h, open_url.h, state_dirs.h, clock.h). Only src/posix_*.cpp include POSIX
# headers (scripts/check-constitution.sh, rule R14).
#
# build_config.cpp is NOT part of the library: it holds the catalog address and the trusted keys and is
# compiled into each program (release) or into the test build (with PBSTORE_TEST_BUILD) so that the
# released program has no way to override either.

file(GLOB PBSTORE_INSTALL_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)
list(FILTER PBSTORE_INSTALL_SOURCES EXCLUDE REGEX "/build_config\\.cpp$")
add_library(pbstore_install STATIC ${PBSTORE_INSTALL_SOURCES})
target_include_directories(pbstore_install PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(pbstore_install PUBLIC pbstore_core)
if(TARGET pbstore_cacert)
    target_link_libraries(pbstore_install PUBLIC pbstore_cacert)
endif()

# ---- build configuration compiled into each program --------------------------------------------------
include(${CMAKE_CURRENT_SOURCE_DIR}/embed_trusted_keys.cmake)
set(PBSTORE_CATALOG_URL "https://voneinem.github.io/appstore-pocketbook/catalog.json" CACHE STRING "Address of the signed catalog compiled into the released installer")
file(GLOB PBSTORE_TRUSTED_KEY_FILES ${CMAKE_SOURCE_DIR}/keys/trusted/*.pub)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${PBSTORE_TRUSTED_KEY_FILES})
pbstore_embed_trusted_keys(${CMAKE_SOURCE_DIR}/keys/trusted ${CMAKE_CURRENT_BINARY_DIR}/generated/trusted_keys.inc)

# The released program: the real keys, the real address, no override.
add_library(pbstore_install_config OBJECT src/build_config.cpp)
target_include_directories(pbstore_install_config PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_link_libraries(pbstore_install_config PRIVATE pbstore_core)
target_compile_definitions(pbstore_install_config PRIVATE PBSTORE_CATALOG_URL="${PBSTORE_CATALOG_URL}")

# The test build (pbstore-install-test, never shipped): a throw-away key made in the build directory and
# overrides read from the environment.
add_library(pbstore_install_config_test OBJECT src/build_config.cpp)
target_include_directories(pbstore_install_config_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include ${PBSTORE_TEST_KEY_DIR} ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_link_libraries(pbstore_install_config_test PRIVATE pbstore_core)
target_compile_definitions(pbstore_install_config_test PRIVATE PBSTORE_TEST_BUILD PBSTORE_CATALOG_URL="${PBSTORE_CATALOG_URL}")
if(EXISTS ${CMAKE_SOURCE_DIR}/tests/CMakeLists.txt)
    add_dependencies(pbstore_install_config_test pbstore_test_key)
endif()

# Key list rules, checked by running the script on small key folders.
set(PBSTORE_KEYCHECK_DIR ${CMAKE_CURRENT_BINARY_DIR}/keycheck)
set(valid_key "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=")
foreach(case three_ok:3 four_fail:4 none_fail:0)
    string(REPLACE ":" ";" parts ${case})
    list(GET parts 0 case_name)
    list(GET parts 1 case_count)
    file(REMOVE_RECURSE ${PBSTORE_KEYCHECK_DIR}/${case_name})
    file(MAKE_DIRECTORY ${PBSTORE_KEYCHECK_DIR}/${case_name})
    if(case_count GREATER 0)
        foreach(i RANGE 1 ${case_count})
            file(WRITE ${PBSTORE_KEYCHECK_DIR}/${case_name}/key${i}.pub "${valid_key}\n")
        endforeach()
    endif()
    add_test(NAME install_keys_${case_name}
             COMMAND ${CMAKE_COMMAND} -DKEYS_DIR=${PBSTORE_KEYCHECK_DIR}/${case_name} -DOUTPUT=${PBSTORE_KEYCHECK_DIR}/${case_name}.inc -P ${CMAKE_CURRENT_SOURCE_DIR}/embed_trusted_keys.cmake)
    set_tests_properties(install_keys_${case_name} PROPERTIES LABELS unit)
    if(case_name MATCHES "_fail$")
        set_tests_properties(install_keys_${case_name} PROPERTIES WILL_FAIL TRUE)
    endif()
endforeach()
file(REMOVE_RECURSE ${PBSTORE_KEYCHECK_DIR}/malformed_fail)
file(MAKE_DIRECTORY ${PBSTORE_KEYCHECK_DIR}/malformed_fail)
file(WRITE ${PBSTORE_KEYCHECK_DIR}/malformed_fail/key.pub "not a key\n")
add_test(NAME install_keys_malformed_fail
         COMMAND ${CMAKE_COMMAND} -DKEYS_DIR=${PBSTORE_KEYCHECK_DIR}/malformed_fail -DOUTPUT=${PBSTORE_KEYCHECK_DIR}/malformed_fail.inc -P ${CMAKE_CURRENT_SOURCE_DIR}/embed_trusted_keys.cmake)
set_tests_properties(install_keys_malformed_fail PROPERTIES LABELS unit WILL_FAIL TRUE)
