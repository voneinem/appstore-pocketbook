// gen_demo_server: the local HTTPS server and the fixture catalog of the tests as a program, for the scripted UI walks and the host demo
// (scripts/ui-walk.sh, scripts/run-host-demo.sh; tasks T069, T079).
//
//   gen_demo_server [--scenario <name>] [--reader <dir>]
//
// The catalog has 14 apps (so that the list needs two pages), signed with the throw-away key of the test build (tests/CMakeLists:
// test_signing_seed.txt). With --reader the reader's storage of the scenario is written to <dir> first: `demo` (the apps below in each of the
// four states), `repair` (an update of app-02 that was cut by a lost power), `updates` (app-01 to app-03 and PB Store installed, a newer version of
// each offered). Then the server runs and prints one line
//
//   READY <catalog url> <ca file> <host:port>
//
// and reads commands from its standard input until it is closed:
//
//   publish <serial>   sign and serve the catalog again with that serial
//   broken <id>        serve garbage instead of the package of that app (the signed hash then does not match)
//   fixed <id>         serve the real package again
//   quit
//
// Nothing here talks to the internet; the server is a fake of the tests, so a walk proves the pages and their wiring, nothing about the real
// catalog or the reader.
#include <iostream>
#include <sstream>

#include "crash_fs.h"
#include "install_rig.h"

using namespace pbtest;

namespace {

std::string two(int n) { return (n < 10 ? "0" : "") + std::to_string(n); }
std::string app_id(int n) { return "app-" + two(n); }

// app-NN: its own files, menu entry and data folder
SampleApp make_app(int n, const std::string& version = "1.2.0", unsigned seed_offset = 0) {
    return make_sample_app(app_id(n), version, "U_App" + two(n), "system/" + app_id(n), static_cast<unsigned>(n) * 7 + seed_offset);
}

constexpr int kApps = 14;

// What the reader holds in the `demo` scenario:
//   app-01  installed by the installer, current (1.2.0), with a data folder          -> Installed
//   app-02  installed 1.2.0, the catalog offers 1.3.0                                 -> Update available
//   app-03  put there by hand, its files equal the catalog package                    -> Installed outside the store (Check, Adopt)
//   app-05  put there by hand, one file altered                                       -> Installed outside the store (Check names the file)
//   zz-extra  put there by hand, not in the catalog                                   -> Not in the catalog
//   the others are not on the reader                                                  -> Not installed
FakeReader demo_reader() {
    FakeReader r;
    r = with_installed(r, make_app(1));
    r.add_data_dir(app_id(1), {{"notes.db", "my notes"}, {"sub/more.db", "more of my notes"}});
    r = with_installed(r, make_app(2));
    r.add_data_dir(app_id(2), {{"user.txt", "my data"}});
    r.add_hand_installed(make_app(3));
    r.add_hand_installed(make_app(5));
    r.files["applications/" + app_id(5) + ".bin"] = "edited by hand";
    SampleApp extra = make_sample_app("zz-extra", "0.9.0", "U_ZzExtra", "", 4242);
    r.add_hand_installed(extra);
    return r;
}

// The demo reader with the update of app-02 cut by a lost power: a journal and a half-done change are left for the repair.
FakeReader repair_reader() {
    FakeReader base = demo_reader();
    Volume v(base);
    Built b = make_planned(v.fat, make_app(2, "1.3.0", 50), PlanKind::Update);
    for (int cut = 30; cut < 90; ++cut) {
        Volume w(base);
        Built bb = make_planned(w.fat, make_app(2, "1.3.0", 50), PlanKind::Update);
        w.mem.lose_power_at(cut, false);
        try { run_execute(w.fat, bb); } catch (const PowerLoss&) {}
        w.mem.reboot();
        if (!journal_exists(w.fat)) continue;
        FakeReader out = base;
        out.assign_tree(w.mem.tree());
        return out;
    }
    throw std::runtime_error("no cut left a journal");
}

// The `updates` reader: four apps installed, PB Store itself among them (its package is id pbstore, no data folder).
SampleApp store_app(const std::string& version, unsigned seed) { return make_sample_app("pbstore", version, "U_PbStore", "", seed); }

FakeReader updates_reader() {
    FakeReader r;
    for (int n = 1; n <= 3; ++n) r = with_installed(r, make_app(n));
    return with_installed(r, store_app("0.0.1", 900));
}

}  // namespace

int main(int argc, char** argv) {
    std::string scenario = "demo", reader_dir;
    for (int i = 1; i + 1 < argc; i += 2) {
        const std::string a = argv[i];
        if (a == "--scenario") scenario = argv[i + 1];
        else if (a == "--reader") reader_dir = argv[i + 1];
        else { std::cerr << "usage: gen_demo_server [--scenario demo|repair|updates] [--reader <dir>]\n"; return 2; }
    }
    if (scenario != "demo" && scenario != "repair" && scenario != "updates") { std::cerr << "unknown scenario: " << scenario << "\n"; return 2; }

    CatalogHost host(key_from_seed_file(PBSTORE_TEST_SEED_FILE));
    if (!host.ready()) { std::cerr << "the openssl CLI is needed for the demo server's throw-away certificates\n"; return 77; }
    std::map<std::string, std::string> real_packages;   // package path -> body, to put back after `broken`
    for (int n = 1; n <= kApps; ++n) {
        const bool newer = n == 2 || (scenario == "updates" && n <= 3);   // the catalog offers 1.3.0 and lists 1.2.0 as an earlier version
        if (newer) {
            pbstore::CatalogEntry& e = host.add_app(make_app(n, "1.3.0", 50));
            e.listed_versions = {"1.2.0", "1.3.0"};
        } else {
            host.add_app(make_app(n));
        }
    }
    if (scenario == "updates") {   // PB Store itself is offered too, and is updated last
        pbstore::CatalogEntry& e = host.add_app(store_app("0.2.0", 901));
        e.listed_versions = {"0.0.1", "0.2.0"};
    }
    host.publish(100);
    for (const auto& kv : host.files) real_packages[kv.first] = kv.second;

    if (!reader_dir.empty()) {
        FakeReader reader = scenario == "repair" ? repair_reader() : scenario == "updates" ? updates_reader() : demo_reader();
        reader.write_to(reader_dir);
    }
    std::cout << "READY " << host.catalog_url() << " " << host.ca_file() << " " << host.host_port() << std::endl;

    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream in(line);
        std::string cmd, arg;
        in >> cmd >> arg;
        if (cmd == "quit") break;
        if (cmd == "publish") host.publish(std::atoll(arg.c_str()));
        else if (cmd == "broken" || cmd == "fixed") {
            for (const auto& e : host.catalog.apps)
                if (e.id == arg) host.set_raw(package_path_of(e), cmd == "broken" ? "this is not the package" : real_packages[package_path_of(e)]);
        }
        std::cout << "OK " << line << std::endl;
    }
    return 0;
}
