#include "pbstore/store/store_rules.h"

#include <algorithm>
#include <cctype>
#include <map>

#include "pbstore/install/text_safety.h"
#include "pbstore/store/store_redact.h"

namespace pbstore {
namespace store {

using install::ReaderEffect;

namespace {
constexpr ReaderEffect U = ReaderEffect::Unchanged;
constexpr ReaderEffect R = ReaderEffect::Restored;
}  // namespace

const std::vector<StoreRule>& store_rule_registry() {
    static const std::vector<StoreRule> rules = {
        {"E-STO-01", "The battery is below the minimum for this action (shown: level and minimum)", U, "Charge the reader and try again."},
        {"E-STO-02", "No connection could be made (Wi-Fi off, no saved network, out of range, or no answer within 30 seconds)", U,
         "Switch Wi-Fi on in the reader's settings or move closer to the router, then try again. Browsing the list and removing apps work without a connection."},
        {"E-STO-03", "Not enough free memory to download this app (shown: needed and available)", U, "Close other apps, restart the reader, and try again."},
        {"E-STO-04", "The reader's clock looks wrong (shown: the date the reader believes) so the certificate cannot be checked", U,
         "Set the date and time in the reader's settings and try again. PB Store always checks the certificate."},
        {"E-STO-05", "The battery level cannot be read", U, "Continue only if the reader is charged enough (tap to confirm), or stop here."},
        {"E-STO-06", "A new version of PB Store did not start; the previous version was restored", R,
         "PB Store will not try that version again by itself. Update again when a newer version is offered, or install a fixed version with PB Installer on a computer."},
        {"E-STO-07", "The new version of PB Store could not be put in place (no space, the copy failed, or a tool is missing); the previous version keeps running", U,
         "Free space on the reader and open PB Store again."},
        {"E-STO-08", "PB Store does not remove itself", U, "Use PB Installer on a computer to remove PB Store."},
        {"E-STO-09", "The saved catalog failed its check and was discarded", U, "Tap Refresh with Wi-Fi available. The list shows the apps found on the reader until then."},
        {"E-STO-10", "The reader's storage is read-only or full of errors (a write was refused)", R,
         "Restart the reader. If this stays, the reader's storage may be damaged: back up your books and check it before using PB Store again."},
        {"W-STO-01", "The last action was cancelled by leaving the store; nothing changed", U, ""},
        {"W-STO-02", "Wi-Fi was switched on for this action and could not be switched off again", U, "Switch Wi-Fi off in the reader's settings if you want it off."},
        {"W-STO-03", "Wi-Fi was switched on for this action and switched off again", U, ""},
    };
    return rules;
}

const StoreRule* find_store_rule(const std::string& id) {
    for (const StoreRule& r : store_rule_registry())
        if (id == r.id) return &r;
    return nullptr;
}

Finding make_store_finding(const std::string& id, const std::string& subject, const std::string& detail) {
    const StoreRule* r = find_store_rule(id);
    const std::string message = !detail.empty() ? detail : (r ? r->what : "");
    return id.compare(0, 1, "W") == 0 ? make_notice(id, subject, message) : make_error(id, subject, message);
}

// ---- text that must not tell the user of the reader to use a computer ------------------------------------------

namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool is_word_char(char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0; }

// `word` found at the start of a word (so "amount" does not contain "mount", "applicable" does not contain "cable")
bool has_word_prefix(const std::string& text, const std::string& word) {
    for (size_t pos = text.find(word); pos != std::string::npos; pos = text.find(word, pos + 1))
        if (pos == 0 || !is_word_char(text[pos - 1])) return true;
    return false;
}

std::string replace_all(std::string s, const std::string& from, const std::string& to) {
    for (size_t pos = s.find(from); pos != std::string::npos; pos = s.find(from, pos + to.size())) s.replace(pos, from.size(), to);
    return s;
}

// The engine's own texts speak to a user of a computer; on the reader the same sentences are about the reader.
std::string to_the_reader(std::string s) {
    for (const auto& r : std::vector<std::pair<const char*, const char*>>{{"this computer's", "this reader's"}, {"the computer's", "the reader's"}, {"your computer's", "the reader's"},
                                                                       {"this computer", "this reader"}, {"the computer", "the reader"}, {"your computer", "the reader"},
                                                                       {"the installer only uses HTTPS", "PB Store only uses HTTPS"}, {"the installer", "PB Store"}})
        s = replace_all(s, r.first, r.second);
    return s;
}

// The third part where the installer's text speaks of a computer, a drive, a cable or an option (contracts/store-rules.md).
const std::map<std::string, std::string>& next_overrides() {
    static const std::map<std::string, std::string> m = {
        {"E-INS-03", "Restart the reader and open PB Store again. If this stays, report it to the author of PB Store."},
        {"E-INS-05", "Restart the reader. If this stays, the reader's storage may be damaged: back up your books and check it before using PB Store again."},
        {"E-INS-06", "Restart the reader. If this stays, the reader's storage may be damaged: back up your books and check it before using PB Store again."},
        {"E-INS-09", "Confirm it on the plan page (tap the confirmation that names it), or stop here."},
        {"E-INS-11", "Confirm it on the plan page (tap the confirmation that names it), or stop here."},
        {"E-INS-12", "Nothing was written. Restore system/config/desktop/view.json from the newest backup in system/pbstore/backups/, or let PB Installer on a computer restore it."},
        {"E-INS-13", "Nothing was written. Restore system/config/desktop/view.json from the newest backup in system/pbstore/backups/, or let PB Installer on a computer restore it."},
        {"E-INS-17", "Its contents stay untouched; confirm it on the plan page, or stop here."},
        {"E-INS-20", "Move or rename that file on the reader yourself; PB Store does not replace files it did not create."},
        {"E-INS-23", "Open PB Store again; it offers to repair the interrupted operation first."},
        {"E-INS-26", "Check the Wi-Fi connection and the clock of the reader, then try again."},
        {"E-INS-30", "Open the list and check the app."},
        {"E-INS-31", "Tap Check on the app to adopt it if its files equal the catalog package."},
        {"E-INS-32", "Confirm on the plan page that the changed files are to be handled, or stop here."},
        {"E-INS-34", "Close it and try again."},
        {"E-INS-35", "PB Store needs updating; use PB Installer on a computer to update it. Nothing was touched."},
        {"E-INS-36", "Check the Wi-Fi connection and try again."},
        {"E-INS-38", "Replacing an app that was put on the reader by hand stays with PB Installer on a computer."},
        {"E-CAT-03", "PB Store needs an update; use PB Installer on a computer, because the store cannot read this catalog."},
    };
    return m;
}

const char* kFallbackNext = "Nothing was written. Report what happened to the author of PB Store.";

}  // namespace

bool tells_the_user_to_use_a_computer(const std::string& text) {
    std::string t = lower(text);
    t = replace_all(t, "pb installer on a computer", "");
    for (const char* w : {"computer", "usb", "drive", "mount", "cable", "keyboard"})
        if (has_word_prefix(t, w)) return true;
    return t.find("--") != std::string::npos;
}

std::string shorten_paths(const std::string& text, size_t max_chars) {
    std::string out;
    size_t i = 0;
    while (i < text.size()) {
        if (std::isspace(static_cast<unsigned char>(text[i]))) {
            out += text[i++];
            continue;
        }
        size_t j = i;
        while (j < text.size() && !std::isspace(static_cast<unsigned char>(text[j]))) ++j;
        const std::string token = text.substr(i, j - i);
        if (token.size() > max_chars && token.find('/') != std::string::npos && max_chars > 6) {
            const size_t keep = max_chars - 3;
            out += token.substr(0, keep / 2) + "..." + token.substr(token.size() - (keep - keep / 2));
        } else {
            out += token;
        }
        i = j;
    }
    return out;
}

install::Message store_message(const Finding& finding, ReaderEffect effect) {
    install::Message m = install::describe(finding, effect);
    // A sentence, then the rule id in brackets for the log and for support; the installer's `error[ID] subject:` line is for the command line.
    std::string sentence = install::neutralize(store_redact(to_the_reader(finding.message)));   // an address in a network error is not shown
    if (sentence.empty()) {
        if (const StoreRule* s = find_store_rule(finding.rule_id)) sentence = s->what;
        else if (const install::InstallRule* r = install::find_install_rule(finding.rule_id)) sentence = r->description;
    }
    if (sentence.empty()) sentence = "The action did not work";
    sentence[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(sentence[0])));
    if (sentence.back() != '.') sentence += '.';
    const std::string subject = install::neutralize(finding.subject);
    if (subject.find('/') != std::string::npos) sentence = subject + ": " + sentence;   // a path says where; a label such as "reader" does not
    const std::string id_suffix = " (" + finding.rule_id + ")";
    m.what = sentence + id_suffix;
    if (finding.rule_id == "E-INS-33") m.what = "You cancelled the action." + id_suffix;   // a cancel is the user's own act, not an error
    std::string next;
    bool final_next = false;   // a store rule's own text is the third part as it stands
    if (const StoreRule* s = find_store_rule(finding.rule_id)) {
        next = s->next;
        final_next = true;
    } else if (finding.rule_id == "E-INS-20" && finding.message.find("replace it instead") != std::string::npos) {
        next = "Tap Replace on the app to put the catalog version in place of its files; read the plan first.";   // the engine says so in the first part
    } else if (auto it = next_overrides().find(finding.rule_id); it != next_overrides().end()) {
        next = it->second;
    } else {
        next = to_the_reader(m.next);
        if (tells_the_user_to_use_a_computer(next)) next = kFallbackNext;
    }
    if (!final_next) {
        if (effect == ReaderEffect::RepairNeeded && next.compare(0, 19, "Open PB Store again") != 0) next = "Open PB Store again and confirm the repair. " + next;
        if (effect == ReaderEffect::Restored) next = "Nothing needs doing; the app works as before. " + next;
    }
    m.next = next;
    // one screen: no path longer than 60 characters, the whole text at most 600
    m.what = shorten_paths(m.what, kMaxPathChars);
    m.next = shorten_paths(m.next, kMaxPathChars);
    const std::string reader_effect_line = m.reader_effect;
    const size_t fixed = reader_effect_line.size() + m.next.size() + std::string("\nNext: ").size() + 1;   // + the line break after `what`
    if (fixed < kMaxMessageChars && m.what.size() + fixed > kMaxMessageChars) {
        const size_t budget = kMaxMessageChars - fixed;
        const std::string tail = "..." + id_suffix;   // the rule id stands at the end and stays
        // the cut never splits a UTF-8 character, and the rule id stays even when the budget is small
        auto cut = [](const std::string& text, size_t n) {
            if (n >= text.size()) return text;
            while (n > 0 && (static_cast<unsigned char>(text[n]) & 0xC0) == 0x80) --n;
            return text.substr(0, n);
        };
        if (budget > tail.size()) m.what = cut(m.what, budget - tail.size()) + tail;
        else if (budget > id_suffix.size()) m.what = cut(m.what, budget - id_suffix.size()) + id_suffix;
        else m.what = id_suffix.substr(1);
    }
    return m;
}

install::Message store_message(const Finding& finding) {
    if (const StoreRule* s = find_store_rule(finding.rule_id)) return store_message(finding, s->effect);
    if (const install::InstallRule* r = install::find_install_rule(finding.rule_id)) return store_message(finding, r->effect);
    return store_message(finding, ReaderEffect::Unchanged);
}

}  // namespace store
}  // namespace pbstore
