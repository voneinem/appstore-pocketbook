// T026: MemAvailable from /proc/meminfo, parsed from text; the reader class reads a file path it is given.
#include "pbstore/store/memory_info.h"
#include "pbstore/store/proc_meminfo.h"
#include "test_support.h"
#include "tmp_dir.h"

using namespace pbstore::store;

namespace {
const char* kMeminfo =
    "MemTotal:        1953124 kB\n"
    "MemFree:          123456 kB\n"
    "MemAvailable:     123456 kB\n"
    "Buffers:            1000 kB\n";
}

PB_TEST(mem_available_is_converted_from_kilobytes_to_bytes) {
    auto v = parse_mem_available(kMeminfo);
    PB_CHECK(v.has_value());
    PB_CHECK_EQ(*v, uint64_t(126418944));                          // 123456 kB
}

PB_TEST(a_text_without_the_line_or_with_a_garbled_number_gives_nothing) {
    PB_CHECK(!parse_mem_available("").has_value());
    PB_CHECK(!parse_mem_available("MemTotal: 100 kB\nMemFree: 50 kB\n").has_value());
    PB_CHECK(!parse_mem_available("MemAvailable: lots kB\n").has_value());
    PB_CHECK(!parse_mem_available("MemAvailable:\n").has_value());
    PB_CHECK(!parse_mem_available("MemAvailable: -5 kB\n").has_value());
    PB_CHECK(!parse_mem_available("MemAvailable: 99999999999999999999999 kB\n").has_value());
    PB_CHECK(parse_mem_available("MemAvailable: 0 kB\n") == std::optional<uint64_t>(0));
    PB_CHECK(!parse_mem_available("XMemAvailable: 5 kB\n").has_value());   // only the line of that name
}

PB_TEST(the_reader_class_reads_the_file_it_is_given_and_nothing_else) {
    pbtest::TmpDir tmp;
    tmp.write("meminfo", kMeminfo);
    ProcMeminfo from_file(tmp.file("meminfo"));
    PB_CHECK(from_file.available_bytes() == std::optional<uint64_t>(126418944));
    ProcMeminfo missing(tmp.file("nothing"));
    PB_CHECK(!missing.available_bytes().has_value());
    ProcMeminfo real;                                              // the default is /proc/meminfo
    auto v = real.available_bytes();
    PB_CHECK(v.has_value());                                       // this computer is Linux
    PB_CHECK(*v > 0);
}

PB_TEST_MAIN()
