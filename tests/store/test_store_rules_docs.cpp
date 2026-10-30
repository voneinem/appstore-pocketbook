// T039: the store's rule ids and messages (contracts/store-rules.md; spec FR-019). Every id is documented in
// docs/store-app-rules.md with the registry's words, every message has three parts, no text the store produces tells a user
// of the reader to use a computer, a drive, a cable, a keyboard or a command-line option (except "PB Installer on a
// computer"), and a message fits one screen.
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>

#include "pbstore/fetch.h"
#include "pbstore/install/catalog_session.h"
#include "pbstore/install/install_rules.h"
#include "pbstore/install/messages.h"
#include "pbstore/store/store_rules.h"
#include "test_support.h"

using namespace pbstore;
using namespace pbstore::install;
using namespace pbstore::store;

namespace {

std::string slurp(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}
bool has(const std::string& t, const std::string& n) { return t.find(n) != std::string::npos; }

struct DocRow { std::string what, reader, todo; };

// | `E-STO-01` | condition | reader | what to do |
std::map<std::string, DocRow> doc_rows(const std::string& text) {
    std::map<std::string, DocRow> rows;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        std::smatch m;
        if (!std::regex_match(line, m, std::regex("\\| `([EW]-STO-[0-9]{2})` \\| (.*) \\| (.*) \\| (.*) \\|"))) continue;
        rows[m[1]] = {m[2], m[3], m[4]};
    }
    return rows;
}

const char* effect_word(ReaderEffect e) {
    switch (e) {
        case ReaderEffect::Unchanged: return "unchanged";
        case ReaderEffect::Restored: return "restored to the previous state";
        case ReaderEffect::RepairNeeded: return "repair needed";
    }
    return "";
}

// Inherited ids the store can produce (those that cannot occur on the reader are listed in contracts/store-rules.md).
std::vector<std::string> producible_inherited_ids() {
    std::vector<std::string> ids;
    for (const auto& r : install_rule_registry()) {
        const std::string id = r.id;
        if (id == "E-INS-01" || id == "E-INS-02" || id == "E-INS-04" || id == "E-INS-27" || id == "E-INS-29") continue;
        ids.push_back(id);
    }
    for (const char* id : {"E-CAT-01", "E-CAT-02", "E-CAT-03", "E-CAT-04", "E-CAT-05"}) ids.push_back(id);
    return ids;
}

}  // namespace

PB_TEST(the_registry_holds_exactly_the_documented_new_ids) {
    const auto& reg = store_rule_registry();
    std::set<std::string> ids;
    for (const auto& r : reg) PB_CHECK(ids.insert(r.id).second);
    PB_CHECK_EQ(reg.size(), size_t(13));
    for (int i = 1; i <= 10; ++i) {
        char buf[16];
        std::snprintf(buf, sizeof buf, "E-STO-%02d", i);
        PB_CHECK(ids.count(buf) == 1);
    }
    for (int i = 1; i <= 3; ++i) {
        char buf[16];
        std::snprintf(buf, sizeof buf, "W-STO-%02d", i);
        PB_CHECK(ids.count(buf) == 1);
    }
}

PB_TEST(every_error_has_all_three_parts_and_a_notice_has_what_and_effect) {
    for (const auto& r : store_rule_registry()) {
        const std::string id = r.id;
        PB_CHECK(std::string(r.what).size() > 10);
        PB_CHECK(std::string(r.what).find('\n') == std::string::npos);
        if (id[0] == 'E') PB_CHECK(std::string(r.next).size() > 10);
        Finding f = id[0] == 'E' ? make_error(id, "subject", "detail") : make_notice(id, "subject", "detail");
        Message m = store_message(f);
        PB_CHECK(has(m.what, id));
        PB_CHECK(!m.reader_effect.empty());
        if (id[0] == 'E') PB_CHECK(!m.next.empty());
    }
}

PB_TEST(the_registry_and_docs_store_app_rules_md_say_the_same) {
    const auto rows = doc_rows(slurp(std::string(PBSTORE_SOURCE_DIR) + "/docs/store-app-rules.md"));
    PB_CHECK_EQ(rows.size(), store_rule_registry().size());
    for (const auto& r : store_rule_registry()) {
        auto it = rows.find(r.id);
        PB_CHECK(it != rows.end());
        if (it == rows.end()) continue;
        PB_CHECK_EQ(it->second.what, std::string(r.what));
        PB_CHECK_EQ(it->second.reader, std::string(effect_word(r.effect)));
        PB_CHECK_EQ(it->second.todo, std::string(r.next).empty() ? std::string("–") : std::string(r.next));
    }
}

