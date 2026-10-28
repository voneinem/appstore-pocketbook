#include "pbstore/store/run_state.h"

#include <cctype>

#include "pbstore/semver.h"
#include "pbstore/store/state_names.h"

namespace pbstore {
namespace store {

namespace {

std::string first_line(std::string text) {
    const size_t nl = text.find('\n');
    if (nl != std::string::npos) text.erase(nl);
    while (!text.empty() && (text.back() == '\r' || text.back() == ' ')) text.pop_back();
    return text;
}

}  // namespace

bool RunState::trying() { return run_.size("trying") != UINT64_MAX; }

void RunState::mark_started() { run_.remove("trying"); }

std::optional<int> RunState::last_exit() {
    const uint64_t size = run_.size("last-exit");
    if (size == UINT64_MAX || size > limit_of("run/last-exit")) return std::nullopt;
    std::string text;
    if (!run_.read("last-exit", &text)) return std::nullopt;
    text = first_line(text);
    if (text.empty() || text.size() > 6) return std::nullopt;
    int value = 0;
    for (unsigned char c : text) {
        if (!std::isdigit(c)) return std::nullopt;
        value = value * 10 + (c - '0');
    }
    return value;
}

bool RunState::failed_present() { return run_.size("pbstore.bin.failed") != UINT64_MAX; }

RunMessage RunState::take_message() {
    RunMessage m;
    const uint64_t size = run_.size("message");
    if (size == UINT64_MAX) return m;
    std::string text;
    const bool readable = size <= limit_of("run/message") && run_.read("message", &text);
    run_.remove("message");
    if (!readable) return m;                 // over its limit: absent
    text = first_line(text);
    if (text.empty()) return m;
    const size_t space = text.find(' ');
    const std::string word = text.substr(0, space);
    m.detail = space == std::string::npos ? "" : text.substr(space + 1);
    if (word == "update-failed") m.kind = RunMessage::Kind::UpdateFailed;
    else if (word == "restored") m.kind = RunMessage::Kind::Restored;
    else if (word == "swap-skipped") m.kind = RunMessage::Kind::SwapSkipped;
    else m.kind = RunMessage::Kind::Unknown;
    return m;
}

std::optional<std::string> RunState::last_run_version() {
    const uint64_t size = app_.size("last-run-version");
    if (size == UINT64_MAX || size > limit_of("last-run-version")) return std::nullopt;
    std::string text;
    if (!app_.read("last-run-version", &text)) return std::nullopt;
    text = first_line(text);
    if (!parse_semver(text)) return std::nullopt;
    return text;
}

bool RunState::remember_run_version(const std::string& version) {
    if (version.size() + 1 > limit_of("last-run-version") || !parse_semver(version)) return false;
    return app_.write_atomic("last-run-version", version + "\n");
}

}  // namespace store
}  // namespace pbstore
