// T015: the test double CrashFs: an in-memory volume that counts every mutating call and loses power
// at a chosen call; the pessimistic mode drops what was not flushed.
#include "crash_fs.h"
#include "test_support.h"

using namespace pbstore::install;
using pbtest::CrashFs;
using pbtest::PowerLoss;

PB_TEST(it_works_as_an_in_memory_reader_volume) {
    CrashFs fs;
    PB_CHECK(fs.make_dir("d"));
    PB_CHECK(fs.create_exclusive("d/a", "AAA", true));
    PB_CHECK(!fs.create_exclusive("d/a", "BBB", true));
    PB_CHECK(fs.rename_free("d/a", "d/b"));
    PB_CHECK(!fs.stat("d/a").exists());
    std::string out;
    PB_CHECK(fs.read_file("d/b", 10, &out));
    PB_CHECK_EQ(out, std::string("AAA"));
    PB_CHECK(!fs.remove_dir("d"));
    PB_CHECK(fs.remove_file("d/b"));
    PB_CHECK(fs.remove_dir("d"));
    Space sp = fs.space();
    PB_CHECK(sp.writable);
}

PB_TEST(every_mutating_call_is_counted) {
    CrashFs fs;
    PB_CHECK_EQ(fs.mutating_calls(), 0);
    fs.make_dir("d");                        // 1
    fs.create_exclusive("d/a", "x", true);   // 2
    fs.rename_free("d/a", "d/b");            // 3
    fs.flush_dir("d");                       // 4
    fs.flush_volume();                       // 5
    fs.remove_file("d/b");                   // 6
    fs.remove_dir("d");                      // 7
    PB_CHECK_EQ(fs.mutating_calls(), 7);
    std::string out;
    fs.stat("d"); fs.list(""); fs.read_file("d/b", 1, &out); fs.space();    // reads do not count
    PB_CHECK_EQ(fs.mutating_calls(), 7);
}

PB_TEST(power_loss_at_call_n_applies_the_calls_before_it_and_throws) {
    CrashFs fs;
    fs.lose_power_at(3, /*pessimistic=*/false);
    fs.make_dir("d");
    fs.create_exclusive("d/a", "x", false);
    bool thrown = false;
    try { fs.create_exclusive("d/b", "y", true); } catch (const PowerLoss&) { thrown = true; }
    PB_CHECK(thrown);
    PB_CHECK(fs.lost());
    // nothing is possible until the reboot
    bool again = false;
    try { fs.stat("d"); } catch (const PowerLoss&) { again = true; }
    PB_CHECK(again);
    auto tree = fs.reboot();
    PB_CHECK(!fs.lost());
    PB_CHECK(fs.stat("d/a").kind == Kind::File);           // call 2 survived (clean mode keeps the data)
    PB_CHECK(!fs.stat("d/b").exists());                    // call 3 was not applied
    PB_CHECK_EQ(tree.files.count("d/a"), size_t(1));
    PB_CHECK_EQ(tree.files["d/a"], std::string("x"));
    PB_CHECK(tree.dirs.count("d") == 1);
}

PB_TEST(in_the_pessimistic_mode_unflushed_data_is_lost) {
    CrashFs fs;
    fs.lose_power_at(0, true);                              // 0: only the mode, no loss yet
    fs.make_dir("d");
    fs.create_exclusive("d/flushed", "keep", /*flush=*/true);
    fs.create_exclusive("d/unflushed", "lose", /*flush=*/false);
    fs.flush_dir("d");                                      // the entries are durable, one file's data is not
    fs.flush_dir("");                                       // and so is the directory itself
    fs.lose_power_now();
    fs.reboot();
    std::string out;
    PB_CHECK(fs.read_file("d/flushed", 10, &out));
    PB_CHECK_EQ(out, std::string("keep"));
    PB_CHECK(fs.stat("d/unflushed").exists());              // the name exists ...
    PB_CHECK(fs.read_file("d/unflushed", 10, &out));
    PB_CHECK_EQ(out, std::string(""));                      // ... with zero bytes
}

PB_TEST(in_the_pessimistic_mode_directory_entries_not_covered_by_a_flush_are_reverted) {
    CrashFs fs;
    fs.lose_power_at(0, true);
    fs.make_dir("d");
    fs.create_exclusive("d/a", "A", true);
    fs.flush_dir("d");
    fs.flush_dir("");                                       // the root lists "d"
    fs.rename_free("d/a", "d/b");                           // not flushed
    fs.create_exclusive("d/c", "C", true);                  // not flushed
    fs.make_dir("e");                                       // not flushed
    fs.lose_power_now();
    fs.reboot();
    PB_CHECK(fs.stat("d/a").exists());                      // the rename is undone
    PB_CHECK(!fs.stat("d/b").exists());
    PB_CHECK(!fs.stat("d/c").exists());                     // the new name was never durable
    PB_CHECK(!fs.stat("e").exists());
    std::string out;
    PB_CHECK(fs.read_file("d/a", 5, &out));
    PB_CHECK_EQ(out, std::string("A"));
}

PB_TEST(flush_volume_makes_everything_durable) {
    CrashFs fs;
    fs.lose_power_at(0, true);
    fs.make_dir("d");
    fs.create_exclusive("d/a", "A", false);
    fs.rename_free("d/a", "d/b");
    fs.flush_volume();
    fs.remove_file("d/b");                                  // not flushed
    fs.lose_power_now();
    fs.reboot();
    std::string out;
    PB_CHECK(fs.read_file("d/b", 5, &out));                 // the removal is undone, the data is there
    PB_CHECK_EQ(out, std::string("A"));
}

PB_TEST(the_loss_point_is_counted_from_the_reboot) {
    CrashFs fs;
    fs.make_dir("d");
    fs.lose_power_at(2, false);
    fs.create_exclusive("d/a", "A", true);
    bool thrown = false;
    try { fs.create_exclusive("d/b", "B", true); } catch (const PowerLoss&) { thrown = true; }
    PB_CHECK(thrown);
    fs.reboot();
    PB_CHECK_EQ(fs.mutating_calls(), 0);
    fs.lose_power_at(1, false);                             // a loss inside the recovery
    thrown = false;
    try { fs.create_exclusive("d/c", "C", true); } catch (const PowerLoss&) { thrown = true; }
    PB_CHECK(thrown);
}

PB_TEST(a_failure_can_be_injected_without_losing_power) {
    CrashFs fs;
    fs.make_dir("d");
    fs.fail_mutating_call(2);                               // the 2nd call from now returns false
    PB_CHECK(fs.create_exclusive("d/a", "A", true));
    PB_CHECK(!fs.create_exclusive("d/b", "B", true));
    PB_CHECK(!fs.stat("d/b").exists());
    PB_CHECK(fs.create_exclusive("d/c", "C", true));
    fs.set_read_failure("d/a");
    std::string out;
    PB_CHECK(!fs.read_file("d/a", 10, &out));
    fs.set_space(1000, 4096, true, false);
    PB_CHECK_EQ(fs.space().free_bytes, uint64_t(1000));
    fs.set_space(1000, 4096, false, true);
    PB_CHECK(!fs.space().writable);
    PB_CHECK(fs.space().read_only);
}

PB_TEST(the_lock_is_exclusive_among_handles_of_the_same_volume) {
    CrashFs fs;
    auto a = fs.lock();
    PB_CHECK(a != nullptr);
    PB_CHECK(fs.lock() == nullptr);
    a.reset();
    PB_CHECK(fs.lock() != nullptr);
}

PB_TEST_MAIN()
