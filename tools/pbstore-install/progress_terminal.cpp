#include "progress_terminal.h"

#include <chrono>
#include <iostream>

#include "prompts.h"

namespace pbstore {
namespace install {
namespace cli {

namespace {
bool g_dirty = false;
std::chrono::steady_clock::time_point g_last;
std::string g_last_phase;
}  // namespace

ProgressFn terminal_progress() {
    if (!stderr_is_terminal()) return [](const Progress&) {};
    return [](const Progress& p) {
        const auto now = std::chrono::steady_clock::now();
        const bool phase_changed = p.phase != g_last_phase;
        if (!phase_changed && g_dirty && now - g_last < std::chrono::milliseconds(250)) return;
        g_last = now;
        g_last_phase = p.phase;
        std::string line = p.text;
        if (p.total > 0) {
            const unsigned pct = static_cast<unsigned>(p.done * 100 / p.total);
            line += " - " + std::to_string(pct) + "% (" + std::to_string(p.done) + " of " + std::to_string(p.total) + " " + p.unit + ")";
        }
        std::cerr << "\r\033[K" << line << std::flush;
        g_dirty = true;
    };
}

void finish_terminal_progress() {
    if (g_dirty) std::cerr << "\r\033[K" << std::flush;
    g_dirty = false;
}

}  // namespace cli
}  // namespace install
}  // namespace pbstore
