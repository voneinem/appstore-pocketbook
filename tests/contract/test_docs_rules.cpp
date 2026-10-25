// T076: the documents cannot drift from the code: rule ids, examples, commands, required sections
// (FR-038 … FR-040, Principle VIII).
#include "pbstore/catalog.h"
#include "pbstore/entry.h"
#include "pbstore/manifest.h"
#include "pbstore/rules.h"
#include "run_tool.h"
#include "test_support.h"

#include <fstream>
#include <set>
#include <sstream>

using namespace pbstore;

static std::string slurp(const std::string& rel) {
    std::ifstream f(std::string(PBSTORE_SOURCE_DIR) + "/" + rel, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}
static bool has(const std::string& text, const std::string& needle) { return text.find(needle) != std::string::npos; }

PB_TEST(every_rule_and_notice_id_is_documented) {
    const std::string review = slurp("docs/review-checks.md");
    PB_CHECK(!review.empty());
    std::vector<std::string> contracts;
    for (const char* f : {"catalog-format", "package-manifest", "entry-file", "cli"}) contracts.push_back(slurp(std::string("docs/contracts/") + f + ".md"));
    for (const RuleInfo& r : rule_registry()) {
        if (!has(review, r.id)) throw pbtest::Failure{std::string(r.id) + " is not in docs/review-checks.md"};
        bool in_contract = false;
        for (const std::string& c : contracts) in_contract = in_contract || has(c, r.id);
        if (!in_contract) throw pbtest::Failure{std::string(r.id) + " is not in any contract document"};
    }
}

PB_TEST(the_examples_parse_with_the_real_parsers) {
    CatalogParseResult c = parse_catalog(slurp("docs/contracts/examples/catalog.json"));
    PB_CHECK(c.catalog.has_value());
    ManifestParseResult m = parse_manifest(slurp("docs/contracts/examples/manifest.json"));
    PB_CHECK(m.manifest.has_value());
    EntryParseResult e = parse_entry(slurp("docs/contracts/examples/entry.json"), "sample-app.json", "2026-10-24");
    PB_CHECK(e.entry.has_value());
    PB_CHECK(e.findings.empty());
    // the three examples describe one app
    PB_CHECK_EQ(c.catalog->apps[0].id, m.manifest->id);
    PB_CHECK_EQ(e.entry->id, m.manifest->id);
    PB_CHECK_EQ(c.catalog->apps[0].version, m.manifest->version);
}

// "pbstore-catalog <command>" at the start of a line in a fenced code block
static std::set<std::string> commands_in_code_blocks(const std::string& text) {
    std::set<std::string> found;
    std::istringstream in(text);
    bool fenced = false;
    for (std::string line; std::getline(in, line);) {
        if (line.rfind("```", 0) == 0) { fenced = !fenced; continue; }
        if (!fenced) continue;
        size_t p = line.find("pbstore-catalog ");
        if (p == std::string::npos) continue;
        std::istringstream words(line.substr(p + 16));
        std::string cmd;
        words >> cmd;
        if (!cmd.empty() && cmd[0] != '-' && cmd[0] != '<') found.insert(cmd);
    }
    return found;
}

PB_TEST(every_command_named_in_the_guides_exists) {
    pbtest::RunResult help = pbtest::run_tool({"--help"});
    std::set<std::string> known;
    std::istringstream in(help.err);
    for (std::string line; std::getline(in, line);)
        if (line.size() > 2 && line[0] == ' ' && line[1] == ' ' && line[2] != ' ' && line[2] != '-') known.insert(line.substr(2));
    PB_CHECK(known.count("pack") && known.count("check-entry") && known.count("build"));
    for (const char* guide : {"docs/developer-guide.md", "docs/maintainer-keys.md"}) {
        const std::string text = slurp(guide);
        PB_CHECK(!text.empty());
        const std::set<std::string> used = commands_in_code_blocks(text);
        PB_CHECK(!used.empty());
        for (const std::string& cmd : used)
            if (!known.count(cmd)) throw pbtest::Failure{std::string(guide) + " uses the unknown command '" + cmd + "'"};
    }
}

PB_TEST(the_guides_cover_what_the_requirements_ask_for) {
    const std::string dev = slurp("docs/developer-guide.md");
    for (const char* needle : {"pbstore-catalog pack", "check-package", "check-entry", "withdrawn_versions", "manifest.json", "catalog/entries",
                               "pull request", "E-ENT-16", "same repository", "before the app was listed"})
        if (!has(dev, needle)) throw pbtest::Failure{std::string("developer guide lacks: ") + needle};
    const std::string keys = slurp("docs/maintainer-keys.md");
    for (const char* needle : {"keygen", "PBSTORE_SIGNING_KEY", "catalog-signing", "offline backup", "rotation", "next key", "Code Owners",
                               "up to date", "first_publish", "emergency", "GitHub Pages"})
        if (!has(keys, needle)) throw pbtest::Failure{std::string("maintainer guide lacks: ") + needle};
}

PB_TEST(the_review_checklist_covers_the_human_checks) {
    const std::string review = slurp("docs/review-checks.md");
    for (const char* needle : {"e-mail", "not on GitHub", "license", "PocketBook", "W-ENT-01", "W-ENT-02", "W-PR-01", "icon", "manifest"})
        if (!has(review, needle)) throw pbtest::Failure{std::string("review-checks.md lacks: ") + needle};
}

PB_TEST(each_contract_has_migration_notes_and_the_readme_says_it_is_unofficial) {
    for (const char* f : {"catalog-format", "package-manifest", "entry-file"})
        if (!has(slurp(std::string("docs/contracts/") + f + ".md"), "Migration notes")) throw pbtest::Failure{std::string(f) + " has no Migration notes section"};
    const std::string readme = slurp("README.md");
    PB_CHECK(has(readme, "unofficial and not made, endorsed or supported by PocketBook"));
    PB_CHECK(has(readme, "docs/developer-guide.md"));
    PB_CHECK(has(readme, "scripts/pre-merge.sh"));
    // feature 002: no Windows or SmartScreen text until a Windows build exists; the checksum and its limits are stated
    PB_CHECK(!has(readme, "SmartScreen"));
    PB_CHECK(has(readme, "sha256sum -c SHA256SUMS"));
    PB_CHECK(has(readme, "docs/installer-guide.md"));
    PB_CHECK(has(readme, "MIT"));
}

PB_TEST_MAIN()
