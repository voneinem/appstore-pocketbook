// T034: property test of the desktop configuration editor: 500 generated files, random edits.
// After every edit: the new text parses; the parsed tree equals the old tree plus exactly the intended
// change; and cutting the changed spans out of old and new text leaves identical bytes (spec FR-014).
#include "pbstore/install/desktop_config.h"
#include "pbstore/json.h"
#include "test_support.h"

#include <algorithm>
#include <random>

using namespace pbstore;
using namespace pbstore::install;

namespace {

struct Gen {
    std::mt19937 rng;
    explicit Gen(unsigned seed) : rng(seed) {}
    int pick(int n) { return static_cast<int>(rng() % static_cast<unsigned>(n)); }
    bool coin() { return pick(2) == 0; }
    std::string ws() {
        static const char* options[] = {"", "", " ", "\n", "\n  ", "\n    ", "\t", "\r\n", "\n\t\t", "  "};
        return options[pick(10)];
    }
    std::string number() {
        static const char* options[] = {"0", "1", "-5", "0.1", "1e3", "1.5E-2", "12345678901234567890", "3.14159"};
        return options[pick(8)];
    }
    std::string str(const std::string& base) {
        static const char* extras[] = {"", " x", "\\u00e4", "\\n", "\\\"", "\\\\"};
        return "\"" + base + extras[pick(6)] + "\"";
    }
    std::string extra_value(int depth) {
        switch (pick(depth > 2 ? 5 : 7)) {
            case 0: return number();
            case 1: return "true";
            case 2: return "null";
            case 3: return str("text");
            case 4: return "false";
            case 5: {
                std::string s = "[" + ws();
                const int n = pick(4);
                for (int i = 0; i < n; ++i) s += (i ? "," + ws() : std::string()) + extra_value(depth + 1);
                return s + ws() + "]";
            }
            default: {
                std::string s = "{" + ws();
                const int n = pick(3);
                for (int i = 0; i < n; ++i) s += (i ? "," + ws() : std::string()) + "\"k" + std::to_string(i) + "\"" + ws() + ":" + ws() + extra_value(depth + 1);
                return s + ws() + "}";
            }
        }
    }
};

struct Doc {
    std::string text;
    std::vector<std::string> ids;
    std::vector<std::string> groups;
};

Doc generate(Gen& g) {
    Doc d;
    const int napps = g.pick(6);
    for (int i = 0; i < napps; ++i) d.ids.push_back((g.coin() ? "U_App" : "Vendor") + std::to_string(i));
    const int ngroups = 1 + g.pick(3);
    for (int i = 0; i < ngroups; ++i) d.groups.push_back("Group" + std::to_string(i));

    std::string apps = "{" + g.ws();
    for (size_t i = 0; i < d.ids.size(); ++i) {
        if (i) apps += "," + g.ws();
        apps += "\"" + d.ids[i] + "\"" + g.ws() + ":" + g.ws() + "{" + g.ws() + "\"path\"" + g.ws() + ":" + g.ws() + g.str("/mnt/ext1/applications/x.app") + "," + g.ws() +
                "\"title\":" + g.ws() + g.str("Title") + (g.coin() ? "," + g.ws() + "\"icon\":" + g.ws() + "{\"path\":" + g.str("i.bmp") + "}" : std::string()) +
                (g.coin() ? "," + g.ws() + "\"extra\":" + g.ws() + g.extra_value(0) : std::string()) + g.ws() + "}";
    }
    apps += g.ws() + "}";

    std::string groups = "[" + g.ws();
    for (size_t i = 0; i < d.groups.size(); ++i) {
        if (i) groups += "," + g.ws();
        std::string list = "[" + g.ws();
        bool first = true;
        for (const std::string& id : d.ids)
            if (g.pick(d.groups.size() == 1 ? 1 : 2) == 0 || i == 0) {
                if (!first) list += "," + g.ws();
                list += "\"" + id + "\"";
                first = false;
            }
        list += g.ws() + "]";
        groups += "{" + g.ws() + "\"title\"" + g.ws() + ":" + g.ws() + "\"" + d.groups[i] + "\"" + g.ws() + "," + g.ws() + "\"apps\"" + g.ws() + ":" + g.ws() + list +
                  (g.coin() ? "," + g.ws() + "\"flag\":" + g.extra_value(0) : std::string()) + g.ws() + "}";
    }
    groups += g.ws() + "]";

    std::vector<std::string> members = {"\"applications\"" + g.ws() + ":" + g.ws() + apps, "\"view\"" + g.ws() + ":" + g.ws() + "{" + g.ws() + "\"_comment\"" + g.ws() + ":" + g.ws() + "[\"note\"]" + g.ws() + "," + g.ws() + "\"groups\"" + g.ws() + ":" + g.ws() + groups + g.ws() + "}"};
    const int nextra = g.pick(4);
    for (int i = 0; i < nextra; ++i) members.push_back("\"extra" + std::to_string(i) + "\"" + g.ws() + ":" + g.ws() + g.extra_value(0));
    std::shuffle(members.begin(), members.end(), g.rng);
    d.text = g.ws() + "{" + g.ws();
    for (size_t i = 0; i < members.size(); ++i) d.text += (i ? "," + g.ws() : std::string()) + members[i];
    d.text += g.ws() + "}" + g.ws();
    return d;
}

// the tree the edit must produce, built independently of the editor
json::Value expected_tree(json::Value tree, const Change& c) {
    json::Value* apps = nullptr;
    json::Value* groups = nullptr;
    for (auto& kv : tree.object) {
        if (kv.first == "applications") apps = &kv.second;
        if (kv.first == "view")
            for (auto& vkv : kv.second.object)
                if (vkv.first == "groups") groups = &vkv.second;
    }
    auto value_tree = [&](const EntryValue& v) {
        json::Value o = json::Value::make_object();
        o.set("path", json::Value::from_string(v.path));
        o.set("title", json::Value::from_string(v.title));
        json::Value i = json::Value::make_object();
        i.set("path", json::Value::from_string(v.icon_path));
        o.set("icon", i);
        json::Value f = json::Value::make_object();
        f.set("path", json::Value::from_string(v.focused_icon_path));
        o.set("focused_icon", f);
        return o;
    };
    auto drop_from_groups = [&](const std::string& id) {
        for (auto& gr : groups->array)
            for (auto& kv : gr.object)
                if (kv.first == "apps")
                    for (size_t i = kv.second.array.size(); i-- > 0;)
                        if (kv.second.array[i].string == id) kv.second.array.erase(kv.second.array.begin() + static_cast<long>(i));
    };
    auto add_to_group = [&](const std::string& id, const std::string& title) {
        for (auto& gr : groups->array)
            if (gr.get_string("title") == title) {
                for (auto& kv : gr.object) if (kv.first == "apps") kv.second.array.push_back(json::Value::from_string(id));
                return;
            }
    };
    switch (c.kind) {
        case Change::Kind::Add:
            apps->object.emplace_back(c.id, value_tree(c.value));
            add_to_group(c.id, c.group);
            break;
        case Change::Kind::Replace: {
            for (auto& kv : apps->object) if (kv.first == c.id) kv.second = value_tree(c.value);
            // the id moves only when the wanted group does not list it yet
            bool listed = false;
            for (auto& gr : groups->array)
                if (gr.get_string("title") == c.group)
                    for (auto& kv : gr.object)
                        if (kv.first == "apps") for (auto& e : kv.second.array) listed = listed || e.string == c.id;
            if (!listed) { drop_from_groups(c.id); add_to_group(c.id, c.group); }
            break;
        }
        case Change::Kind::Remove:
            for (size_t i = 0; i < apps->object.size(); ++i) if (apps->object[i].first == c.id) { apps->object.erase(apps->object.begin() + static_cast<long>(i)); break; }
            drop_from_groups(c.id);
            break;
    }
    return tree;
}

bool trees_equal(const json::Value& a, const json::Value& b) { return json::write(a) == json::write(b); }

bool same_outside(const std::string& old_text, const std::string& new_text, const std::vector<Splice>& splices) {
    std::string a = old_text, b = new_text;
    std::vector<size_t> new_begin;
    long shift = 0;
    for (const Splice& s : splices) {
        new_begin.push_back(static_cast<size_t>(static_cast<long>(s.begin) + shift));
        shift += static_cast<long>(s.text.size()) - static_cast<long>(s.end - s.begin);
    }
    for (size_t i = splices.size(); i-- > 0;) {
        a.erase(splices[i].begin, splices[i].end - splices[i].begin);
        b.erase(new_begin[i], splices[i].text.size());
    }
    return a == b;
}
}  // namespace

