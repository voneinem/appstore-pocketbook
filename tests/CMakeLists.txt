# Host-only tests. Every tests/{unit,contract,integration}/test_*.cpp becomes its own
# executable (linked with pbstore_core) registered with CTest and labelled by directory.
# Tests are written first and must fail before the implementation exists (Principle I).
# Helper programs tests/support/gen_*.cpp are built as plain executables.
#
# Work on one test at a time: cmake --build <dir> --target test_json && ctest -R test_json

file(GLOB PBSTORE_TEST_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/contract/test_*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/integration/test_*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/store/test_*.cpp)
file(GLOB PBSTORE_HELPER_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/support/gen_*.cpp)

# The TLS tests run local HTTPS servers with libssl.
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)

set(PBSTORE_TOOL_PATH "")
set(PBSTORE_GEN_FIXTURES_PATH "")
set(PBSTORE_INSTALL_TEST_TOOL_PATH "")
set(PBSTORE_TEST_SEED_FILE_PATH "")
if(TARGET pbstore-catalog)
    set(PBSTORE_TOOL_PATH "$<TARGET_FILE:pbstore-catalog>")
endif()
if(TARGET pbstore-install-test)
    set(PBSTORE_INSTALL_TEST_TOOL_PATH "$<TARGET_FILE:pbstore-install-test>")
endif()
# The throw-away key of the installer's test build (made in the build directory by gen_test_key).
if(TARGET pbstore_install)
    set(PBSTORE_TEST_SEED_FILE_PATH ${PBSTORE_TEST_KEY_DIR}/test_signing_seed.txt)
endif()

# Tests that need the installer engine (and, for the assistant, its library) link them when they exist.
set(PBSTORE_TEST_EXTRA_LIBS "")
foreach(lib pbstore_install_ui pbstore_store pbstore_install)
    if(TARGET ${lib})
        list(APPEND PBSTORE_TEST_EXTRA_LIBS ${lib})
    endif()
endforeach()

foreach(src ${PBSTORE_HELPER_SOURCES})
    get_filename_component(name ${src} NAME_WE)
    add_executable(${name} ${src})
    if(name STREQUAL "gen_fixtures")
        set(PBSTORE_GEN_FIXTURES_PATH "$<TARGET_FILE:gen_fixtures>")
    endif()
    target_link_libraries(${name} PRIVATE pbstore_core OpenSSL::Crypto Threads::Threads)
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/support)
endforeach()

# The throw-away key pair of the installer's test build: made in the build directory, never committed.
if(TARGET gen_test_key AND TARGET pbstore_install)
    add_custom_command(
        OUTPUT ${PBSTORE_TEST_KEY_DIR}/test_trusted_key.inc ${PBSTORE_TEST_KEY_DIR}/test_signing_seed.txt
        COMMAND ${CMAKE_COMMAND} -E env LSAN_OPTIONS=detect_leaks=0 $<TARGET_FILE:gen_test_key> ${PBSTORE_TEST_KEY_DIR}
        DEPENDS gen_test_key
        COMMENT "Generating the throw-away test key")
    add_custom_target(pbstore_test_key DEPENDS ${PBSTORE_TEST_KEY_DIR}/test_trusted_key.inc ${PBSTORE_TEST_KEY_DIR}/test_signing_seed.txt)
endif()

