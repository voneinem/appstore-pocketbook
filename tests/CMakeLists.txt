# Host-only tests. Every tests/{unit,contract,integration}/test_*.cpp becomes its own
# executable (linked with pbstore_core) registered with CTest and labelled by directory.
# Tests are written first and must fail before the implementation exists (Principle I).
# Helper programs tests/support/gen_*.cpp are built as plain executables.
#
# Work on one test at a time: cmake --build <dir> --target test_json && ctest -R test_json

file(GLOB PBSTORE_TEST_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/contract/test_*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/integration/test_*.cpp)
file(GLOB PBSTORE_HELPER_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/support/gen_*.cpp)

# The TLS tests run local HTTPS servers with libssl.
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)

set(PBSTORE_TOOL_PATH "")
set(PBSTORE_GEN_FIXTURES_PATH "")
if(TARGET pbstore-catalog)
    set(PBSTORE_TOOL_PATH "$<TARGET_FILE:pbstore-catalog>")
endif()

foreach(src ${PBSTORE_HELPER_SOURCES})
    get_filename_component(name ${src} NAME_WE)
    add_executable(${name} ${src})
    if(name STREQUAL "gen_fixtures")
        set(PBSTORE_GEN_FIXTURES_PATH "$<TARGET_FILE:gen_fixtures>")
    endif()
    target_link_libraries(${name} PRIVATE pbstore_core OpenSSL::Crypto Threads::Threads)
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/support)
endforeach()

foreach(src ${PBSTORE_TEST_SOURCES})
    get_filename_component(name ${src} NAME_WE)
    get_filename_component(dir ${src} DIRECTORY)
    get_filename_component(kind ${dir} NAME)
    add_executable(${name} ${src})
    target_link_libraries(${name} PRIVATE pbstore_core OpenSSL::SSL OpenSSL::Crypto Threads::Threads)
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/support)
    target_compile_definitions(${name} PRIVATE
        PBSTORE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
        PBSTORE_TOOL="${PBSTORE_TOOL_PATH}"
        PBSTORE_GEN_FIXTURES="${PBSTORE_GEN_FIXTURES_PATH}")
    if(PBSTORE_GEN_FIXTURES_PATH)
        add_dependencies(${name} gen_fixtures)
    endif()
    if(TARGET pbstore-catalog)
        add_dependencies(${name} pbstore-catalog)
    endif()
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES LABELS "${kind}" SKIP_RETURN_CODE 77)
    if(PBSTORE_SANITIZE)
        set_tests_properties(${name} PROPERTIES ENVIRONMENT "LSAN_OPTIONS=suppressions=${CMAKE_CURRENT_SOURCE_DIR}/lsan.supp:print_suppressions=0")
    endif()
endforeach()