PB_TEST(the_same_registry_is_also_in_the_contract_of_the_plan) {
    const auto rows = doc_rows(slurp(std::string(PBSTORE_SOURCE_DIR) + "/specs/003-pb-store-reader-app/contracts/store-rules.md"));
    PB_CHECK_EQ(rows.size(), store_rule_registry().size());
    for (const auto& r : store_rule_registry()) {
        auto it = rows.find(r.id);
        PB_CHECK(it != rows.end());
        if (it != rows.end()) PB_CHECK_EQ(it->second.what, std::string(r.what));
    }
}

PB_TEST(no_text_the_store_produces_names_a_computer_a_drive_a_cable_a_keyboard_or_an_option) {
    for (const auto& r : store_rule_registry()) {
        PB_CHECK(!tells_the_user_to_use_a_computer(std::string(r.what) + " " + r.next));
    }
    for (const std::string& id : producible_inherited_ids()) {
        for (ReaderEffect effect : {ReaderEffect::Unchanged, ReaderEffect::Restored, ReaderEffect::RepairNeeded}) {
            Finding f = make_error(id, "subject", "something specific happened");
            const std::string text = store_message(f, effect).text();
            if (tells_the_user_to_use_a_computer(text)) PB_FAIL_AT(id + " produced: " + text);
            PB_CHECK(has(text, id));
        }
    }
}

PB_TEST(the_installers_download_messages_lose_their_computer_but_keep_their_distinct_causes) {
    // the real texts of download_finding for every way a download can fail
    struct Case { FetchKind kind; const char* error; const char* cause; };
    const Case cases[] = {
        {FetchKind::Unreachable, "could not resolve host", "cannot reach"},
        {FetchKind::Tls, "certificate has expired", "certificate"},
        {FetchKind::Protocol, "redirect to http", "redirected"},
        {FetchKind::TooLarge, "limit", "larger"},
        {FetchKind::Timeout, "no data", "made no progress"},
        {FetchKind::Timeout, "total time", "took too long"},
        {FetchKind::HttpStatus, "", "status"},
        {FetchKind::Other, "boom", "failed"},
    };
    for (const Case& c : cases) {
        FetchResult r;
        r.ok = false;
        r.kind = c.kind;
        r.error = c.error;
        r.http_status = 404;
        Finding f = download_finding(r, "the catalog", "https://example.org/catalog.json");
        const std::string text = store_message(f).text();
        if (tells_the_user_to_use_a_computer(text)) PB_FAIL_AT(std::string("download message names a computer: ") + text);
        PB_CHECK(has(text, c.cause));
    }
}

PB_TEST(a_certificate_message_names_the_certificate_and_the_clock_and_offers_no_way_round) {
    FetchResult r;
    r.ok = false;
    r.kind = FetchKind::Tls;
    r.error = "certificate verify failed";
    const std::string text = store_message(download_finding(r, "the catalog", "https://example.org/catalog.json")).text();
    PB_CHECK(has(text, "certificate"));
    PB_CHECK(has(text, "clock"));
    for (const char* bad : {"ignore", "skip", "disable", "insecure", "bypass", "trust anyway", "accept the"}) PB_CHECK(!has(text, bad));
}

PB_TEST(specific_third_parts_for_the_ids_the_contract_names) {
    auto next = [](const char* id) { return store_message(make_error(id, "x", "y")).next; };
    PB_CHECK(has(next("E-INS-09"), "plan"));                                  // a confirmation on the plan page
    PB_CHECK(has(next("E-INS-11"), "plan"));
    PB_CHECK(has(next("E-INS-32"), "plan"));
    PB_CHECK(has(next("E-INS-34"), "Close it and try again"));
    PB_CHECK(has(next("E-INS-36"), "Wi-Fi"));
    PB_CHECK(has(next("E-CAT-03"), "PB Installer on a computer"));
    PB_CHECK(has(next("E-CAT-03"), "needs an update"));
    PB_CHECK(has(next("E-INS-35"), "PB Installer on a computer"));
    PB_CHECK(has(next("E-INS-12"), "system/pbstore/backups/"));
    PB_CHECK(has(next("E-INS-05"), "Restart the reader"));                    // the third part of E-STO-10
    PB_CHECK(has(next("E-INS-06"), "Restart the reader"));
    const std::string what26 = store_message(make_error("E-INS-26", "x", "The catalog or the package could not be downloaded.")).what;
    PB_CHECK(has(what26, "could not be downloaded"));
}