foreach(src ${PBSTORE_TEST_SOURCES})
    get_filename_component(name ${src} NAME_WE)
    get_filename_component(dir ${src} DIRECTORY)
    get_filename_component(kind ${dir} NAME)
    add_executable(${name} ${src})
    target_link_libraries(${name} PRIVATE ${PBSTORE_TEST_EXTRA_LIBS} pbstore_core OpenSSL::SSL OpenSSL::Crypto Threads::Threads)
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/support)
    target_compile_definitions(${name} PRIVATE
        PBSTORE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
        PBSTORE_TOOL="${PBSTORE_TOOL_PATH}"
        PBSTORE_INSTALL_TEST_TOOL="${PBSTORE_INSTALL_TEST_TOOL_PATH}"
        PBSTORE_TEST_SEED_FILE="${PBSTORE_TEST_SEED_FILE_PATH}"
        PBSTORE_BUILD_DIR="${CMAKE_BINARY_DIR}"
        PBSTORE_GEN_FIXTURES="${PBSTORE_GEN_FIXTURES_PATH}")
    # The build configuration is compiled into each program, so two tests link it: the release one and the test one.
    if(name STREQUAL "test_install_build_config" AND TARGET pbstore_install_config)
        target_sources(${name} PRIVATE $<TARGET_OBJECTS:pbstore_install_config>)
    endif()
    if(name STREQUAL "test_install_build_config_testbuild" AND TARGET pbstore_install_config_test)
        target_sources(${name} PRIVATE $<TARGET_OBJECTS:pbstore_install_config_test>)
    endif()
    # The reader's wrappers of InkView are compiled against the host shim of inkview.h (the only place a test includes it).
    if(name STREQUAL "test_platform_wrappers")
        if(TARGET pbstore_platform_shim)
            target_link_libraries(${name} PRIVATE pbstore_platform_shim)
        endif()
        target_include_directories(${name} PRIVATE ${CMAKE_SOURCE_DIR}/tests/support/inkview_shim ${CMAKE_SOURCE_DIR})
    endif()
    # Tests of the installer may include the engine's private sources next to the public headers.
    if(TARGET pbstore_install)
        target_include_directories(${name} PRIVATE ${CMAKE_SOURCE_DIR}/core/install/src ${PBSTORE_TEST_KEY_DIR})
        add_dependencies(${name} pbstore_test_key)
    endif()
    if(TARGET pbstore-install-test)
        add_dependencies(${name} pbstore-install-test)
    endif()
    if(PBSTORE_GEN_FIXTURES_PATH)
        add_dependencies(${name} gen_fixtures)
    endif()
    if(TARGET pbstore-catalog)
        add_dependencies(${name} pbstore-catalog)
    endif()
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES LABELS "${kind}" SKIP_RETURN_CODE 77)
    # Tests that need a loop-mounted FAT volume (mkfs.vfat and sudo) are named test_*_vfat and carry
    # the label "vfat"; they skip with status 77 when either tool is missing.
    if(name MATCHES "_vfat$")
        set_tests_properties(${name} PROPERTIES LABELS "${kind};vfat")
    endif()
    # Tests of the released binary (PBSTORE_RELEASE_BINARY set by the release scripts) carry "release".
    if(name MATCHES "_release_binary$")
        set_tests_properties(${name} PROPERTIES LABELS "${kind};release")
    endif()
    if(PBSTORE_SANITIZE)
        set_tests_properties(${name} PROPERTIES ENVIRONMENT "LSAN_OPTIONS=suppressions=${CMAKE_CURRENT_SOURCE_DIR}/lsan.supp:print_suppressions=0")
    endif()
endforeach()

# ---- the store app on the reader ------------------------------------------------------------------------
# Tests of core/store are tests/store/test_*.cpp (label "store", linked with pbstore_store through the list above).

# Gates: the e-ink style checker proves itself and scans the QML.
add_test(NAME check_ui_style_selftest COMMAND ${CMAKE_SOURCE_DIR}/scripts/check-ui-style.sh --self-test)
add_test(NAME check_ui_style COMMAND ${CMAKE_SOURCE_DIR}/scripts/check-ui-style.sh ${CMAKE_SOURCE_DIR})
set_tests_properties(check_ui_style_selftest check_ui_style PROPERTIES LABELS "script")

# Shell tests: the launcher on a fake tree (with its fault matrix), the packaging script, the probe on the host.
# Each is registered once its file exists, so the tree configures at every stage of the work.
foreach(shell_test launcher/test_pbstore_launcher packaging/test_package_script store/test_pbstore_probe_host)
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${shell_test}.sh)
        get_filename_component(shell_name ${shell_test} NAME)
        get_filename_component(shell_dir ${shell_test} DIRECTORY)
        add_test(NAME ${shell_name} COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/${shell_test}.sh
                 WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
        set_tests_properties(${shell_name} PROPERTIES LABELS "${shell_dir}" SKIP_RETURN_CODE 77
                             ENVIRONMENT "PBSTORE_BUILD_DIR=${CMAKE_BINARY_DIR}")
    endif()
endforeach()

# Scripted walks of the real pages, offscreen (needs the host test build of the app, which needs Qt).
if(TARGET pbstore-app-test AND EXISTS ${CMAKE_SOURCE_DIR}/scripts/ui-walk.sh)
    file(GLOB PBSTORE_WALKS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/walks/*.walk)
    if(PBSTORE_WALKS)
        add_test(NAME ui_walks COMMAND ${CMAKE_SOURCE_DIR}/scripts/ui-walk.sh ${PBSTORE_WALKS}
                 WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
        set_tests_properties(ui_walks PROPERTIES LABELS "walk" SKIP_RETURN_CODE 77
                             ENVIRONMENT "PBSTORE_BUILD_DIR=${CMAKE_BINARY_DIR}")
    endif()
endif()

# Qt Test of the bridge, offscreen (skips with status 77 without Qt).
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/bridge/CMakeLists.txt)
    add_subdirectory(bridge)
endif()
