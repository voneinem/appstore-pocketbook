// pbstore-catalog: see docs/contracts/cli.md.
#include <iostream>

#include "common.h"
#include "pbstore/schema.h"

namespace {

void print_usage() {
    std::cerr << "usage: pbstore-catalog <command> [options]\n\ncommands:\n";
    for (const auto& c : pbcli::commands()) std::cerr << "  " << c.first << "\n      " << c.second.first << "\n";
    std::cerr << "\n  --version   print the tool version and the supported schema versions\n";
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);
    if (args.empty()) {
        print_usage();
        return 2;
    }
    if (args[0] == "--version") {
        std::cout << "pbstore-catalog " << PBSTORE_VERSION << " (catalog, manifest and entry schema "
                  << pbstore::kSupportedSchemaMajor << ".x, writes " << pbstore::kSupportedSchema << ")\n";
        return 0;
    }
    if (args[0] == "--help" || args[0] == "help") {
        print_usage();
        return 0;
    }
    auto it = pbcli::commands().find(args[0]);
    if (it == pbcli::commands().end()) {
        std::cerr << "error: unknown command '" << args[0] << "'\n";
        print_usage();
        return 2;
    }
    pbcli::Args parsed = pbcli::parse_args(std::vector<std::string>(args.begin() + 1, args.end()));
    if (!parsed.error.empty()) return pbcli::usage_error(parsed.error);
    try {
        return it->second.second(parsed);
    } catch (const std::exception& e) {
        return pbcli::usage_error(e.what());
    }
}