PB_TEST(five_hundred_random_files_and_edits_keep_every_other_byte) {
    int edits = 0, adds = 0, replaces = 0, removes = 0;
    for (unsigned seed = 1; seed <= 500; ++seed) {
        Gen g(seed);
        Doc d = generate(g);
        ParseResult p = parse_desktop_config(d.text);
        if (!p.config) PB_FAIL_AT("seed " + std::to_string(seed) + ": generated file refused: " + p.findings[0].message + "\n" + d.text);
        EntryValue v = {"/mnt/ext1/applications/new.app", "New \"App\"", "applications/icons/new.bmp", "applications/icons/new_f.bmp"};
        Change c;
        switch (g.pick(3)) {
            case 0: c = add_entry("U_Fresh", v, d.groups[static_cast<size_t>(g.pick(static_cast<int>(d.groups.size())))]); ++adds; break;
            case 1:
                if (d.ids.empty()) { c = add_entry("U_Fresh", v, d.groups[0]); ++adds; break; }
                c = replace_entry(d.ids[static_cast<size_t>(g.pick(static_cast<int>(d.ids.size())))], v, d.groups[static_cast<size_t>(g.pick(static_cast<int>(d.groups.size())))]);
                ++replaces;
                break;
            default:
                if (d.ids.empty()) { c = add_entry("U_Fresh", v, d.groups[0]); ++adds; break; }
                c = remove_entry(d.ids[static_cast<size_t>(g.pick(static_cast<int>(d.ids.size())))]);
                ++removes;
        }
        EditResult r = edit(*p.config, c);
        if (!r.text) PB_FAIL_AT("seed " + std::to_string(seed) + ": edit failed: " + (r.findings.empty() ? "?" : r.findings[0].message) + "\n" + d.text);
        ++edits;
        // 1. the new text parses
        ParseResult after = parse_desktop_config(*r.text);
        if (!after.config) PB_FAIL_AT("seed " + std::to_string(seed) + ": the new text does not parse\n" + *r.text);
        // 2. the parsed tree equals the old tree plus exactly the intended change
        if (!trees_equal(after.config->tree, expected_tree(p.config->tree, c)))
            PB_FAIL_AT("seed " + std::to_string(seed) + ": the tree differs from the expected one\nold: " + d.text + "\nnew: " + *r.text);
        // 3. cutting the changed spans out of old and new text leaves identical bytes
        if (!same_outside(d.text, *r.text, r.splices)) PB_FAIL_AT("seed " + std::to_string(seed) + ": bytes outside the spans differ\nold: " + d.text + "\nnew: " + *r.text);
        // and the independent check agrees
        std::string why;
        if (!verify(d.text, *r.text, c, &why)) PB_FAIL_AT("seed " + std::to_string(seed) + ": verify refused a correct edit: " + why);
    }
    PB_CHECK_EQ(edits, 500);
    PB_CHECK(adds > 100 && replaces > 100 && removes > 100);
}

PB_TEST(a_text_that_changes_a_second_id_is_rejected_by_verify) {
    Gen g(7);
    Doc d;
    do { d = generate(g); } while (d.ids.size() < 2);
    ParseResult p = parse_desktop_config(d.text);
    EntryValue v = {"/mnt/ext1/applications/new.app", "New", "i.bmp", "f.bmp"};
    Change c = add_entry("U_Fresh", v, d.groups[0]);
    EditResult r = edit(*p.config, c);
    PB_CHECK(r.text.has_value());
    // damage a second entry in the new text: remove another id altogether
    EditResult other = edit(*parse_desktop_config(*r.text).config, remove_entry(d.ids[0]));
    PB_CHECK(other.text.has_value());
    PB_CHECK(!verify(d.text, *other.text, c));
}

PB_TEST_MAIN()
