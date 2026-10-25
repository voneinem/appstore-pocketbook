// T115: the pages of the assistant: every api/ string they use is in the API table of the contract and the
// other way round; no inline script or style; catalog and package text only through textContent; real controls;
// no state carried by colour alone; English.
#include "test_support.h"

#include <dirent.h>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>

namespace {

std::string slurp(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}
std::string assets_dir() { return std::string(PBSTORE_SOURCE_DIR) + "/tools/pbstore-install-ui/assets"; }
bool has(const std::string& t, const std::string& n) { return t.find(n) != std::string::npos; }

std::set<std::string> api_strings_of_the_page(const std::string& js) {
    std::set<std::string> found;
    const std::regex re("[\"'`](api/[a-z/]+)");
    for (std::sregex_iterator it(js.begin(), js.end(), re), end; it != end; ++it) found.insert((*it)[1]);
    return found;
}

std::set<std::string> api_paths_of_the_contract() {
    const std::string md = slurp(std::string(PBSTORE_SOURCE_DIR) + "/specs/002-pb-installer-linux/contracts/ui-api.md");
    std::set<std::string> found;
    const std::regex row("\\|\\s*`(?:GET|POST)\\s+(api/[a-z/]+)");
    for (std::sregex_iterator it(md.begin(), md.end(), row), end; it != end; ++it) found.insert((*it)[1]);
    // rows that name two endpoints: "POST api/repair/plan`, `POST api/repair`"
    const std::regex second("`POST\\s+(api/[a-z/]+)`");
    for (std::sregex_iterator it(md.begin(), md.end(), second), end; it != end; ++it) found.insert((*it)[1]);
    return found;
}

}  // namespace

PB_TEST(the_page_exists_and_is_in_english) {
    const std::string html = slurp(assets_dir() + "/index.html");
    PB_CHECK(!html.empty());
    PB_CHECK(!slurp(assets_dir() + "/app.js").empty());
    PB_CHECK(!slurp(assets_dir() + "/style.css").empty());
    PB_CHECK(has(html, "<html lang=\"en\""));
    PB_CHECK(has(html, "PB Installer"));
}

PB_TEST(every_api_string_of_the_page_is_in_the_contract_and_every_endpoint_is_used) {
    const std::set<std::string> used = api_strings_of_the_page(slurp(assets_dir() + "/app.js"));
    const std::set<std::string> contract = api_paths_of_the_contract();
    PB_CHECK(contract.size() >= 14);
    for (const std::string& u : used)
        if (!contract.count(u)) PB_FAIL_AT("the page uses " + u + ", which is not in the contract");
    for (const std::string& c : contract)
        if (!used.count(c)) PB_FAIL_AT("the contract has " + c + ", which the page never uses");
}

PB_TEST(the_plan_text_on_the_page_condenses_unchanged_files_and_the_table_still_lists_every_file) {
    // a plan of an app with hundreds of unchanged files would be unreadable; the full list stays under "All files"
    const std::string js = slurp(assets_dir() + "/app.js");
    const std::string html = slurp(assets_dir() + "/index.html");
    PB_CHECK(has(js, "condensePlanText(plan.text)"));
    PB_CHECK(has(js, "for (const item of s.items)"));   // one table row per file of the plan, changed or not
    PB_CHECK(has(html, "<summary>All files</summary>"));
}

PB_TEST(there_is_no_inline_script_or_style_and_no_event_handler_attribute) {
    const std::string html = slurp(assets_dir() + "/index.html");
    // scripts only by src, never with content
    const std::regex script_re("<script([^>]*)>([\\s\\S]*?)</script>");
    for (std::sregex_iterator it(html.begin(), html.end(), script_re), end; it != end; ++it) {
        PB_CHECK(has((*it)[1].str(), "src="));
        PB_CHECK((*it)[2].str().find_first_not_of(" \t\r\n") == std::string::npos);
    }
    PB_CHECK(!std::regex_search(html, std::regex("<style\\b")));
    PB_CHECK(!std::regex_search(html, std::regex("\\sstyle\\s*=")));
    PB_CHECK(!std::regex_search(html, std::regex("\\son[a-z]+\\s*=")));
    PB_CHECK(!has(html, "javascript:"));
    const std::string js = slurp(assets_dir() + "/app.js");
    PB_CHECK(!std::regex_search(js, std::regex("\\.setAttribute\\(\\s*['\"]on")));
    PB_CHECK(!std::regex_search(js, std::regex("\\beval\\s*\\(")));
    PB_CHECK(!std::regex_search(js, std::regex("new\\s+Function\\s*\\(")));
    PB_CHECK(!std::regex_search(js, std::regex("document\\.write")));
    PB_CHECK(!std::regex_search(js, std::regex("\\.style\\.cssText")));
}

PB_TEST(text_from_the_catalog_and_packages_is_inserted_only_as_text) {
    DIR* d = ::opendir(assets_dir().c_str());
    PB_CHECK(d != nullptr);
    while (dirent* e = ::readdir(d)) {
        const std::string name = e->d_name;
        if (name == "." || name == "..") continue;
        const std::string text = slurp(assets_dir() + "/" + name);
        for (const char* bad : {"innerHTML", "outerHTML", "insertAdjacentHTML", "DOMParser", "createContextualFragment", "srcdoc"})
            if (has(text, bad)) PB_FAIL_AT(name + " uses " + bad);
    }
    ::closedir(d);
    PB_CHECK(has(slurp(assets_dir() + "/app.js"), "textContent"));
}

PB_TEST(external_links_carry_noopener_noreferrer) {
    const std::string js = slurp(assets_dir() + "/app.js");
    const std::string html = slurp(assets_dir() + "/index.html");
    // every place that makes a link from the catalog sets rel
    size_t anchors = 0;
    for (size_t at = js.find("createElement('a')"); at != std::string::npos; at = js.find("createElement('a')", at + 1)) {
        ++anchors;
        const std::string around = js.substr(at, 600);
        PB_CHECK(has(around, "noopener noreferrer"));
    }
    const std::regex anchor_re("<a\\b[^>]*>");
    for (std::sregex_iterator it(html.begin(), html.end(), anchor_re), end; it != end; ++it) {
        const std::string a = (*it)[0];
        if (has(a, "http")) PB_CHECK(has(a, "rel=\"noopener noreferrer\""));
    }
    (void)anchors;
}

PB_TEST(controls_are_real_elements_and_every_state_has_a_text_label) {
    const std::string html = slurp(assets_dir() + "/index.html");
    const std::string js = slurp(assets_dir() + "/app.js");
    PB_CHECK(has(html, "<button"));
    // no clickable div or span
    PB_CHECK(!std::regex_search(html, std::regex("<(div|span)[^>]*\\b(onclick|role=\"button\")")));
    PB_CHECK(!std::regex_search(js, std::regex("createElement\\(['\"](div|span)['\"]\\)[\\s\\S]{0,120}addEventListener\\(['\"]click")));
    // each state the API names has a visible label in the page
    for (const char* label : {"not installed", "installed", "update available", "installed outside the installer", "on the reader, not in the catalog"})
        PB_CHECK(has(js, label));
    // labels for form controls
    PB_CHECK(has(html, "<label") || has(js, "createElement('label')"));
    // the style does not use colour as the only difference: states are text; the page declares a focus style
    const std::string css = slurp(assets_dir() + "/style.css");
    PB_CHECK(has(css, ":focus"));
    PB_CHECK(has(css, "prefers-color-scheme") || has(css, "color-scheme"));
}

PB_TEST_MAIN()
