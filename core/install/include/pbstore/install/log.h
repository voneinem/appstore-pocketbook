#pragma once
// The log on the computer: one line per action, with no personal data. The text of a line goes through
// the redactor first. The log stays on the computer; nothing of it is ever written to the reader.

#include <string>
#include <vector>

#include "pbstore/install/clock.h"
#include "pbstore/install/state_dirs.h"

namespace pbstore {
namespace install {

// Replaces what could identify the user or the reader by fixed placeholders.
class Redactor {
public:
    void set_mount_path(const std::string& p) { mount_ = p; }          // -> <reader>
    void set_volume_label(const std::string& l) { label_ = l; }        // -> <label>
    void set_local_file(const std::string& f) { file_ = f; }           // -> <file>
    void set_home(const std::string& h) { home_ = h; }                 // -> <home>

    std::string redact(const std::string& text) const;
    // Drops "user:password@" and the query string and fragment of a URL.
    static std::string clean_url(const std::string& url);

private:
    std::string mount_, label_, file_, home_;
};

class Log {
public:
    static constexpr uint64_t kMaxFileBytes = 512u * 1024u;
    Log(StateDirs& dirs, Clock& clock, const Redactor& redactor) : dirs_(dirs), clock_(clock), redactor_(redactor) {}

    // "<UTC time> <action> <app-id> <version> <result> <rule ids...> <text>"; app id and version are "-"
    // when there are none. Failing to write the log never fails an action.
    void event(const std::string& action, const std::string& app_id, const std::string& version, const std::string& result,
               const std::vector<std::string>& rule_ids, const std::string& text = "");

private:
    StateDirs& dirs_;
    Clock& clock_;
    const Redactor& redactor_;
};

}  // namespace install
}  // namespace pbstore
