// T049: one operation per reader, and a plan is valid only for the reader as it was when it was made.
#include "install_fixtures.h"
#include "test_support.h"

using namespace pbstore::install;
using namespace pbtest;

namespace {
bool has_rule(const ExecResult& r, const char* id) {
    for (const auto& f : r.findings)
        if (f.rule_id == id) return true;
    return false;
}
}  // namespace

PB_TEST(a_second_engine_on_the_same_reader_is_refused_while_the_first_holds_the_lock) {
    FakeReader reader;
    Volume v(reader);
    Built b = make_install_plan(v.fat, nested_sample_app());
    const Snapshot original = snapshot(v.fat);
    {
        auto held = v.fat.lock();
        PB_CHECK(held != nullptr);
        ExecResult res = run_execute(v.fat, b);
        PB_CHECK(!res.ok());
        PB_CHECK(has_rule(res, "E-INS-34"));
        PB_CHECK(res.outcome == ExecResult::Outcome::NotStarted);
        PB_CHECK(compare(original, snapshot(v.fat)).identical);
    }
    PB_CHECK(run_execute(v.fat, b).ok());      // after the first one ends, it works
}

PB_TEST(a_changed_desktop_configuration_gives_e_ins_25_and_writes_nothing) {
    FakeReader reader;
    Volume v(reader);
    Built b = make_install_plan(v.fat, nested_sample_app());
    // somebody (or the reader itself) changed view.json after the plan was made
    std::string view = reader_view_text(v.fat);
    view.insert(view.find("\"version\""), " ");
    PB_CHECK(v.fat.remove_file("system/config/desktop/view.json"));
    PB_CHECK(v.fat.create_exclusive("system/config/desktop/view.json", view, true));
    const Snapshot before = snapshot(v.fat);
    ExecResult res = run_execute(v.fat, b);
    PB_CHECK(!res.ok());
    PB_CHECK(has_rule(res, "E-INS-25"));
    PB_CHECK(res.outcome == ExecResult::Outcome::NotStarted);
    PB_CHECK(res.findings[0].message.find("desktop configuration") != std::string::npos);
    PB_CHECK(compare(before, snapshot(v.fat)).identical);
}

PB_TEST(a_new_colliding_file_gives_e_ins_25_and_names_it) {
    FakeReader reader;
    Volume v(reader);
    Built b = make_install_plan(v.fat, nested_sample_app());
    PB_CHECK(v.fat.create_exclusive(b.plan.items[0].path, "appeared meanwhile", true));
    const Snapshot before = snapshot(v.fat);
    ExecResult res = run_execute(v.fat, b);
    PB_CHECK(!res.ok());
    PB_CHECK(has_rule(res, "E-INS-25"));
    PB_CHECK(res.findings[0].message.find(b.plan.items[0].path) != std::string::npos);
    PB_CHECK(compare(before, snapshot(v.fat)).identical);
}

PB_TEST(a_different_free_space_figure_gives_e_ins_25_and_says_what_changed) {
    FakeReader reader;
    Volume v(reader);
    Built b = make_install_plan(v.fat, nested_sample_app());
    v.mem.set_space(b.plan.space.free_bytes - 3 * 1024 * 1024, 4096, true, false);   // still enough, but not what the plan saw
    const Snapshot before = snapshot(v.fat);
    ExecResult res = run_execute(v.fat, b);
    PB_CHECK(!res.ok());
    PB_CHECK(has_rule(res, "E-INS-25"));
    PB_CHECK(res.findings[0].message.find("free space") != std::string::npos);
    PB_CHECK(compare(before, snapshot(v.fat)).identical);
}

PB_TEST(an_interrupted_earlier_operation_has_to_be_repaired_first) {
    FakeReader reader;
    Volume v(reader);
    Built first = make_install_plan(v.fat, nested_sample_app());
    v.mem.lose_power_at(40, false);
    try { run_execute(v.fat, first); } catch (const PowerLoss&) {}
    v.mem.reboot();
    PB_CHECK(journal_exists(v.fat));
    Built second = make_install_plan(v.fat, nested_sample_app("otherapp", "1.0.0", "U_OtherApp"));
    const Snapshot before = snapshot(v.fat);
    ExecResult res = run_execute(v.fat, second);
    PB_CHECK(!res.ok());
    PB_CHECK(has_rule(res, "E-INS-23"));
    PB_CHECK(res.effect == ReaderEffect::RepairNeeded);
    PB_CHECK(compare(before, snapshot(v.fat)).identical);
}

PB_TEST_MAIN()
