// T035: the early refusals before a download (research R-08): storage space from the signed package size, and memory,
// which holds the package. Two conditions, two ids.
#include "pbstore/store/memory_gate.h"
#include "test_support.h"

using namespace pbstore::store;

namespace {
constexpr uint64_t kMiB = 1024ull * 1024ull;
bool has(const std::string& t, const std::string& n) { return t.find(n) != std::string::npos; }
}  // namespace

PB_TEST(the_margins_are_named_constants) {
    PB_CHECK_EQ(kSpaceMargin, 10 * kMiB);
    PB_CHECK_EQ(kMemoryMargin, 32 * kMiB);
}

PB_TEST(storage_below_the_package_plus_the_margin_is_refused_exactly_at_the_boundary) {
    const uint64_t size = 5 * kMiB;
    PB_CHECK(decide_memory(size, size + kSpaceMargin, 512 * kMiB).result == MemoryResult::Ok);
    MemoryDecision d = decide_memory(size, size + kSpaceMargin - 1, 512 * kMiB);
    PB_CHECK(d.result == MemoryResult::NoSpace);
    PB_CHECK_EQ(d.needed, size + kSpaceMargin);
    PB_CHECK_EQ(d.available, size + kSpaceMargin - 1);
    MemoryFinding f = memory_finding(d);
    PB_CHECK_EQ(f.finding.rule_id, std::string("E-INS-07"));
}

PB_TEST(memory_below_twice_the_package_plus_the_margin_is_refused_exactly_at_the_boundary) {
    const uint64_t size = 5 * kMiB;
    const uint64_t need = 2 * size + kMemoryMargin;
    PB_CHECK(decide_memory(size, 1000 * kMiB, need).result == MemoryResult::Ok);
    MemoryDecision d = decide_memory(size, 1000 * kMiB, need - 1);
    PB_CHECK(d.result == MemoryResult::NoMemory);
    PB_CHECK_EQ(d.needed, need);
    PB_CHECK_EQ(d.available, need - 1);
    MemoryFinding f = memory_finding(d);
    PB_CHECK_EQ(f.finding.rule_id, std::string("E-STO-03"));
    PB_CHECK(has(f.finding.message, "MiB"));                         // needed and available are shown
}

PB_TEST(unknown_memory_is_ok_because_the_download_is_bounded_and_fails_with_a_message) {
    PB_CHECK(decide_memory(5 * kMiB, 100 * kMiB, std::nullopt).result == MemoryResult::Ok);
    PB_CHECK(decide_memory(5 * kMiB, 1 * kMiB, std::nullopt).result == MemoryResult::NoSpace);   // storage is still checked
}

PB_TEST(when_both_fail_the_storage_is_reported_first) {
    MemoryDecision d = decide_memory(5 * kMiB, 1 * kMiB, 1 * kMiB);
    PB_CHECK(d.result == MemoryResult::NoSpace);
}

PB_TEST(the_largest_package_needs_about_160_mib_of_memory) {
    const uint64_t size = 64 * kMiB;
    PB_CHECK_EQ(2 * size + kMemoryMargin, 160 * kMiB);
    PB_CHECK(decide_memory(size, 1000 * kMiB, 160 * kMiB).result == MemoryResult::Ok);
    PB_CHECK(decide_memory(size, 1000 * kMiB, 159 * kMiB).result == MemoryResult::NoMemory);
}

PB_TEST_MAIN()
