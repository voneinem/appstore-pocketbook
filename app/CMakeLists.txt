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
