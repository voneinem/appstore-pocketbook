#include "gui.h"

#include <atomic>
#include <thread>

#include "api.h"
#include "http_server.h"

namespace pbstore {
namespace install {
namespace ui {

int run_assistant(Engine& engine, OpenUrl& opener, std::ostream& out, std::ostream& err, const std::string& home, const std::vector<std::string>& roots,
                  CancelToken& interrupt) {
    Api api(engine, home, roots);
    HttpServer::Options options;
    options.busy = [&api] { return api.busy(); };
    HttpServer server([&api](const HttpRequest& r) { return api.handle(r); }, options);
    std::string error;
    if (!server.start(&error)) {
        err << "The assistant could not be started: " << error << "\n";
        return 2;
    }
    const std::string url = server.url();
    out << "The assistant is at " << url << "\n" << std::flush;
    if (!opener.open(url))
        out << "The browser could not be opened. Open the address above in your browser; it works only on this computer and only while this window stays open.\n" << std::flush;
    std::atomic<bool> done{false};
    std::thread watcher([&] {
        while (!done) {
            if (interrupt.cancelled()) server.stop();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    });
    server.run();
    done = true;
    watcher.join();
    out << "The assistant has ended.\n";
    return 0;
}

}  // namespace ui
}  // namespace install
}  // namespace pbstore
