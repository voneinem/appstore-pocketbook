// T027: the model and firmware strings InkView gives (GetDeviceModel, GetSoftwareVersion) become the engine's
// identity; text that does not parse gives nothing, so that the installer's "unknown" confirmations apply.
// Until the device task DS-04 shows the real strings, only the two plain forms and the empty string are required.
#include "crash_fs.h"
#include "pbstore/install/identity.h"
#include "pbstore/store/identity_parse.h"
#include "test_support.h"

using namespace pbstore::install;
using namespace pbstore::store;

PB_TEST(the_plain_forms_are_returned) {
    PB_CHECK(parse_model("1030") == std::optional<std::string>("1030"));
    PB_CHECK(parse_firmware("6.11.1437") == std::optional<std::string>("6.11.1437"));
}

PB_TEST(empty_or_unparsable_text_gives_nothing) {
    PB_CHECK(!parse_model("").has_value());
    PB_CHECK(!parse_firmware("").has_value());
    PB_CHECK(!parse_firmware("not a version").has_value());
    PB_CHECK(!parse_firmware("6.11").has_value());                 // MAJOR.MINOR.BUILD is required
    PB_CHECK(!parse_firmware("6.11.x").has_value());
    PB_CHECK(!parse_model("   ").has_value());
}

PB_TEST(a_leading_v_and_surrounding_white_space_are_tolerated_and_other_words_are_not) {
    // recorded here, to be revised when DS-04 shows the real strings
    PB_CHECK(parse_firmware(" 6.11.1437\n") == std::optional<std::string>("6.11.1437"));
    PB_CHECK(parse_firmware("v6.11.1437") == std::optional<std::string>("6.11.1437"));
    PB_CHECK(!parse_firmware("firmware 6.11.1437").has_value());
    PB_CHECK(parse_model(" 1030\n") == std::optional<std::string>("1030"));
}

PB_TEST(the_identity_source_returns_nothing_for_unparsable_text_so_the_unknown_confirmations_apply) {
    pbtest::CrashFs fs;                                           // the source does not read the volume; it asks InkView
    StoreIdentitySource good([] { return std::string("1030"); }, [] { return std::string("6.11.1437"); });
    PB_CHECK(good.model(fs) == std::optional<std::string>("1030"));
    PB_CHECK(good.firmware(fs) == std::optional<std::string>("6.11.1437"));

    StoreIdentitySource bad([] { return std::string(""); }, [] { return std::string("garbage"); });
    PB_CHECK(!bad.model(fs).has_value());
    PB_CHECK(!bad.firmware(fs).has_value());

    StoreIdentitySource partial([] { return std::string("1030"); }, [] { return std::string(""); });
    PB_CHECK(partial.model(fs).has_value());
    PB_CHECK(!partial.firmware(fs).has_value());                  // each value is decided on its own
}

PB_TEST_MAIN()
