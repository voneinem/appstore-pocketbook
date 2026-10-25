#pragma once
// Test helper: a signed catalog and packages served by a local HTTPS server (with its own throw-away CA), an
// engine wired to it, and the same for the command-line program. Nothing here talks to the internet.

#include <algorithm>
#include <chrono>
#include <fstream>
#include <sstream>
#include <thread>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "fake_mount_source.h"
#include "fake_reader.h"
#include "fixed_clock.h"
#include "install_fixtures.h"
#include "keys.h"
#include "mem_state_dirs.h"
#include "pbstore/base64.h"
#include "pbstore/catalog.h"
#include "pbstore/install/engine.h"
#include "pbstore/install/net.h"
#include "pbstore/install/posix_local_file.h"
#include "pbstore/install/posix_reader_fs.h"
#include "run_tool.h"
#include "sample_catalog.h"
#include "tls_test_server.h"
#include "tmp_dir.h"

namespace pbtest {

inline KeyPair key_from_seed_file(const std::string& path) {
    std::ifstream f(path);
    std::ostringstream ss;
    ss << f.rdbuf();
    KeyPair k;
    k.seed = *pbstore::base64_decode(ss.str());
    k.pub = pbstore::ed25519_public_from_seed(k.seed);
    return k;
}

inline std::string package_path_of(const pbstore::CatalogEntry& e) {
    return e.package_url.substr(std::string("https://github.com").size());
}

// The catalog host: serves /catalog.json, /catalog.json.sig and the packages of the apps added to it.
class CatalogHost {
public:
    explicit CatalogHost(const KeyPair& key, const std::string& san = "DNS:localhost,IP:127.0.0.1") : key_(key), pki_(dir_) {
        if (!pki_.available()) return;
        leaf_ = pki_.issue("srv", san);
        if (!leaf_.ok) return;
        server_ = std::make_unique<TlsTestServer>(leaf_.cert, leaf_.key, [this](const std::string& req) { return answer(req); },
                                                  [this](const std::string& req, const std::function<bool(const std::string&)>& write) { stream(req, write); });
        catalog.schema_version = "1.0";
        catalog.serial = 100;
        catalog.generated_at = "2026-10-24T12:00:00Z";
    }
    bool ready() const { return server_ && server_->running(); }
    std::string host_url() const { return "https://localhost:" + std::to_string(server_->port()); }
    std::string host_port() const { return "localhost:" + std::to_string(server_->port()); }
    std::string catalog_url() const { return host_url() + "/catalog.json"; }
    std::string ca_file() const { return pki_.ca_cert(); }
    std::vector<std::string> request_paths() const {
        std::vector<std::string> out;
        for (const std::string& r : server_->requests()) {
            const size_t a = r.find(' '), b = r.find(' ', a + 1);
            out.push_back(r.substr(a + 1, b - a - 1));
        }
        return out;
    }
    std::vector<std::string> raw_requests() const { return server_->requests(); }

    pbstore::Catalog catalog;
    KeyPair key_;
    std::map<std::string, std::string> files;     // path -> body
    std::string stall_path;                        // a path whose answer stalls (sends nothing for a while)
    int stall_ms = 0;
    std::string redirect_path, redirect_to;        // a path that answers with a redirect

    pbstore::CatalogEntry& add_app(const SampleApp& app, bool offered = true) {
        pbstore::CatalogEntry e = sample_entry(app.id, app.manifest.get_string("version").value());
        e.name = app.manifest.get_string("name").value();
        e.menu_entry_id = app.manifest.find("menu_entry")->get_string("id").value();
        const pbstore::json::Value* dd = app.manifest.find("data_dir");
        if (dd && dd->is_string()) e.data_dir = dd->string; else e.data_dir.reset();
        e.min_firmware = app.manifest.get_string("min_firmware").value();
        const std::string zip = build_package(app);
        e.package_size = static_cast<int64_t>(zip.size());
        e.package_sha256 = pbstore::sha256_hex(zip);
        if (!offered) e.withdrawn_versions = {e.version};
        files[package_path_of(e)] = zip;
        catalog.apps.erase(std::remove_if(catalog.apps.begin(), catalog.apps.end(), [&](const pbstore::CatalogEntry& x) { return x.id == e.id; }), catalog.apps.end());
        catalog.apps.push_back(e);
        return catalog.apps.back();
    }
    // Publishes the catalog (and signs it) with the given serial.
    void publish(int64_t serial) {
        catalog.serial = serial;
        const std::string text = pbstore::write_catalog(catalog);
        files["/catalog.json"] = text;
        files["/catalog.json.sig"] = sign_text(key_, text);
    }
    void set_raw(const std::string& path, const std::string& body) { files[path] = body; }

