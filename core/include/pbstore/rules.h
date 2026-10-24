#pragma once
// Rule ids, findings and their formatting. Every check in the contracts has a stable id:
//   E-ENT-nn  entry file          E-PKG-nn  package and manifest
//   E-CAT-nn  catalog (consumer)  E-BLD-nn  build and publish
//   W-ENT-nn, W-PR-nn  notices for the reviewer (never fail a check)

#include <string>
#include <vector>

namespace pbstore {

enum class Severity { Error, Notice };

struct Finding {
    Severity severity = Severity::Error;
    std::string rule_id;   // for example "E-PKG-12"
    std::string subject;   // entry id, file path, field name ...
    std::string message;   // plain text, names the offending value
};

struct RuleInfo {
    const char* id;
    const char* description;  // one line, plain language
};

const std::vector<RuleInfo>& rule_registry();
bool is_known_rule(const std::string& id);

Finding make_error(const std::string& rule_id, const std::string& subject, const std::string& message);
Finding make_notice(const std::string& rule_id, const std::string& subject, const std::string& message);

// "error[E-PKG-12] applications/x.bin: message" or "notice[W-ENT-01] subject: message"
std::string format_finding(const Finding& f);
// GitHub workflow command: "::error file=<path>::[E-PKG-12] subject: message" (::notice for notices).
// Message and property values are escaped as the workflow-command syntax requires.
std::string github_annotation(const Finding& f, const std::string& file = "");

}  // namespace pbstore
