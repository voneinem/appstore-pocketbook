# pbstore-install: the installer program (command line and assistant in one executable).
#
# pbstore-install        the released program. It has no way to change the catalog address, the
#                        trusted keys or the trust anchors (constitution Principle III, spec FR-006).
# pbstore-install-test   the same sources built with PBSTORE_TEST_BUILD. The tests drive it with a
#                        throw-away key, a test CA and a local catalog. It is never installed or shipped.
file(GLOB PBSTORE_INSTALL_CLI_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)

add_executable(pbstore-install ${PBSTORE_INSTALL_CLI_SOURCES})
target_link_libraries(pbstore-install PRIVATE pbstore_install pbstore_install_config)

add_executable(pbstore-install-test ${PBSTORE_INSTALL_CLI_SOURCES})
target_compile_definitions(pbstore-install-test PRIVATE PBSTORE_TEST_BUILD)
target_link_libraries(pbstore-install-test PRIVATE pbstore_install pbstore_install_config_test)

if(TARGET pbstore_install_ui)
    target_link_libraries(pbstore-install PRIVATE pbstore_install_ui)
    target_link_libraries(pbstore-install-test PRIVATE pbstore_install_ui)
endif()
