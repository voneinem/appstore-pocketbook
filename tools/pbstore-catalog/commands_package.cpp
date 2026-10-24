// pack and check-package (docs/contracts/cli.md).
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <filesystem>
#include <iostream>

#include "common.h"
#include "pbstore/json.h"
#include "pbstore/package_check.h"
#include "pbstore/sha256.h"
#include "pbstore/zip_package.h"

namespace {

using pbcli::Args;
namespace fs = std::filesystem;

void print_package_summary(const pbstore::PackageCheckResult& r, const std::string& label) {
    std::cout << label << "\n"
              << "size: " << r.package_size << " bytes\n"
              << "sha256: " << r.package_sha256 << "\n";
    if (r.manifest) std::cout << "app: " << r.manifest->id << " " << r.manifest->version << "\n";
}

// Writes `data` to `path` through a temporary file in the same directory (no half-written package).
bool write_atomic(const std::string& path, const std::string& data, std::string* error) {
    const std::string tmp = path + ".tmp";
    if (!pbcli::write_file(tmp, data, error)) return false;
    if (::rename(tmp.c_str(), path.c_str()) != 0) {
        ::unlink(tmp.c_str());
        *error = "cannot write " + path;
        return false;
    }
    return true;
}

int cmd_check_package(const Args& a) {
    if (a.positional.size() != 1) return pbcli::usage_error("check-package needs one package file: check-package FILE.zip");
    std::error_code ec;
    const auto size = fs::file_size(a.positional[0], ec);
    if (ec) return pbcli::usage_error("cannot read " + a.positional[0]);
    if (size > 1024ull * 1024 * 1024) return pbcli::usage_error(a.positional[0] + " is too large to check");
    std::string bytes, error;
    if (!pbcli::read_file(a.positional[0], &bytes, &error)) return pbcli::usage_error(error);
    pbstore::PackageCheckResult r = pbstore::check_package(bytes);
    pbcli::report(r.findings, a, a.positional[0]);
    const int code = pbcli::exit_code(r.findings);
    if (code == 0) print_package_summary(r, "package ok: " + a.positional[0]);
    return code;
}

int cmd_pack(const Args& a) {
    if (a.positional.size() != 1 || !a.has("out")) return pbcli::usage_error("pack needs a package directory and --out FILE.zip: pack PACKAGE_DIR --out FILE.zip");
    const std::string dir = a.positional[0], out_path = a.value("out");
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return pbcli::usage_error("package directory not found: " + dir);
    std::string template_text, error;
    if (!pbcli::read_file(dir + "/manifest.json", &template_text, &error)) return pbcli::usage_error("no manifest.json in " + dir);

    std::vector<pbstore::Finding> findings;
    std::string json_error;
    auto doc = pbstore::json::parse(template_text, &json_error, pbstore::kMaxManifestBytes);
    if (!doc || !doc->is_object()) {
        findings.push_back(pbstore::make_error("E-PKG-05", "manifest.json", doc ? "the manifest must be a JSON object" : "not valid JSON: " + json_error));
        pbcli::report(findings, a, dir + "/manifest.json");
        return 1;
    }

    // the applications/ tree
    std::vector<pbstore::ZipFile> files;
    const fs::path apps = fs::path(dir) / "applications";
    if (fs::is_directory(apps, ec)) {
        for (fs::recursive_directory_iterator it(apps, fs::directory_options::none, ec), end; !ec && it != end; it.increment(ec)) {
            const fs::path rel = fs::path("applications") / fs::relative(it->path(), apps, ec);
            const std::string name = rel.generic_string();
            if (it->is_symlink(ec)) {
                findings.push_back(pbstore::make_error("E-PKG-03", name, "symbolic links are not allowed in a package"));
            } else if (it->is_regular_file(ec)) {
                std::string data;
                if (!pbcli::read_file(it->path().string(), &data, &error)) return pbcli::usage_error(error);
                files.push_back({name, std::move(data)});
            } else if (!it->is_directory(ec)) {
                findings.push_back(pbstore::make_error("E-PKG-03", name, "not a regular file"));
            }
        }
    }
    if (!findings.empty()) {
        pbcli::report(findings, a, dir);
        return 1;
    }

    // the files list: path, size, sha256, sorted by path
    std::sort(files.begin(), files.end(), [](const pbstore::ZipFile& x, const pbstore::ZipFile& y) { return x.name < y.name; });
    pbstore::json::Value list = pbstore::json::Value::make_array();
    for (const auto& f : files) {
        pbstore::json::Value o = pbstore::json::Value::make_object();
        o.set("path", pbstore::json::Value::from_string(f.name));
        o.set("size", pbstore::json::Value::from_int(static_cast<int64_t>(f.data.size())));
        o.set("sha256", pbstore::json::Value::from_string(pbstore::sha256_hex(f.data)));
        list.push(o);
    }
    // keep the template's members and their order; replace "files" in place (or add it after target_device)
    bool replaced = false;
    for (auto& kv : doc->object)
        if (kv.first == "files") { kv.second = list; replaced = true; }
    if (!replaced) {
        auto pos = std::find_if(doc->object.begin(), doc->object.end(), [](const auto& kv) { return kv.first == "target_device"; });
        doc->object.insert(pos == doc->object.end() ? doc->object.end() : pos + 1, {"files", list});
    }
    std::vector<pbstore::ZipFile> all = files;
    all.push_back({"manifest.json", pbstore::json::write(*doc)});
    const std::string zip = pbstore::write_zip(all);

    pbstore::PackageCheckResult r = pbstore::check_package(zip);
    pbcli::report(r.findings, a, dir);
    if (pbcli::exit_code(r.findings) != 0) return 1;       // nothing is written for an invalid package
    if (!write_atomic(out_path, zip, &error)) return pbcli::usage_error(error);
    print_package_summary(r, "package written: " + out_path);
    return 0;
}

pbcli::CommandRegistrar reg_pack("pack", "build a conforming package: pack PACKAGE_DIR --out FILE.zip", cmd_pack);
pbcli::CommandRegistrar reg_check_package("check-package", "check a package offline: check-package FILE.zip [--github]", cmd_check_package);

}  // namespace
