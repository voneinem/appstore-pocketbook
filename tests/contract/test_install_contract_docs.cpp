// T138: the contracts that other programs (the store app) depend on are promoted to docs/contracts/; a copy must
// never differ from its source in the spec folder. The engine and assistant contracts stay in the spec folder as
// design records.
#include "test_support.h"

#include <fstream>
#include <sstream>

namespace {
std::string slurp(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}
std::string src(const std::string& name) { return std::string(PBSTORE_SOURCE_DIR) + "/specs/002-pb-installer-linux/contracts/" + name; }
std::string doc(const std::string& name) { return std::string(PBSTORE_SOURCE_DIR) + "/docs/contracts/" + name; }
}  // namespace

PB_TEST(a_promoted_contract_equals_its_source) {
    struct Pair { const char* source; const char* copy; };
    for (const Pair& p : {Pair{"install-record.md", "install-record.md"}, Pair{"journal.md", "journal.md"}, Pair{"reader-layout.md", "reader-layout.md"}, Pair{"cli.md", "installer-cli.md"}}) {
        const std::string a = slurp(src(p.source));
        const std::string b = slurp(doc(p.copy));
        PB_CHECK(!a.empty());
        if (b.empty()) PB_FAIL_AT(std::string("docs/contracts/") + p.copy + " does not exist");
        if (a != b) PB_FAIL_AT(std::string("docs/contracts/") + p.copy + " differs from specs/002-pb-installer-linux/contracts/" + p.source);
    }
}

PB_TEST(the_contracts_of_the_store_are_promoted_with_their_final_text) {
    // T152: the store's contracts live in docs/contracts/ next to the installer's; a copy never differs from its source in the feature folder
    const std::string base = std::string(PBSTORE_SOURCE_DIR) + "/specs/003-pb-store-reader-app/contracts/";
    for (const char* name : {"store-state-folder.md", "launcher.md", "device-interfaces.md", "engine-additions.md", "ui-pages.md"}) {
        const std::string a = slurp(base + name);
        const std::string b = slurp(doc(name));
        PB_CHECK(!a.empty());
        if (b.empty()) PB_FAIL_AT(std::string("docs/contracts/") + name + " does not exist");
        if (a != b) PB_FAIL_AT(std::string("docs/contracts/") + name + " differs from specs/003-pb-store-reader-app/contracts/" + name);
    }
    // the rules of the store are in docs/store-app-rules.md, written from the registry (checked by test_store_rules_docs)
    PB_CHECK(!slurp(std::string(PBSTORE_SOURCE_DIR) + "/docs/store-app-rules.md").empty());
}

PB_TEST(the_table_of_paths_names_the_store_s_state_folder) {
    const std::string layout = slurp(doc("reader-layout.md"));
    PB_CHECK(layout.find("`system/pbstore/app/**`") != std::string::npos);
    PB_CHECK(slurp(src("reader-layout.md")).find("`system/pbstore/app/**`") != std::string::npos);   // the promoted copy equals its source
}

PB_TEST(the_design_records_that_stay_in_the_spec_folder_exist) {
    for (const char* name : {"engine-api.md", "ui-api.md", "rules.md"}) PB_CHECK(!slurp(src(name)).empty());
}

PB_TEST_MAIN()
