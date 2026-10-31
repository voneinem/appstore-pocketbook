# pbstore_store: the Qt-free policies and the façade of the store app on the reader (catalog cache, battery and
# memory gates, Wi-Fi session, launcher state, rule table, store_service). It sits on top of pbstore_install and
# contains no install logic and no reader implementation of an operating-system interface that the engine already
# has; it is Qt-free, InkView-free and SDK-free (constitution Principle VI, scripts/check-constitution.sh R4, R14,
# R18, R19, R20). Sources are globbed so parallel work on separate files does not collide on this file.

file(GLOB PBSTORE_STORE_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)
add_library(pbstore_store STATIC ${PBSTORE_STORE_SOURCES})
target_include_directories(pbstore_store PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(pbstore_store PUBLIC pbstore_install)
