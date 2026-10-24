#pragma once
// Test helper: entry files that match the sample apps of sample_app.h.

#include <string>

#include "pbstore/json.h"

namespace pbtest {

inline pbstore::json::Value sample_entry_doc(const std::string& id = "sample-app", const std::string& version = "1.2.0",
                                             const std::string& repo = "") {
    using pbstore::json::Value;
    const std::string r = repo.empty() ? "example-org/" + id : repo;
    Value d = Value::make_object();
    d.set("schema_version", Value::from_string("1.0"));
    d.set("id", Value::from_string(id));
    d.set("package_url", Value::from_string("https://github.com/" + r + "/releases/download/v" + version + "/" + id + "-inkpad-one-" + version + ".zip"));
    d.set("release_date", Value::from_string("2026-10-20"));
    Value sd = Value::make_object();
    sd.set("en", Value::from_string("A small sample app."));
    sd.set("de", Value::from_string("Eine kleine Beispiel-App."));
    d.set("short_description", sd);
    Value ld = Value::make_object();
    ld.set("en", Value::from_string("A sample app used in the contract tests."));
    d.set("long_description", ld);
    d.set("license", Value::from_string("MIT"));
    d.set("source_url", Value::from_string("https://github.com/" + r));
    d.set("author", Value::from_string("Example Author"));
    d.set("icon_url", Value::from_string("https://raw.githubusercontent.com/" + r + "/v" + version + "/icon.png"));
    d.set("changelog_url", Value::from_string("https://github.com/" + r + "/releases/tag/v" + version));
    return d;
}

}  // namespace pbtest
