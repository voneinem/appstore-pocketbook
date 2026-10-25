#pragma once
// Shared by the commands of the command line: the engine, the streams and the printing of findings.

#include <iostream>
#include <string>
#include <vector>

#include "args.h"
#include "pbstore/install/engine.h"
#include "pbstore/install/messages.h"

namespace pbstore {
namespace install {
namespace cli {

struct Context {
    Engine& engine;
    std::ostream& out;
    std::ostream& err;
    bool interactive = false;   // stdin and stdout are terminals
    std::string program_version;
};

// error[<id>] <subject>: <message> for each finding, then the state of the reader and what to do next
// (once for all). Returns the exit status of these findings.
int print_findings(Context& ctx, const std::vector<Finding>& findings, ReaderEffect effect);
void print_notices(Context& ctx, const std::vector<Finding>& notices);

// "1.5 MiB"
std::string human_size(uint64_t bytes);

// Commands. Each returns the exit status.
int cmd_readers(Context& ctx, const Options& opts);
int cmd_installed(Context& ctx, const Options& opts);
int cmd_list(Context& ctx, const Options& opts);
int cmd_repair(Context& ctx, const Options& opts);
int cmd_install(Context& ctx, const Options& opts);
int cmd_update(Context& ctx, const Options& opts);
int cmd_remove(Context& ctx, const Options& opts);
int cmd_adopt(Context& ctx, const Options& opts);
int cmd_replace(Context& ctx, const Options& opts);

// Opens the reader named by --reader or the single qualifying drive. Prints the findings and returns false
// (with the status in *status) if there is none.
bool open_reader_for(Context& ctx, const Options& opts, std::shared_ptr<ReaderHandle>* reader, int* status);

// If an operation was left unfinished: shows it. With --confirm-repair (or a yes on a terminal) repairs it,
// reports and ends. *handled is set when the command must end with the returned status.
int handle_interrupted(Context& ctx, const Options& opts, ReaderHandle& reader, bool* handled);

}  // namespace cli
}  // namespace install
}  // namespace pbstore
