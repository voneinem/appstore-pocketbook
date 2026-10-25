# pbstore_install_ui: the assistant (loopback HTTP server, JSON API, embedded page). Linked into the
find_package(Threads REQUIRED)
# single pbstore-install program; it contains no install logic.
include(${CMAKE_CURRENT_SOURCE_DIR}/embed_assets.cmake)
pbstore_embed_assets(${CMAKE_CURRENT_SOURCE_DIR}/assets ${CMAKE_CURRENT_BINARY_DIR}/generated/assets.inc)
file(GLOB PBSTORE_INSTALL_UI_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
add_library(pbstore_install_ui STATIC ${PBSTORE_INSTALL_UI_SOURCES})
target_include_directories(pbstore_install_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(pbstore_install_ui PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_link_libraries(pbstore_install_ui PUBLIC pbstore_install Threads::Threads)
