#pragma once
// Test helper: the assistant's server and API on top of an engine rig, served on a thread of the test.

#include "api.h"
#include "http_server.h"
#include "install_rig.h"
#include "ui_client.h"

namespace pbtest {

struct UiRig {
    EngineRig rig;
    std::unique_ptr<pbstore::install::ui::Api> api;
    std::unique_ptr<pbstore::install::ui::HttpServer> server;
    std::thread thread;

    explicit UiRig(const FakeReader& r = FakeReader()) : rig(r) {}
    ~UiRig() { stop(); }

    bool ready() const { return rig.ready(); }

    // Writes the reader to disk, builds the engine, starts the server. `download` may be a custom downloader.
    void start(const pbstore::install::Downloader& download = nullptr) {
        rig.host.publish(rig.host.catalog.serial ? rig.host.catalog.serial : 100);
        rig.write_reader();
        pbstore::install::Engine& engine = rig.make_engine(download);
        api = std::make_unique<pbstore::install::ui::Api>(engine, rig.dir.path() + "/home", std::vector<std::string>{rig.dir.path()});
        pbstore::install::ui::HttpServer::Options options;
        options.busy = [this] { return api->busy(); };
        server = std::make_unique<pbstore::install::ui::HttpServer>([this](const pbstore::install::ui::HttpRequest& r) { return api->handle(r); }, options);
        std::string error;
        if (!server->start(&error)) throw std::runtime_error("server: " + error);
        thread = std::thread([this] { server->run(); });
    }
    void stop() {
        if (server) server->stop();
        if (thread.joinable()) thread.join();
    }
    UiClient client() const { return UiClient(server->port(), server->token()); }
    std::string root() const { return rig.root(); }
};

}  // namespace pbtest
