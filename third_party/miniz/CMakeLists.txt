# Vendored miniz (pinned, see VERSION): ZIP and deflate reading/writing.
# Built without its own warnings and without stdio file helpers we do not use.
add_library(pbstore_miniz STATIC miniz.c)
target_include_directories(pbstore_miniz PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(pbstore_miniz PRIVATE -w)
# keep the zlib-compatible names (compress, crc32, ...) out of our code; we use the mz_/tdefl_ names only
target_compile_definitions(pbstore_miniz PUBLIC MINIZ_NO_ZLIB_COMPATIBLE_NAMES)
