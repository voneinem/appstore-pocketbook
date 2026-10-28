// T044a (Principle VII): the state folder of the store is a contract. The names, limits and writers in
// core/store/include/pbstore/store/state_names.h are the single source and equal the tables in
// contracts/store-state-folder.md (this test parses the document). The part that runs a whole StoreService cycle on
// StoreRig and checks that the store touches only these names is added with the service (task T072).
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>

#include "pbstore/store/state_names.h"
#include "store_rig.h"
#include "test_support.h"

using namespace pbstore::store;

namespace {

std::string slurp(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

std::string contract_text() { return slurp(std::string(PBSTORE_SOURCE_DIR) + "/specs/003-pb-store-reader-app/contracts/store-state-folder.md"); }

// "1 MiB" -> bytes, "128 bytes", "512 KiB each"; 0 if the cell has no number
uint64_t parse_limit(const std::string& cell) {
    std::smatch m;
    if (!std::regex_search(cell, m, std::regex("([0-9]+) (bytes|KiB|MiB)"))) return 0;
    uint64_t n = std::stoull(m[1]);
    if (m[2] == "KiB") n *= 1024;
    if (m[2] == "MiB") n *= 1024 * 1024;
    return n;
}

// the "Files" table: | `name` | format | limit | on error |  (a format cell may hold a "|" without spaces around it)
std::map<std::string, uint64_t> files_table(const std::string& text) {
    std::map<std::string, uint64_t> rows;
    const size_t start = text.find("## Files");
    const size_t end = text.find("## Compatibility");
    std::istringstream in(text.substr(start, end - start));
    std::string line;
    while (std::getline(in, line)) {
        if (line.compare(0, 3, "| `") != 0) continue;
        std::vector<std::string> cells;
        size_t pos = 2;
        while (true) {
            const size_t sep = line.find(" | ", pos);
            cells.push_back(line.substr(pos, sep == std::string::npos ? std::string::npos : sep - pos));
            if (sep == std::string::npos) break;
            pos = sep + 3;
        }
        if (cells.size() < 3 || cells[0].size() < 3 || cells[0].front() != '`' || cells[0].back() != '`') continue;
        rows[cells[0].substr(1, cells[0].size() - 2)] = parse_limit(cells[2]);
    }
    return rows;
}

}  // namespace

PB_TEST(every_name_of_the_header_is_in_the_document_with_the_same_limit) {
    const auto doc = files_table(contract_text());
    PB_CHECK(!doc.empty());
    for (const StateName& n : state_names()) {
        auto it = doc.find(n.name);
        if (it == doc.end()) PB_FAIL_AT(std::string(n.name) + " is not in the document");
        PB_CHECK_EQ(it->second, n.limit);
    }
}

PB_TEST(every_name_of_the_document_is_in_the_header) {
    std::set<std::string> header;
    for (const StateName& n : state_names()) header.insert(n.name);
    for (const auto& kv : files_table(contract_text()))
        if (!header.count(kv.first)) PB_FAIL_AT(kv.first + " is documented but not in state_names.h");
}

PB_TEST(the_limits_are_the_documented_numbers) {
    auto limit = [](const char* name) {
        for (const StateName& n : state_names()) if (std::string(n.name) == name) return n.limit;
        return uint64_t(-1);
    };
    PB_CHECK_EQ(limit("catalog.json"), uint64_t(1024 * 1024));
    PB_CHECK_EQ(limit("catalog.json.sig"), uint64_t(1024));
    PB_CHECK_EQ(limit("catalog.meta"), uint64_t(128));
    PB_CHECK_EQ(limit("state.json"), uint64_t(256));
    PB_CHECK_EQ(limit("pending-note.txt"), uint64_t(256));
    PB_CHECK_EQ(limit("last-run-version"), uint64_t(32));
    PB_CHECK_EQ(limit("run/trying"), uint64_t(64));
    PB_CHECK_EQ(limit("run/last-exit"), uint64_t(8));
    PB_CHECK_EQ(limit("run/message"), uint64_t(128));
    PB_CHECK_EQ(limit("pbstore.log"), uint64_t(512 * 1024));
}

PB_TEST(the_names_the_store_program_may_write_are_the_ones_in_the_who_may_write_what_table) {
    const std::string text = contract_text();
    const size_t start = text.find("| The store program");
    PB_CHECK(start != std::string::npos);
    const size_t end = text.find('\n', start);
    const std::string row = text.substr(start, end - start);
    const size_t second_cell = row.find('|', 1);                       // skip the writer's own name (it names PosixStateDirs)
    PB_CHECK(second_cell != std::string::npos);
    const std::string names = row.substr(second_cell);
    std::set<std::string> documented;
    const std::regex ticks("`([^`]+)`");
    for (std::sregex_iterator it(names.begin(), names.end(), ticks), e; it != e; ++it) documented.insert((*it)[1]);
    std::set<std::string> in_header;
    for (const StateName& n : state_names())
        if (n.store_writes) in_header.insert(n.name);
    for (const std::string& d : documented)
        if (!in_header.count(d)) PB_FAIL_AT(d + " is documented as written by the store but the header says otherwise");
    for (const std::string& h : in_header)
        if (!documented.count(h)) PB_FAIL_AT(h + " is written by the store according to the header but the document does not say so");
}

PB_TEST(a_name_that_is_not_in_the_table_is_not_a_store_name) {
    PB_CHECK(is_store_name("catalog.json"));
    PB_CHECK(is_store_name("pbstore.log.1"));
    PB_CHECK(is_store_name("pbstore.log.2"));
    PB_CHECK(!is_store_name("pbstore.log.3"));
    PB_CHECK(!is_store_name("unknown.txt"));
    PB_CHECK(!is_store_name("../escape"));
    PB_CHECK(!is_store_name("catalog.json/x"));
}

PB_TEST(a_full_cycle_of_the_service_touches_only_the_documented_names_and_leaves_unknown_files_alone) {
    pbtest::StoreRig rig;
    if (!rig.ready()) PB_SKIP("openssl CLI not available");
    rig.host.add_app(pbtest::nested_sample_app("testapp", "1.2.0"));
    rig.host.publish(100);
    rig.write_reader();
    rig.make_engine();
    auto service = rig.make_service();
    // files the store did not make: in the app folder and in run/ (the launcher's)
    rig.storage.state->write_atomic("unknown.txt", "mine\n");
    rig.storage.state->write_atomic("pbstore.log.3", "not a name of ours\n");
    rig.run_dirs->write_atomic("last-exit", "0\n");
    rig.run_dirs->write_atomic("message", "update-failed 9.9.9\n");
    rig.run_dirs->write_atomic("pbstore.bin", "ELF");
    rig.radio.set_on(true);
    service->start_check();
    pbstore::install::CancelToken cancel;
    PB_CHECK(service->refresh(cancel, nullptr).ok);
    service->list_cached();
    service->settings_facts();
    service->request_leave(pbstore::store::LeaveReason::Home);
    const std::string app = rig.root() + "/system/pbstore/app";
    for (const auto& item : pbtest::snapshot(app)) {
        if (item.kind == 'd') continue;                                       // run/ is the launcher's folder
        const std::string name = item.path;
        if (name.compare(0, 4, "run/") == 0) continue;
        if (name == "unknown.txt" || name == "pbstore.log.3") continue;
        if (!is_store_name(name)) PB_FAIL_AT("the store made or left a file that is not in the table: " + name);
    }
    std::string text;                                                          // never written or deleted
    PB_CHECK(rig.storage.state->read("unknown.txt", &text));
    PB_CHECK_EQ(text, std::string("mine\n"));
    PB_CHECK(rig.storage.state->read("pbstore.log.3", &text));
    PB_CHECK_EQ(text, std::string("not a name of ours\n"));
    PB_CHECK(rig.run_dirs->size("last-exit") != UINT64_MAX);                   // the launcher's files stay
    PB_CHECK(rig.run_dirs->size("pbstore.bin") != UINT64_MAX);
    PB_CHECK(rig.run_dirs->size("message") == UINT64_MAX);                     // the one thing the store removes: the message it showed
}

PB_TEST_MAIN()
