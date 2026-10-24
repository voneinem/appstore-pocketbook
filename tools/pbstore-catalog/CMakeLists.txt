# pbstore-catalog: the command line over pbstore_core (keygen, sign, verify, pack, check-package,
# check-entry, check-pr-paths, build). Developers, the pull-request check and the publish workflow
# all run this program, so they get the same verdicts.
file(GLOB PBSTORE_CLI_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
add_executable(pbstore-catalog ${PBSTORE_CLI_SOURCES})
target_link_libraries(pbstore-catalog PRIVATE pbstore_core)
