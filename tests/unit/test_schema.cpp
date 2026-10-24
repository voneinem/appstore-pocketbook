// schema_version "MAJOR.MINOR" shared by catalog, manifest and entry file (Principle VII).
#include "pbstore/schema.h"
#include "test_support.h"

using namespace pbstore;

PB_TEST(parses_major_minor) {
    auto v = parse_schema_version("1.0");
    PB_CHECK(v.has_value());
    PB_CHECK_EQ(v->major, 1);
    PB_CHECK_EQ(v->minor, 0);
    auto w = parse_schema_version("12.34");
    PB_CHECK(w.has_value() && w->major == 12 && w->minor == 34);
}

PB_TEST(rejects_everything_else) {
    for (const char* bad : {"", "1", "1.0.0", "v1.0", "1.", ".0", "1.x", "-1.0", "01.0", "1.00", " 1.0", "1.0 ",
                            "1,0", "1234567890.0"}) {
        PB_CHECK(!parse_schema_version(bad).has_value());
    }
}

PB_TEST(classifies_against_the_supported_major) {
    PB_CHECK(schema_support(*parse_schema_version("1.0")) == SchemaSupport::Supported);
    PB_CHECK(schema_support(*parse_schema_version("1.7")) == SchemaSupport::Supported);   // minor: additive
    PB_CHECK(schema_support(*parse_schema_version("2.0")) == SchemaSupport::NeedsUpdate);
    PB_CHECK(schema_support(*parse_schema_version("0.9")) == SchemaSupport::Unsupported);
    PB_CHECK_EQ(std::string(kSupportedSchema), std::string("1.0"));
}

PB_TEST_MAIN()