    // The downloader of the installer, pointed at this host instead of github.com.
    pbstore::install::Downloader downloader(const std::string& user_agent = "pbstore-install/0.2.0", long stall_seconds = 30,
                                            const std::string& ca_file_override = "") const {
        pbstore::install::NetOptions o;
        o.ca_file = ca_file_override.empty() ? ca_file() : ca_file_override;
        o.user_agent = user_agent;
        o.stall_seconds = stall_seconds;
        pbstore::install::Downloader inner = pbstore::install::make_https_downloader(o);
        const std::string host = host_url();
        return [inner, host](const std::string& url, uint64_t max, long secs, const pbstore::install::DownloadProgress& p) {
            const std::string github = "https://github.com";
            if (url.compare(0, github.size(), github) == 0) return inner(host + url.substr(github.size()), max, secs, p);
            return inner(url, max, secs, p);
        };
    }

private:
    std::string path_of(const std::string& req) const {
        const size_t a = req.find(' '), b = req.find(' ', a + 1);
        return req.substr(a + 1, b - a - 1);
    }
    std::string answer(const std::string& req) {
        const std::string path = path_of(req);
        if (!redirect_path.empty() && path == redirect_path) return TlsTestServer::http_response(302, "", "Location: " + redirect_to + "\r\n");
        auto it = files.find(path);
        if (it == files.end()) return TlsTestServer::http_response(404, "not found");
        return TlsTestServer::http_response(200, it->second);
    }
    void stream(const std::string& req, const std::function<bool(const std::string&)>& write) {
        const std::string path = path_of(req);
        if (!stall_path.empty() && path == stall_path) {
            write("HTTP/1.1 200 OK\r\nContent-Length: 100000\r\nConnection: close\r\n\r\n");
            write("x");
            std::this_thread::sleep_for(std::chrono::milliseconds(stall_ms));
            return;
        }
        write(answer(req));
    }

    TmpDir dir_;
    TestPki pki_;
    TestPki::Leaf leaf_;
    std::unique_ptr<TlsTestServer> server_;
};

// An engine on a real directory (the fake reader written to disk), a catalog host, an in-memory state folder.
struct EngineRig {
    TmpDir dir;
    KeyPair key = make_keypair();
    CatalogHost host;
    FakeReader reader;
    MemStateDirs state;
    FixedClock clock;
    pbstore::install::Redactor redactor;
    pbstore::install::Log log{state, clock, redactor};
    FakeMountSource mounts;
    std::unique_ptr<pbstore::install::Engine> engine;

    explicit EngineRig(const FakeReader& r = FakeReader()) : host(key), reader(r) {}

    std::string root() const { return dir.path() + "/reader"; }
    bool ready() const { return host.ready(); }
    void write_reader() {
        reader.write_to(root());
        mounts.mount(root(), "vfat", 8ull << 30);
    }
    pbstore::install::Engine& make_engine(const pbstore::install::Downloader& download = nullptr, std::vector<pbstore::install::IdentitySource*> sources = {}) {
        pbstore::install::Config config;
        config.catalog_url = host.catalog_url();
        config.trusted_keys = {key.pub};
        config.program_version = "0.2.0";
        pbstore::install::Deps deps;
        deps.open_fs = [](const std::string& root) -> std::unique_ptr<pbstore::install::ReaderFs> { return std::make_unique<pbstore::install::PosixReaderFs>(root); };
        deps.mounts = &mounts;
        deps.state = &state;
        deps.download = download ? download : host.downloader();
        deps.clock = &clock;
        deps.log = &log;
        deps.redactor = &redactor;
        deps.identity_sources = sources;
        deps.read_local_file = pbstore::install::read_regular_file;
        engine = std::make_unique<pbstore::install::Engine>(config, deps);
        return *engine;
    }
};

// The command-line program of the test build, wired to a catalog host through the environment.
struct CliRig {
    TmpDir dir;
    CatalogHost host;
    FakeReader reader;

    explicit CliRig(const FakeReader& r = FakeReader()) : host(key_from_seed_file(PBSTORE_TEST_SEED_FILE)), reader(r) {}
    bool ready() const { return host.ready() && PBSTORE_INSTALL_TEST_TOOL[0] != '\0'; }
    std::string root() const { return dir.path() + "/reader"; }
    std::string state_dir() const { return dir.path() + "/state"; }
    void write_reader() { reader.write_to(root()); }

    std::vector<std::string> env(const std::vector<std::string>& extra = {}) const {
        std::vector<std::string> e = {"PBSTORE_TEST_CATALOG_URL=" + host.catalog_url(), "PBSTORE_TEST_CA_FILE=" + host.ca_file(),
                                      "PBSTORE_TEST_STATE_DIR=" + state_dir(), "PBSTORE_TEST_HOST_MAP=github.com=" + host.host_port(),
                                      "HOME=" + dir.path() + "/home", "USER=cliuser"};
        e.insert(e.end(), extra.begin(), extra.end());
        return e;
    }
    RunResult run(const std::vector<std::string>& args, const std::vector<std::string>& extra_env = {}, const std::string& stdin_text = "") const {
        return run_program(PBSTORE_INSTALL_TEST_TOOL, args, env(extra_env), {"DISPLAY", "WAYLAND_DISPLAY"}, "", stdin_text);
    }
};


}  // namespace pbtest
