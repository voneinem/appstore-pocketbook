#pragma once
// The command line of pbstore-install (contracts/cli.md): parsing only, no I/O.

#include <optional>
#include <string>
#include <vector>

#include "pbstore/install/confirmations.h"

namespace pbstore {
namespace install {
namespace cli {

struct Options {
    std::string command;                     // readers, installed, list, install, update, remove, adopt, replace, repair, gui; "" for none
    std::string app_id;
    std::string local_file;                  // install --file PACKAGE.zip
    std::optional<std::string> reader;       // --reader DIR
    bool dry_run = false;
    bool help = false;
    bool version = false;
    bool delete_data = false;                // remove --delete-data
    Confirmations confirmed;                 // the --confirm-* options that were given
    std::optional<std::string> delete_token_1;   // --confirm-delete-data-1=<folder>
    std::optional<std::string> delete_token_2;   // --confirm-delete-data-2=<count>
    std::string usage_error;                 // set when the arguments are not valid
};

Options parse_args(const std::vector<std::string>& args);

// The help text (also printed for a start without arguments and no graphical session).
std::string help_text(const std::string& program_version);

}  // namespace cli
}  // namespace install
}  // namespace pbstore
