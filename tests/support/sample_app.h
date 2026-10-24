#pragma once
// Test helper: small sample apps that follow the package contract, and ways to break them.

#include <algorithm>
#include <string>
#include <vector>

#include "pbstore/json.h"
#include "pbstore/sha256.h"
#include "zip_builder.h"

namespace pbtest {

struct SampleApp {
    std::string id;
    std::vector<ZEntry> files;      // everything under applications/
    pbstore::json::Value manifest;  // as a document, so tests can mutate it
};

inline std::string pseudo_bytes(size_t n, unsigned seed) {
    std::string s(n, '\0');
    uint32_t x = seed * 2654435761u + 12345u;
    for (size_t i = 0; i < n; ++i) {
        x = x * 1664525u + 1013904223u;
        s[i] = static_cast<char>(x >> 24);
    }
    return s;
}

inline pbstore::json::Value files_value(const std::vector<ZEntry>& files) {
    using pbstore::json::Value;
    Value list = Value::make_array();
    std::vector<ZEntry> sorted = files;
    std::sort(sorted.begin(), sorted.end(), [](const ZEntry& a, const ZEntry& b) { return a.name < b.name; });
    for (const ZEntry& f : sorted) {
        if (f.name.back() == '/') continue;
        Value o = Value::make_object();
        o.set("path", Value::from_string(f.name));
        o.set("size", Value::from_int(static_cast<int64_t>(f.data.size())));
        o.set("sha256", Value::from_string(pbstore::sha256_hex(f.data)));
        list.push(o);
    }
    return list;
}

// `id` also names the files; `menu_id` and `data_dir` can be set to build apps that collide.
inline SampleApp make_sample_app(const std::string& id = "sample-app", const std::string& version = "1.2.0",
                                 const std::string& menu_id = "U_SampleApp", const std::string& data_dir = "system/sample-app",
                                 unsigned seed = 1) {
    using pbstore::json::Value;
    SampleApp app;
    app.id = id;
    app.files = {
        file("applications/" + id + ".app", "#!/bin/sh\nexec /mnt/ext1/applications/" + id + ".bin\n"),
        file("applications/" + id + ".bin", pseudo_bytes(4096, seed)),
        file("applications/icons/" + id + ".bmp", pseudo_bytes(512, seed + 10)),
        file("applications/icons/" + id + "_f.bmp", pseudo_bytes(512, seed + 20)),
    };
    Value m = Value::make_object();
    m.set("schema_version", Value::from_string("1.0"));
    m.set("id", Value::from_string(id));
    m.set("name", Value::from_string("App " + id));
    m.set("version", Value::from_string(version));
    m.set("min_firmware", Value::from_string("6.8.0"));
    m.set("target_device", Value::from_string("InkPad One"));
    m.set("files", files_value(app.files));
    Value menu = Value::make_object();
    menu.set("id", Value::from_string(menu_id));
    menu.set("title", Value::from_string("App " + id));
    menu.set("path", Value::from_string("applications/" + id + ".app"));
    menu.set("icon", Value::from_string("applications/icons/" + id + ".bmp"));
    menu.set("focused_icon", Value::from_string("applications/icons/" + id + "_f.bmp"));
    menu.set("group", Value::from_string("@General"));
    m.set("menu_entry", menu);
    m.set("data_dir", data_dir.empty() ? Value() : Value::from_string(data_dir));
    app.manifest = m;
    return app;
}

inline void set_member(pbstore::json::Value& obj, const std::string& key, pbstore::json::Value v) { obj.set(key, std::move(v)); }
inline void remove_member(pbstore::json::Value& obj, const std::string& key) {
    for (size_t i = 0; i < obj.object.size(); ++i)
        if (obj.object[i].first == key) { obj.object.erase(obj.object.begin() + static_cast<long>(i)); return; }
}
inline pbstore::json::Value& member(pbstore::json::Value& obj, const std::string& key) {
    for (auto& kv : obj.object) if (kv.first == key) return kv.second;
    obj.object.emplace_back(key, pbstore::json::Value());
    return obj.object.back().second;
}

// The package as bytes. Pass a mutated manifest and/or files to build invalid packages.
inline std::string build_package(const pbstore::json::Value& manifest, const std::vector<ZEntry>& files,
                                 const std::vector<ZEntry>& extra = {}) {
    std::vector<ZEntry> all;
    all.push_back(file("manifest.json", pbstore::json::write(manifest)));
    for (const ZEntry& f : files) all.push_back(f);
    for (const ZEntry& f : extra) all.push_back(f);
    return build_zip(all);
}

inline std::string build_package(const SampleApp& app) { return build_package(app.manifest, app.files); }

}  // namespace pbtest
