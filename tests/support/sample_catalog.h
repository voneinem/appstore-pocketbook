#pragma once
// Builders for catalogs used by the contract tests.

#include "pbstore/catalog.h"

namespace pbtest {

inline pbstore::CatalogEntry sample_entry(const std::string& id, const std::string& version = "1.2.0") {
    pbstore::CatalogEntry e;
    e.id = id;
    e.name = "App " + id;
    e.short_description.en = "Short description of " + id + ".";
    e.long_description.en = "Long description of " + id + ".";
    e.version = version;
    e.release_date = "2026-10-20";
    e.package_url = "https://github.com/example-org/" + id + "/releases/download/v" + version + "/" + id + ".zip";
    e.package_size = 222000;
    e.package_sha256 = std::string(63, '0') + "5";
    e.license = "MIT";
    e.source_url = "https://github.com/example-org/" + id;
    e.author = "Example Author";
    e.min_firmware = "6.8.0";
    e.target_device = "InkPad One";
    e.icon_url = "https://raw.githubusercontent.com/example-org/" + id + "/v" + version + "/icon.png";
    e.changelog_url = "https://github.com/example-org/" + id + "/releases/tag/v" + version;
    e.listed_versions = {version};
    e.menu_entry_id = "U_" + id;
    for (char& c : e.menu_entry_id)
        if (c == '-') c = '_';
    e.data_dir = "system/" + id;
    return e;
}

inline pbstore::Catalog sample_catalog(int64_t serial = 100) {
    pbstore::Catalog c;
    c.schema_version = "1.0";
    c.serial = serial;
    c.generated_at = "2026-10-24T12:00:00Z";
    c.apps.push_back(sample_entry("alpha-app"));
    c.apps.push_back(sample_entry("beta-app", "2.0.1"));
    return c;
}

}  // namespace pbtest
