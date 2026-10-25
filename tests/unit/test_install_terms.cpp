// T110 (spec FR-037, terms): the same words everywhere - reader, app, package, catalog, installed outside the
// installer, adopt, repair, data folder - and none of their synonyms; sizes in KiB/MiB/GiB, dates as
// YYYY-MM-DD; no advice to use elevated rights.
#include "install_rig.h"
#include "pbstore/install/messages.h"
#include "pbstore/install/planner.h"
#include "test_support.h"

#include <dirent.h>
#include <regex>
#include <sys/stat.h>

using namespace pbstore;
using namespace pbstore::install;
using namespace pbtest;

namespace {

std::string slurp(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// words that must not appear as words in user-facing text (case-insensitive)
const char* kAvoided[] = {"e-reader", "ereader", "tablet", "plugin", "plug-in", "add-on", "addon", "extension", "gadget", "widget", "directory", "repository",
                          "uninstall", "device", "software", "program", "executable"};

std::vector<std::string> problems_in(const std::string& source, const std::string& text) {
    std::vector<std::string> problems;
    std::string low = text;
    for (char& c : low) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (const char* w : kAvoided) {
        std::regex re(std::string("\\b") + w + "\\b");
        if (std::regex_search(low, re)) problems.push_back(source + ": uses \"" + w + "\"");
    }
    // the word "application" alone, not the folder name "applications/"
    if (std::regex_search(low, std::regex("\\bapplications?\\b(?!/)")) && !std::regex_search(low, std::regex("applications/"))) problems.push_back(source + ": uses \"application\"");
    // advice about rights
    for (const char* bad : {"sudo", "root", "chmod", "administrator", "as root"}) {
        std::regex re(std::string("\\b") + bad + "\\b");
        if (std::regex_search(low, re)) problems.push_back(source + ": mentions \"" + bad + "\"");
    }
    // sizes in other units and dates in other orders
    if (std::regex_search(text, std::regex("[0-9] ?(KB|MB|GB|kB|Kb|Mb|Gb)\\b"))) problems.push_back(source + ": a size in KB/MB/GB");
    if (std::regex_search(text, std::regex("\\b[0-3]?[0-9][./][0-1]?[0-9][./](19|20)[0-9][0-9]\\b"))) problems.push_back(source + ": a date that is not YYYY-MM-DD");
    return problems;
}

void expect_clean(const std::string& source, const std::string& text) {
    const auto problems = problems_in(source, text);
    if (!problems.empty()) PB_FAIL_AT(problems[0]);
}

}  // namespace

PB_TEST(the_rule_texts_use_the_canonical_terms) {
    for (const auto& r : install_rule_registry()) {
        expect_clean(std::string(r.id) + " description", r.description);
        expect_clean(std::string(r.id) + " next", r.next);
    }
}

PB_TEST(the_plans_and_their_confirmations_use_the_canonical_terms) {
    FakeReader f;
    f.add_data_dir("testapp", {{"a.db", "1234567"}});
    f.put_file("system/config/device.cfg", "x");
    CrashFs mem;
    f.load_into(mem);
    FatRulesFs fat(mem);
    SampleApp app = nested_sample_app();
    for (PlanKind kind : {PlanKind::Install}) {
        Built b = make_planned(fat, app, kind);
        b.plan.warnings.push_back("E-INS-17: example");
        b.plan.confirmations = {Confirmation::Plan, Confirmation::EditedFiles, Confirmation::UnknownFirmware, Confirmation::UnknownModel, Confirmation::ExistingDataFolder,
                                Confirmation::ReplaceOutside, Confirmation::Repair};
        expect_clean("plan text", render_plan(b.plan));
    }
    Built installed_plan = make_planned(fat, app);
    RunResult none;
    (void)installed_plan;
}

PB_TEST(the_command_line_help_and_outputs_use_the_canonical_terms) {
    CliRig r;
    if (!r.ready()) PB_SKIP("openssl CLI or the test program is not available");
    r.host.add_app(nested_sample_app());
    r.host.publish(100);
    ::mkdir((r.dir.path() + "/home").c_str(), 0755);
    r.write_reader();
    const std::vector<std::string> unknown = {"--confirm-unknown-model", "--confirm-unknown-firmware"};
    auto with = [&](std::vector<std::string> a) { a.insert(a.end(), unknown.begin(), unknown.end()); return a; };
    std::string all;
    for (const std::vector<std::string>& cmd : {std::vector<std::string>{"--help"}, std::vector<std::string>{"readers"}, std::vector<std::string>{"list", "--reader", r.root()},
                                                 with({"install", "testapp", "--reader", r.root(), "--dry-run"}), with({"install", "testapp", "--reader", r.root(), "--confirm-plan"}),
                                                 std::vector<std::string>{"installed", "--reader", r.root()}, std::vector<std::string>{"remove", "testapp", "--reader", r.root(), "--confirm-plan"},
                                                 with({"install", "nothing", "--reader", r.root(), "--confirm-plan"}), std::vector<std::string>{"repair", "--reader", r.root()}}) {
        RunResult res = r.run(cmd, {"PBSTORE_TEST_MOUNTS=" + r.root() + "|vfat|1000"});
        all += res.out + res.err;
    }
    // the reader's own paths and the test's names are not user-facing words
    std::string cleaned = all;
    for (size_t at; (at = cleaned.find(r.dir.path())) != std::string::npos;) cleaned.erase(at, r.dir.path().size());
    expect_clean("command line output", cleaned);
}

PB_TEST(the_assistants_pages_use_the_canonical_terms_when_they_exist) {
    const std::string dir = std::string(PBSTORE_SOURCE_DIR) + "/tools/pbstore-install-ui/assets";
    DIR* d = ::opendir(dir.c_str());
    if (!d) PB_SKIP("the assistant has no pages yet");
    while (dirent* e = ::readdir(d)) {
        const std::string n = e->d_name;
        if (n.size() > 5 && (n.substr(n.size() - 5) == ".html" || n.substr(n.size() - 3) == ".js")) {
            std::string text = slurp(dir + "/" + n);
            // the page's code is not prose; only look at the quoted texts and the page text
            std::string prose;
            const bool html = n.substr(n.size() - 5) == ".html";
            const std::regex quoted(html ? "()>([^<>]{12,})<" : "[\"'`]()([^\"'`\\n]{12,})[\"'`]");
            for (std::sregex_iterator it(text.begin(), text.end(), quoted), end; it != end; ++it) prose += (*it)[1].str() + (*it)[2].str() + "\n";
            expect_clean(n, prose);
        }
    }
    ::closedir(d);
}

PB_TEST_MAIN()
