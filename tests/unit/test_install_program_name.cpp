// T014 (engine addition E2): the record says which program wrote it. The default is the installer's name, so
// the installer's behavior and every old test are unchanged; the store passes its own name.
#include "install_rig.h"
#include "test_support.h"

using namespace pbstore;
using namespace pbstore::install;
using namespace pbtest;

namespace {

std::string record_of(const std::string& root, const std::string& id) {
    std::ifstream f(root + "/system/pbstore/installed/" + id + ".json");
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

std::string install_testapp(const std::string& program_name) {
    EngineRig rig;
    if (!rig.ready()) return "SKIP";
    rig.host.add_app(nested_sample_app("testapp", "1.2.0"));
    rig.host.publish(100);
    rig.write_reader();
    Engine& engine = rig.make_engine(nullptr, {}, nullptr, program_name);
        auto handle = engine.open_reader(rig.root());
    CancelToken cancel;
    Action action;
    action.type = Action::Type::Install;
    action.app_id = "testapp";
    auto plan = engine.plan(**handle.value, action, nullptr, cancel);
    if (!plan.ok()) return "PLAN FAILED";
    Confirmations given = plan.value->plan.confirmations;          // every consent the plan names
    ExecResult result = engine.execute(**handle.value, *plan.value, given, nullptr, cancel);
    if (!result.ok()) return "EXECUTE FAILED";
    return record_of(rig.root(), "testapp");
}

bool has(const std::string& t, const std::string& n) { return t.find(n) != std::string::npos; }

}  // namespace

PB_TEST(a_program_name_is_written_into_installed_by) {
    const std::string record = install_testapp("pbstore-app");
    if (record == "SKIP") PB_SKIP("openssl CLI not available");
    PB_CHECK(has(record, "\"installed_by\": \"pbstore-app 0.2.0\"") || has(record, "\"installed_by\":\"pbstore-app 0.2.0\""));
}

PB_TEST(the_default_is_the_installers_name_exactly_as_before) {
    const std::string record = install_testapp("");
    if (record == "SKIP") PB_SKIP("openssl CLI not available");
    PB_CHECK(has(record, "pbstore-install 0.2.0"));
    PB_CHECK(!has(record, "pbstore-app"));
}

PB_TEST_MAIN()
