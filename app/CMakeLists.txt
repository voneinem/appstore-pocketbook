# The store program: main, the InkView device bridge (reader only), VERSION and RELEASE_DATE. Targets:
#   pbstore            the program for the reader (PBSTORE_TARGET=ARM), packaged as applications/pbstore.bin
#   pbstore-app-test   the host-only test build, never installed or packaged
# The Qt parts are filled in by tasks T075 to T080; the build configuration comes first.

# The release date is the floor of the clock check and a tag the packaging step looks for (read from app/RELEASE_DATE).
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/RELEASE_DATE)
file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/RELEASE_DATE PBSTORE_RELEASE_DATE LIMIT_COUNT 1)
if(NOT PBSTORE_RELEASE_DATE MATCHES "^[0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9]$")
    message(FATAL_ERROR "app/RELEASE_DATE must be YYYY-MM-DD (got '${PBSTORE_RELEASE_DATE}')")
endif()

# store_config.cpp is not part of a library: it names the release configuration of the engine and is compiled into each program
# (and into test_store_build_config), like build_config.cpp of the installer.
add_library(pbstore_app_config OBJECT store_config.cpp)
target_include_directories(pbstore_app_config PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(pbstore_app_config PUBLIC pbstore_store)
target_compile_definitions(pbstore_app_config PRIVATE PBSTORE_RELEASE_DATE="${PBSTORE_RELEASE_DATE}")

file(GLOB PBSTORE_QML_FILES CONFIGURE_DEPENDS
    ${CMAKE_SOURCE_DIR}/qml/*.qml ${CMAKE_SOURCE_DIR}/qml/pages/*.qml ${CMAKE_SOURCE_DIR}/qml/qmldir ${CMAKE_SOURCE_DIR}/qml/icons/*.svg ${CMAKE_SOURCE_DIR}/bridge/strings/strings.tsv)
# The license texts and their manifest are compiled into the program (a running program never reads a package file that an update could replace
# under it; research R-04). licenses.qrc is generated here: the files are the manifest, the texts it names under licenses/, and LICENSE.
file(GLOB PBSTORE_LICENSE_TEXTS CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/licenses/*.txt)
set(PBSTORE_LICENSE_FILES ${CMAKE_SOURCE_DIR}/LICENSE ${PBSTORE_LICENSE_TEXTS})
set(PBSTORE_LICENSES_QRC ${CMAKE_CURRENT_BINARY_DIR}/licenses.qrc)
set(_licenses_qrc "<RCC>\n    <qresource prefix=\"/licenses\">\n        <file alias=\"LICENSE\">${CMAKE_SOURCE_DIR}/LICENSE</file>\n")
foreach(_text ${PBSTORE_LICENSE_TEXTS})
    get_filename_component(_name ${_text} NAME)
    string(APPEND _licenses_qrc "        <file alias=\"licenses/${_name}\">${_text}</file>\n")
endforeach()
string(APPEND _licenses_qrc "    </qresource>\n</RCC>\n")
file(WRITE ${PBSTORE_LICENSES_QRC}.tmp "${_licenses_qrc}")
execute_process(COMMAND ${CMAKE_COMMAND} -E copy_if_different ${PBSTORE_LICENSES_QRC}.tmp ${PBSTORE_LICENSES_QRC})
set(PBSTORE_APP_SOURCES main.cpp device_bridge.cpp)

if(NOT TARGET pbstore_bridge)
    message(STATUS "pbstore_bridge is not built: no store program")
elseif(PBSTORE_TARGET STREQUAL "ARM")
    # the program for the reader: SDK Qt by absolute library paths, manual moc and rcc; packaged as applications/pbstore.bin
    pbstore_moc(PBSTORE_APP_MOC device_bridge.h)
    pbstore_rcc(PBSTORE_QML_RCC qml ${CMAKE_SOURCE_DIR}/qml/qml.qrc ${PBSTORE_QML_FILES})
    pbstore_rcc(PBSTORE_LICENSES_RCC licenses ${PBSTORE_LICENSES_QRC} ${PBSTORE_LICENSE_FILES})
    add_executable(pbstore ${PBSTORE_APP_SOURCES} ${PBSTORE_APP_MOC} ${PBSTORE_QML_RCC} ${PBSTORE_LICENSES_RCC}
                   $<TARGET_OBJECTS:pbstore_app_config> $<TARGET_OBJECTS:pbstore_install_config>)
    target_include_directories(pbstore SYSTEM PRIVATE ${PBSTORE_QT_INCLUDES})
    target_include_directories(pbstore PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/third_party/cacert/include)
    target_compile_definitions(pbstore PRIVATE PBSTORE_ON_READER PBSTORE_APP_VERSION="${PBSTORE_APP_VERSION}")
    # libinkview lives outside the default sysroot library directories
    target_link_directories(pbstore PRIVATE ${SYSROOT}/usr/local/lib)
    target_link_libraries(pbstore PRIVATE pbstore_bridge pbstore_platform pbstore_cacert ${PBSTORE_QT_LIBS} pthread inkview)
    # Qt's own dependencies and libcurl's are found next to them at link time
    target_link_options(pbstore PRIVATE -Wl,-rpath-link,${QT6_ROOT}/lib -Wl,-rpath-link,${SYSROOT}/usr/lib)
else()
    # the host test build: never installed or packaged. It has the installer's test configuration (a throw-away key, overrides read from
    # the environment) and stand-ins for the reader's devices, so that the scripted walks and the screenshots can run on a computer.
    find_package(Qt6 QUIET COMPONENTS Core Gui Qml Quick Test)
    if(Qt6Quick_FOUND AND Qt6Qml_FOUND AND Qt6Test_FOUND AND TARGET pbstore_install_config_test)
        set(CMAKE_AUTOMOC ON)
        set(CMAKE_AUTORCC ON)
        add_executable(pbstore-app-test ${PBSTORE_APP_SOURCES} ui_script.cpp ${CMAKE_SOURCE_DIR}/qml/qml.qrc ${PBSTORE_LICENSES_QRC}
                       $<TARGET_OBJECTS:pbstore_app_config> $<TARGET_OBJECTS:pbstore_install_config_test>)
        set_property(SOURCE ${CMAKE_SOURCE_DIR}/qml/qml.qrc PROPERTY OBJECT_DEPENDS ${PBSTORE_QML_FILES})
        set_property(SOURCE ${PBSTORE_LICENSES_QRC} PROPERTY OBJECT_DEPENDS ${PBSTORE_LICENSE_FILES})
        target_include_directories(pbstore-app-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/third_party/cacert/include)
        target_compile_definitions(pbstore-app-test PRIVATE PBSTORE_TEST_BUILD PBSTORE_APP_VERSION="${PBSTORE_APP_VERSION}")
        target_link_libraries(pbstore-app-test PRIVATE pbstore_bridge pbstore_cacert Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Test)   # Qt6::Test only for the host walk (ui_script.cpp)
        add_dependencies(pbstore-app-test pbstore_test_key)
        # the pages must load without a QML error (offscreen platform, no display needed)
        add_test(NAME app_self_test COMMAND pbstore-app-test --self-test WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
        set_tests_properties(app_self_test PROPERTIES LABELS "bridge" SKIP_RETURN_CODE 77
                             ENVIRONMENT "QT_QPA_PLATFORM=offscreen;PBSTORE_TEST_READER_ROOT=${CMAKE_CURRENT_BINARY_DIR}/self-test-reader")
        if(PBSTORE_SANITIZE)
            set_property(TEST app_self_test APPEND PROPERTY ENVIRONMENT "LSAN_OPTIONS=suppressions=${CMAKE_SOURCE_DIR}/tests/lsan.supp:print_suppressions=0")
        endif()
    endif()
endif()
