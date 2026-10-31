// T084 (story 6): the controller at the start of a store whose last operation was interrupted. It exposes repairPending and repairText (what was
// found, finish or undo, why); the first screen is the repair prompt; while the repair is pending every write action ends in the refusal E-INS-23
// with its three parts (the "Not now" of the page only goes back to the list); startRepair runs on the worker thread with the phases
// executing and result; the result names what was done; afterwards the rows have their actions again and nothing is pending.
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>
#include <fstream>
#include <functional>
#include <sys/stat.h>

#include "store_controller.h"
#undef emit   // a Qt macro; the test support has a method of that name
#include "store_rig.h"
#include "test_support.h"
#include "throwing_fs.h"

using namespace pbstore;
using namespace pbtest;

namespace {

QCoreApplication& app() {
    static int argc = 1;
    static char name[] = "test";
    static char* argv[] = {name, nullptr};
    static QCoreApplication a(argc, argv);
    return a;
}

bool wait_for(const std::function<bool()>& done, int ms = 20000) {
    app();
    QElapsedTimer t;
    t.start();
    while (!done()) {
        if (t.elapsed() > ms) return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(5);
    }
    return true;
}


struct Rig {
    StoreRig rig;
    std::unique_ptr<store::StoreService> service;
    std::unique_ptr<StoreController> controller;
    explicit Rig(const FakeReader& r = FakeReader()) : rig(r) {}
    void make_controller() {
        controller = std::make_unique<StoreController>(*service, rig.screen, QStringLiteral("0.1.0"), QString::fromStdString(rig.root() + "/system/pbstore/app/pbstore.log"));
        controller->startUp();
    }
    // The reader as an install of testapp left it when the power was lost at the cut-th write; the store then starts again.
    bool prepare_interrupted(int cut) {
        app();
        if (!rig.ready()) return false;
        rig.host.add_app(nested_sample_app("testapp", "1.2.0"));
        rig.host.publish(100);
        rig.write_reader();
        rig.make_engine();
        service = rig.make_service();
        service->start_check();
        install::CancelToken c;
        if (!service->refresh(c, nullptr).ok) return false;
        const install::ReaderFsFactory normal = rig.storage.open_fs;
        rig.storage.open_fs = [cut, normal](const std::string& root) -> std::unique_ptr<install::ReaderFs> { return std::make_unique<ThrowingFs>(normal(root), cut); };
        rig.make_engine();
        service = rig.make_service();
        service->start_check();
        store::PlanView plan = service->plan_action(store::RowAction::Install, "testapp", c, nullptr);
        std::vector<std::string> given;
        for (const store::ConfirmationItem& i : plan.confirmations) given.push_back(i.name);
        try { service->execute_plan(given, false, c, nullptr); } catch (const PowerLoss&) {}
        rig.storage.open_fs = normal;
        rig.make_engine();
        service = rig.make_service();
        make_controller();
        rig.radio = FakeRadio();
        return controller->repairPending();
    }
    bool phase(const char* p) { return wait_for([&] { return controller->phase() == QLatin1String(p); }); }
};

// A cut that leaves a journal behind.
bool interrupted(Rig& r) {
    for (int cut = 8; cut < 60; ++cut) {
        Rig probe;
        if (!probe.rig.ready()) return false;
        if (probe.prepare_interrupted(cut)) return r.prepare_interrupted(cut);
    }
    return false;
}

}  // namespace

PB_TEST(the_controller_starts_with_the_repair_prompt_and_says_what_was_found_and_what_it_will_do) {
    Rig r;
    if (!r.rig.ready()) PB_SKIP("openssl CLI not available");
    PB_CHECK(interrupted(r));
    PB_CHECK(r.controller->repairPending());
    const QString text = r.controller->repairText();
    PB_CHECK(text.contains(QStringLiteral("testapp")));
    PB_CHECK(text.contains(QStringLiteral("interrupted")));
    PB_CHECK(text.contains(QStringLiteral("finish")) || text.contains(QStringLiteral("undo")));
    PB_CHECK(r.controller->notice().contains(QStringLiteral("repaired first")));        // the list says so too
    PB_CHECK(r.controller->phase() == QLatin1String("idle"));
    PB_CHECK(r.rig.radio.calls().empty());                                               // nothing used the network or the radio
    for (int i = 0; i < r.controller->model()->rowCount(); ++i) PB_CHECK(r.controller->model()->rows()[static_cast<size_t>(i)].actions.empty());   // browse only until the repair
}

PB_TEST(while_the_repair_is_pending_every_write_action_ends_in_the_refusal_with_its_three_parts) {
    Rig r;
    if (!r.rig.ready()) PB_SKIP("openssl CLI not available");
    PB_CHECK(interrupted(r));
    for (const char* action : {"Install", "Update", "Remove", "Check"}) {
        r.controller->startAction(QString::fromLatin1(action), QStringLiteral("testapp"));
        PB_CHECK(r.phase("result"));
        PB_CHECK(!r.controller->resultOk());
        PB_CHECK(r.controller->resultText().contains(QStringLiteral("E-INS-23")));
        PB_CHECK(!r.controller->resultReader().isEmpty());
        PB_CHECK(!r.controller->resultTodo().isEmpty());
        r.controller->finishResult();
        PB_CHECK(r.controller->phase() == QLatin1String("idle"));
        PB_CHECK(r.controller->repairPending());                                         // "Not now" changed nothing
    }
}

PB_TEST(confirming_the_repair_runs_it_names_what_was_done_and_gives_the_actions_back) {
    Rig r;
    if (!r.rig.ready()) PB_SKIP("openssl CLI not available");
    PB_CHECK(interrupted(r));
    r.controller->startRepair(false);
    PB_CHECK(r.controller->phase() == QLatin1String("executing"));
    PB_CHECK(r.phase("result"));
    PB_CHECK(r.controller->resultOk());
    PB_CHECK(r.controller->resultText().contains(QStringLiteral("interrupted")));
    PB_CHECK(r.controller->resultText().contains(QStringLiteral("finished")) || r.controller->resultText().contains(QStringLiteral("undone")));
    PB_CHECK(!r.controller->repairPending());
    PB_CHECK(r.rig.radio.calls().empty());                                               // a repair never touches Wi-Fi
    r.controller->finishResult();
    PB_CHECK(r.controller->phase() == QLatin1String("idle"));
    bool any_action = false;
    for (int i = 0; i < r.controller->model()->rowCount(); ++i) any_action = any_action || !r.controller->model()->rows()[static_cast<size_t>(i)].actions.empty();
    PB_CHECK(any_action);
}

PB_TEST(a_repair_below_the_battery_minimum_is_refused_and_a_second_tap_while_it_runs_is_ignored) {
    Rig r;
    if (!r.rig.ready()) PB_SKIP("openssl CLI not available");
    PB_CHECK(interrupted(r));
    r.rig.battery.set_percent(8);
    r.controller->startRepair(false);
    r.controller->startRepair(false);                                                    // ignored: one operation at a time
    PB_CHECK(r.phase("result"));
    PB_CHECK(!r.controller->resultOk());
    PB_CHECK(r.controller->resultText().contains(QStringLiteral("E-STO-01")));
    PB_CHECK(r.controller->repairPending());
}

PB_TEST_MAIN()
