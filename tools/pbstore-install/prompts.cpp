#include "prompts.h"

#include <unistd.h>

#include <cctype>
#include <iostream>

namespace pbstore {
namespace install {
namespace cli {

bool stdin_and_stdout_are_terminals() { return ::isatty(0) && ::isatty(1); }
bool stderr_is_terminal() { return ::isatty(2); }

std::string ask_line(const std::string& question) {
    std::cout << question << " " << std::flush;
    std::string line;
    if (!std::getline(std::cin, line)) return "";
    return line;
}

bool ask_yes_no(const std::string& question) {
    std::string a = ask_line(question + " [y/N]");
    for (char& c : a) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return a == "y" || a == "yes";
}

}  // namespace cli
}  // namespace install
}  // namespace pbstore
