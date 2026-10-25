#pragma once
// Questions on the terminal. Only asked when there is a terminal; the safe answer (no) is the default.

#include <string>

namespace pbstore {
namespace install {
namespace cli {

bool stdin_and_stdout_are_terminals();
bool stderr_is_terminal();
// Prints the question and reads one line; only "y" or "yes" (any case) is a yes.
bool ask_yes_no(const std::string& question);
// Prints the question and reads one line of text (for the data-deletion confirmations).
std::string ask_line(const std::string& question);

}  // namespace cli
}  // namespace install
}  // namespace pbstore
