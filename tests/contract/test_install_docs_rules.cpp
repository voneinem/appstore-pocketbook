// T028: every rule id of the installer's registry is documented in docs/installer-rules.md and in the design
// contract; no documented id is missing from the registry (as test_docs_rules does for the shared rules).
#include "pbstore/install/install_rules.h"
#include "test_support.h"

#include <fstream>
#include <regex>
#include <set>
#include <sstream>

using namespace pbstore::install;

namespace {
std::string slurp(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}
std::set<std::string> ids_in(const std::string& text) {
    std::set<std::string> ids;
    std::regex re("`([EW]-INS-[0-9]{2})`");
    for (std::sregex_iterator it(text.begin(), text.end(), re), end; it != end; ++it) ids.insert((*it)[1]);
    return ids;
}
}  // namespace

PB_TEST(every_registry_id_is_in_the_user_documentation_and_the_contract) {
    const std::string docs = slurp(std::string(PBSTORE_SOURCE_DIR) + "/docs/installer-rules.md");
    const std::string contract = slurp(std::string(PBSTORE_SOURCE_DIR) + "/specs/002-pb-installer-linux/contracts/rules.md");
    PB_CHECK(!docs.empty());
    PB_CHECK(!contract.empty());
    const std::set<std::string> in_docs = ids_in(docs), in_contract = ids_in(contract);
    for (const auto& r : install_rule_registry()) {
        PB_CHECK(in_docs.count(r.id) == 1);
        PB_CHECK(in_contract.count(r.id) == 1);
    }
}

PB_TEST(no_documented_id_is_missing_from_the_registry) {
    const std::string docs = slurp(std::string(PBSTORE_SOURCE_DIR) + "/docs/installer-rules.md");
    for (const std::string& id : ids_in(docs)) PB_CHECK(find_install_rule(id) != nullptr);
}

PB_TEST(the_documented_exit_status_of_each_rule_is_the_registry_one) {
    const std::string docs = slurp(std::string(PBSTORE_SOURCE_DIR) + "/docs/installer-rules.md");
    // table rows: | `E-INS-03` | ... | <status> |
    std::regex row("\\| `([EW]-INS-[0-9]{2})` \\|[^\\n]*\\| ([0-4]) \\|");
    int seen = 0;
    for (std::sregex_iterator it(docs.begin(), docs.end(), row), end; it != end; ++it) {
        const InstallRule* r = find_install_rule((*it)[1]);
        PB_CHECK(r != nullptr);
        PB_CHECK_EQ(r->exit_status, std::stoi((*it)[2]));
        ++seen;
    }
    PB_CHECK_EQ(seen, 41);
}

PB_TEST_MAIN()
