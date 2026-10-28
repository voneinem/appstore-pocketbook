# The reader's interface: the only code that includes inkview.h (radio, battery, screen, identity), the launcher pbstore.app and
# the probe pbstore-probe. Each function is one InkView call plus the conversions of contracts/device-interfaces.md (scripts/
# check-constitution.sh R18 and R19 keep install logic out of here).
#
#   ARM  pbstore_platform       the wrappers against the SDK's inkview.h, linked into the store program
#   HOST pbstore_platform_shim  the same sources against tests/support/inkview_shim/inkview.h, for tests/store/test_platform_wrappers

set(PBSTORE_PLATFORM_SOURCES radio.cpp battery.cpp screen.cpp identity.cpp)

if(PBSTORE_TARGET STREQUAL "ARM")
    add_library(pbstore_platform STATIC ${PBSTORE_PLATFORM_SOURCES})
    target_include_directories(pbstore_platform SYSTEM PUBLIC ${SYSROOT}/usr/local/include)
    target_include_directories(pbstore_platform PUBLIC ${CMAKE_SOURCE_DIR})
    target_link_libraries(pbstore_platform PUBLIC pbstore_store)
else()
    add_library(pbstore_platform_shim STATIC ${PBSTORE_PLATFORM_SOURCES})
    target_include_directories(pbstore_platform_shim PUBLIC ${CMAKE_SOURCE_DIR}/tests/support/inkview_shim ${CMAKE_SOURCE_DIR})
    target_link_libraries(pbstore_platform_shim PUBLIC pbstore_store)
endif()

# pbstore-probe: a console program (the store's Qt-free library and the C library only, no Qt) that prints what the engine's calls do
# on the volume it is run on (device task DS-01). Built on the computer for the host test and with the SDK for the reader, where
# it is installed as applications/pbstore-probe.bin in a dev package.
add_executable(pbstore-probe pbstore-probe.cpp)
target_link_libraries(pbstore-probe PRIVATE pbstore_store)
if(PBSTORE_TARGET STREQUAL "ARM")
    target_compile_definitions(pbstore-probe PRIVATE PBSTORE_ON_READER)
    # libinkview lives outside the default sysroot library directories
    target_link_directories(pbstore-probe PRIVATE ${SYSROOT}/usr/local/lib)
    target_link_libraries(pbstore-probe PRIVATE inkview)
    target_include_directories(pbstore-probe SYSTEM PRIVATE ${SYSROOT}/usr/local/include)
endif()
