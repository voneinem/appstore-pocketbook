// Text that comes from a catalog or a package is shown as plain text: control characters, terminal escape
// sequences and invisible direction overrides are neutralised in every output and in the log (spec FR-035).
#include "install_fixtures.h"
#include "mem_state_dirs.h"
#include "pbstore/install/log.h"
#include "pbstore/install/messages.h"
#include "pbstore/install/planner.h"
#include "pbstore/install/text_safety.h"
#include "test_support.h"

using namespace pbstore;
using namespace pbstore::install;
using namespace pbtest;

namespace {
bool clean(const std::string& s, bool lines_allowed) {
    for (unsigned char c : s) {
        if (c == 0x1b || c == 0x7f || c == '\r' || c == 0x07 || c == 0x08) return false;
        if (c < 0x20 && !(lines_allowed && (c == '\n' || c == '\t'))) return false;
    }
    return s.find("\xe2\x80\xae") == std::string::npos && s.find("\xc2\x9b") == std::string::npos;
}
}  // namespace

PB_TEST(control_characters_escape_sequences_and_direction_overrides_are_replaced) {
    PB_CHECK_EQ(neutralize("plain text"), std::string("plain text"));
    PB_CHECK_EQ(neutralize("caf\xc3\xa9 \xe2\x82\xac"), std::string("caf\xc3\xa9 \xe2\x82\xac"));        // UTF-8 passes
    PB_CHECK(clean(neutralize("\x1b[31mred\x1b[0m"), false));
    PB_CHECK(clean(neutralize("bell\x07 backspace\x08 cr\r del\x7f"), false));
    PB_CHECK(clean(neutralize("evil \xe2\x80\xae txt.exe"), false));          // right-to-left override
    PB_CHECK(clean(neutralize("c1 \xc2\x9b[2J"), false));                     // the one-byte escape of the C1 range
    PB_CHECK(clean(neutralize(std::string("nul\0inside", 10)), false));
    PB_CHECK(clean(neutralize("bad utf8 \xff\xfe"), false));
    PB_CHECK_EQ(neutralize("a\nb\tc", true), std::string("a\nb\tc"));
    PB_CHECK(clean(neutralize("a\nb\tc"), false));
    PB_CHECK(neutralize("x\x1b[31my").find("red") == std::string::npos);
}

PB_TEST(a_plan_with_hostile_names_prints_only_plain_text) {
    SampleApp app = nested_sample_app();
    app.manifest.set("name", pbstore::json::Value::from_string("Name \x1b[31mred\x1b[0m"));
    FakeReader reader;
    Volume v(reader);
    Built b = make_planned(v.fat, app);
    b.plan.app_name = "Name \x1b[31mred\x1b[0m \xe2\x80\xae";
    b.plan.menu.title = "Title\x07";
    PB_CHECK(clean(render_plan(b.plan), true));
}

PB_TEST(findings_with_hostile_text_are_neutralised_in_messages_and_the_log) {
    Finding f = make_error("E-PKG-03", "applications/\x1b[2Jevil", "bad name \xe2\x80\xae");
    Message m = describe(f, ReaderEffect::Unchanged);
    PB_CHECK(clean(m.text(), true));
    MemStateDirs dirs;
    FixedClock clock;
    Redactor red;
    Log log(dirs, clock, red);
    log.event("install", "app\x1b[31m", "1.0\x07", "failed", {"E-PKG-03"}, f.message + " " + f.subject);
    PB_CHECK(clean(dirs.text("installer.log"), true));
    PB_CHECK(dirs.text("installer.log").find('\n') == dirs.text("installer.log").size() - 1);     // still one line
}

PB_TEST_MAIN()
