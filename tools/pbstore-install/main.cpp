// pbstore-install: the installer program. This file only wires the engine to the operating system and
// dispatches the commands; the logic is in pbstore_install.
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>

#include "args.h"
#include "cli.h"
#include "pbstore/base64.h"
#include "pbstore/install/build_config.h"
#include "pbstore/install/engine.h"
#include "pbstore/install/posix_local_file.h"
#include "pbstore/install/posix_mount_source.h"
#include "pbstore/install/posix_reader_fs.h"
#include "pbstore/install/posix_state_dirs.h"
#include "pbstore_cacert.h"
#include "gui.h"
#include "open_url_xdg.h"
#include "prompts.h"
#include "signals.h"
#include "test_hooks.h"

using namespace pbstore::install;

namespace {

bool read_whole_file(const std::string& path, std::string* out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    *out = ss.str();
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);
    cli::Options opts = cli::parse_args(args);
    const std::string version = PBSTORE_VERSION;

    if (!opts.usage_error.empty()) {
        std::cerr << "pbstore-install: " << opts.usage_error << "\n";
        return 2;
    }
    if (opts.version) {
        std::cout << "pbstore-install " << version << "\nsupported schemas: catalog 1, package manifest 1, install record 1, journal 1\n"
                  << "trusted catalog keys: " << trusted_keys().size() << "\n";
        for (const std::string& key : trusted_keys()) std::cout << "  " << pbstore::base64_encode(key) << "\n";
        return 0;
    }
    const bool graphical = (std::getenv("DISPLAY") && *std::getenv("DISPLAY")) || (std::getenv("WAYLAND_DISPLAY") && *std::getenv("WAYLAND_DISPLAY"));
    const bool start_assistant = opts.command == "gui" || (opts.command.empty() && !opts.help && graphical);
    if (opts.help || (opts.command.empty() && !start_assistant)) {
        // a start without arguments opens the assistant when there is a graphical session, otherwise it prints the help
        (opts.help ? std::cout : std::cerr) << cli::help_text(version);
        return opts.help ? 0 : 2;
    }

    // ---- the operating system --------------------------------------------------------------------------------
    PosixMountSource real_mounts;
    MountSource* mounts = &real_mounts;
    std::string state_override;
    NetOptions net = choose_trust_anchors(system_ca_bundle_candidates(), read_whole_file, pbstore_cacert_bundle());
    net.user_agent = "pbstore-install/" + version;
    ReaderFsFactory open_fs = [](const std::string& root) -> std::unique_ptr<ReaderFs> { return std::make_unique<PosixReaderFs>(root); };
#ifdef PBSTORE_TEST_BUILD
    mounts = cli::test_mount_source_or(mounts);
    state_override = test_state_dir();
    if (!test_ca_file().empty()) {
        net.ca_file = test_ca_file();
        net.ca_blob.clear();
    }
    open_fs = cli::apply_test_stop_after(open_fs);
#endif
    Downloader download = make_https_downloader(net);
#ifdef PBSTORE_TEST_BUILD
    download = cli::apply_test_host_map(download);
#endif

    PosixStateDirs state(state_override);
    SystemClock system_clock;
    Clock* clock_ptr = &system_clock;
#ifdef PBSTORE_TEST_BUILD
    if (Clock* c = cli::test_clock()) clock_ptr = c;
#endif
    Clock& clock = *clock_ptr;
    Redactor redactor;
    if (const char* home = std::getenv("HOME")) redactor.set_home(home);
    Log log(state, clock, redactor);

    Config config;
    config.catalog_url = catalog_url();
    config.trusted_keys = trusted_keys();
    config.program_version = version;
    Deps deps;
    deps.open_fs = open_fs;
    deps.mounts = mounts;
    deps.state = &state;
    deps.download = download;
    deps.clock = &clock;
    deps.log = &log;
    deps.redactor = &redactor;
    deps.identity_sources = default_identity_sources();
#ifdef PBSTORE_TEST_BUILD
    if (IdentitySource* id = cli::test_identity_source()) deps.identity_sources.push_back(id);
#endif
    deps.read_local_file = read_regular_file;
    Engine engine(config, deps);

    if (start_assistant) {
        ui::XdgOpenUrl xdg;
        OpenUrl* opener = &xdg;
#ifdef PBSTORE_TEST_BUILD
        if (OpenUrl* o = cli::test_open_url()) opener = o;
#endif
        CancelToken interrupt;
        cli::SigintCancel sigint(&interrupt);
        const char* home_env = std::getenv("HOME");
        const std::string home = home_env ? home_env : "";
        return ui::run_assistant(engine, *opener, std::cout, std::cerr, home, {"/run/media", "/media", "/mnt"}, interrupt);
    }

    cli::Context ctx{engine, std::cout, std::cerr, cli::stdin_and_stdout_are_terminals(), version};
    const std::string& c = opts.command;
    if (c == "readers") return cli::cmd_readers(ctx, opts);
    if (c == "installed") return cli::cmd_installed(ctx, opts);
    if (c == "list") return cli::cmd_list(ctx, opts);
    if (c == "repair") return cli::cmd_repair(ctx, opts);
    if (c == "install") return cli::cmd_install(ctx, opts);
    if (c == "update") return cli::cmd_update(ctx, opts);
    if (c == "remove") return cli::cmd_remove(ctx, opts);
    if (c == "adopt") return cli::cmd_adopt(ctx, opts);
    if (c == "replace") return cli::cmd_replace(ctx, opts);
    return 2;
}
