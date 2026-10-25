#pragma once
// The JSON API of the assistant (contracts/ui-api.md). Every action is a call into the engine; this layer has no
// install logic of its own. One engine operation runs at a time (a second request answers 409); the engine, not
// the page, decides whether a confirmation is missing.

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "http_server.h"
#include "pbstore/install/engine.h"

namespace pbstore {
namespace install {
namespace ui {

class Api {
public:
    // `home` and `roots` bound the folders the page may look into when it lets the user pick a folder by hand.
    Api(Engine& engine, std::string home, std::vector<std::string> roots);

    HttpResponse handle(const HttpRequest& request);
    bool busy() const { return busy_.load(); }

private:
    struct Op;   // the running operation (progress, cancel)

    HttpResponse state();
    HttpResponse readers();
    HttpResponse open_reader(const HttpRequest& req);
    HttpResponse folders(const HttpRequest& req);
    HttpResponse installed();
    HttpResponse list();
    HttpResponse plan(const HttpRequest& req, bool preview);
    HttpResponse execute(const HttpRequest& req);
    HttpResponse cancel();
    HttpResponse repair_plan();
    HttpResponse repair(const HttpRequest& req);
    HttpResponse asset(const std::string& name);

    bool begin_operation();
    void end_operation();
    ProgressFn progress_callback();

    Engine& engine_;
    std::string home_;
    std::vector<std::string> roots_;
    std::atomic<bool> busy_{false};
    std::mutex mu_;
    Progress progress_;
    bool progress_set_ = false;
    CancelToken* cancel_ = nullptr;
    std::shared_ptr<ReaderHandle> reader_;
    std::map<std::string, std::shared_ptr<PlanHandle>> plans_;
    std::map<std::string, Plan> removals_;   // plan id of a data-deletion plan -> unused; kept for the removal that made it
};

}  // namespace ui
}  // namespace install
}  // namespace pbstore