PB_TEST(the_third_part_follows_the_state_of_the_reader) {
    const std::string repair = store_message(make_error("E-INS-24", "x", "y")).next;
    PB_CHECK(has(repair, "Open PB Store again"));                             // repair needed
    const std::string repair23 = store_message(make_error("E-INS-23", "x", "y")).next;
    PB_CHECK(has(repair23, "Open PB Store again"));
    const std::string restored = store_message(make_error("E-INS-37", "x", "y")).next;
    PB_CHECK(has(restored, "Nothing needs doing"));                           // restored to the previous state
    PB_CHECK(has(restored, "as before"));
    const std::string unchanged = store_message(make_error("E-INS-07", "x", "y")).next;
    PB_CHECK(has(unchanged, "Free some space"));                              // unchanged: the step to try again
}

PB_TEST(a_cancel_by_the_user_is_told_as_a_cancel_and_not_as_an_error) {
    const Message m = store_message(make_error("E-INS-33", "operation", "cancelled by the user"), ReaderEffect::Restored);
    PB_CHECK(has(m.what, "You cancelled"));
    PB_CHECK(!has(m.what, "error["));
    PB_CHECK(has(m.what, "E-INS-33"));                                         // the id stays, behind the sentence, for the log and for support
    PB_CHECK(has(m.reader_effect, "restored"));
}

PB_TEST(a_message_is_at_most_600_characters_and_a_path_in_it_at_most_60) {
    const std::string long_path = "applications/" + std::string(150, 'a') + "/" + std::string(120, 'b') + ".bin";
    Finding f = make_error("E-INS-20", long_path, "the file " + long_path + " is in the way");
    const std::string text = store_message(f).text();
    PB_CHECK(text.size() <= 600);
    PB_CHECK(!has(text, std::string(61, 'a')));
    PB_CHECK(has(text, "E-INS-20"));
    PB_CHECK(has(text, "Next:"));                                             // the rule id and the third part are never cut
    // an enormous detail is cut in the middle of the first part, never at the ends
    Finding huge = make_error("E-INS-19", "x", std::string(5000, 'w'));
    const std::string t2 = store_message(huge).text();
    PB_CHECK(t2.size() <= 600);
    PB_CHECK(has(t2, "E-INS-19"));
    PB_CHECK(has(t2, "Next:"));
    PB_CHECK(has(t2, "The reader was not changed."));
    // a short message is not changed
    PB_CHECK_EQ(shorten_paths("see applications/a.bin now", 60), std::string("see applications/a.bin now"));
    const std::string shortened = shorten_paths("see " + long_path + " now", 60);
    PB_CHECK(shortened.size() <= 4 + 60 + 4);
    PB_CHECK(has(shortened, "..."));
}

PB_TEST(the_words_that_name_a_computer_are_found_and_the_allowed_phrase_is_not) {
    PB_CHECK(tells_the_user_to_use_a_computer("connect the USB cable"));
    PB_CHECK(tells_the_user_to_use_a_computer("use --confirm-repair"));
    PB_CHECK(tells_the_user_to_use_a_computer("the clock of the computer"));
    PB_CHECK(tells_the_user_to_use_a_computer("mount it again"));
    PB_CHECK(tells_the_user_to_use_a_computer("type on the keyboard"));
    PB_CHECK(tells_the_user_to_use_a_computer("the drive is full"));
    PB_CHECK(!tells_the_user_to_use_a_computer("Use PB Installer on a computer to remove PB Store."));
    PB_CHECK(!tells_the_user_to_use_a_computer("this amount of space"));       // "mount" inside a word is not a mount
    PB_CHECK(!tells_the_user_to_use_a_computer("Charge the reader and try again."));
}

PB_TEST_MAIN()
