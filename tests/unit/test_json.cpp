// T011: JSON reader and writer (strict, defensive). Written first; fails until core/src/json.cpp exists.
#include "pbstore/json.h"
#include "test_support.h"

using namespace pbstore;

PB_TEST(parses_valid_documents) {
    std::string err;
    auto v = json::parse(R"({"a": 1, "b": [true, null, "x"], "c": {"d": 2.5}})", &err);
    PB_CHECK(v.has_value());
    PB_CHECK(v->is_object());
    PB_CHECK_EQ(v->get_int("a").value_or(-1), int64_t(1));
    const json::Value* b = v->find("b");
    PB_CHECK(b && b->is_array() && b->array.size() == 3);
    PB_CHECK(b->array[0].is_bool() && b->array[0].boolean);
    PB_CHECK(b->array[1].is_null());
    PB_CHECK_EQ(b->array[2].string, std::string("x"));
    const json::Value* c = v->find("c");
    PB_CHECK(c && c->find("d") && !c->find("d")->is_integer);
}

PB_TEST(syntax_errors_fail_the_whole_parse) {
    for (const char* bad : {"", "{", "{\"a\":}", "[1,]", "{\"a\":1,}", "[1 2]", "tru", "{\"a\" 1}", "\"abc",
                            "{} x", "01", "1.", "-", "[\"a\\x\"]", "{'a':1}", "\"\\ud800\""}) {
        std::string err;
        PB_CHECK(!json::parse(bad, &err).has_value());
        PB_CHECK(!err.empty());
    }
}

PB_TEST(duplicate_object_keys_are_rejected) {
    std::string err;
    PB_CHECK(!json::parse(R"({"a": 1, "a": 2})", &err).has_value());
    PB_CHECK(err.find("duplicate") != std::string::npos);
    // duplicates are rejected at any depth, but the same key in different objects is fine
    PB_CHECK(!json::parse(R"({"x": {"k": 1, "k": 2}})", &err).has_value());
    PB_CHECK(json::parse(R"({"x": {"k": 1}, "y": {"k": 2}})", &err).has_value());
}

PB_TEST(nesting_deeper_than_32_is_rejected) {
    std::string ok(32, '['), bad(33, '[');
    ok += std::string(32, ']');
    bad += std::string(33, ']');
    std::string err;
    PB_CHECK(json::parse(ok, &err).has_value());
    PB_CHECK(!json::parse(bad, &err).has_value());
    PB_CHECK(err.find("deep") != std::string::npos);
}

PB_TEST(size_limit_is_enforced) {
    std::string doc = "[" + std::string(100, '1') + "]";   // 102 bytes
    std::string err;
    PB_CHECK(json::parse(doc, &err, 1000).has_value());
    PB_CHECK(!json::parse(doc, &err, 50).has_value());
    PB_CHECK(err.find("large") != std::string::npos);
}

PB_TEST(strings_round_trip_with_escapes_and_utf8) {
    std::string err;
    auto v = json::parse("\"a\\n\\t\\\"\\\\ \\u00e4 \\ud83d\\ude00 \xc3\xb6\"", &err);
    PB_CHECK(v.has_value());
    PB_CHECK_EQ(v->string, std::string("a\n\t\"\\ \xc3\xa4 \xf0\x9f\x98\x80 \xc3\xb6"));
    PB_CHECK_EQ(json::quote(std::string("a\"b\\c\n\x01")), std::string("\"a\\\"b\\\\c\\n\\u0001\""));
    PB_CHECK_EQ(json::quote("\xc3\xa4"), std::string("\"\xc3\xa4\""));
}

PB_TEST(writer_is_deterministic_two_space_indented_in_given_order) {
    json::Value root = json::Value::make_object();
    root.set("zeta", json::Value::from_string("last\n"));
    root.set("alpha", json::Value::from_int(7));
    json::Value list = json::Value::make_array();
    list.push(json::Value::from_string("1.0.0"));
    list.push(json::Value::from_string("1.1.0"));
    root.set("list", list);
    root.set("empty_list", json::Value::make_array());
    root.set("empty_obj", json::Value::make_object());
    json::Value nested = json::Value::make_object();
    nested.set("flag", json::Value::from_bool(true));
    nested.set("none", json::Value());
    json::Value objs = json::Value::make_array();
    objs.push(nested);
    root.set("objs", objs);
    const std::string expected =
        "{\n"
        "  \"zeta\": \"last\\n\",\n"
        "  \"alpha\": 7,\n"
        "  \"list\": [\"1.0.0\", \"1.1.0\"],\n"
        "  \"empty_list\": [],\n"
        "  \"empty_obj\": {},\n"
        "  \"objs\": [\n"
        "    {\n"
        "      \"flag\": true,\n"
        "      \"none\": null\n"
        "    }\n"
        "  ]\n"
        "}\n";
    PB_CHECK_EQ(json::write(root), expected);
    PB_CHECK_EQ(json::write(root), expected);  // same bytes every time
    // what is written can be read back to the same bytes
    auto back = json::parse(expected);
    PB_CHECK(back.has_value());
    PB_CHECK_EQ(json::write(*back), expected);
}

// ---- byte spans (feature 002, T009): the desktop configuration editor splices text by span ---------------
namespace {
std::string cut(const std::string& text, const json::Value::Span& s) { return text.substr(s.begin, s.end - s.begin); }
}  // namespace

PB_TEST(spans_give_the_exact_text_of_every_value) {
    const std::string text =
        "  {\"a\": 1.0, \"b\": [1e3, 12345678901234567890, \"x\\n\\u00e9\", true, null],\n"
        "\t\"c\" : { \"d\" : \"v\" }, \"e\": {}, \"f\": [ ] }  \n";
    std::string err;
    auto v = json::parse(text, &err, 1 << 20, /*spans=*/true);
    PB_CHECK(v.has_value());
    PB_CHECK_EQ(cut(text, v->span), text.substr(2, text.rfind('}') - 2 + 1));
    PB_CHECK_EQ(cut(text, v->find("a")->span), std::string("1.0"));
    const json::Value* b = v->find("b");
    PB_CHECK_EQ(cut(text, b->span), std::string("[1e3, 12345678901234567890, \"x\\n\\u00e9\", true, null]"));
    PB_CHECK_EQ(cut(text, b->array[0].span), std::string("1e3"));
    PB_CHECK_EQ(cut(text, b->array[1].span), std::string("12345678901234567890"));
    PB_CHECK_EQ(cut(text, b->array[2].span), std::string("\"x\\n\\u00e9\""));
    PB_CHECK_EQ(cut(text, b->array[3].span), std::string("true"));
    PB_CHECK_EQ(cut(text, b->array[4].span), std::string("null"));
    PB_CHECK_EQ(cut(text, v->find("c")->span), std::string("{ \"d\" : \"v\" }"));
    PB_CHECK_EQ(cut(text, v->find("c")->find("d")->span), std::string("\"v\""));
    PB_CHECK_EQ(cut(text, v->find("e")->span), std::string("{}"));
    PB_CHECK_EQ(cut(text, v->find("f")->span), std::string("[ ]"));
}

PB_TEST(object_members_record_where_the_key_starts) {
    const std::string text = "{ \"first\": 1,\n  \"second\" : [2], \"t\":\"x\" }";
    auto v = json::parse(text, nullptr, 1 << 20, true);
    PB_CHECK(v.has_value());
    PB_CHECK_EQ(v->key_begin.size(), v->object.size());
    for (size_t i = 0; i < v->object.size(); ++i) {
        PB_CHECK_EQ(text[v->key_begin[i]], '"');
        PB_CHECK(text.compare(v->key_begin[i], v->object[i].first.size() + 2, "\"" + v->object[i].first + "\"") == 0);
        PB_CHECK(v->key_begin[i] < v->object[i].second.span.begin);
    }
}

PB_TEST(spans_do_not_change_what_is_accepted_or_rejected) {
    for (const char* ok : {"{}", "[]", "[1,2]", " {\"a\":{\"b\":[null]}} ", "\"s\"", "12"})
        PB_CHECK_EQ(json::parse(ok, nullptr, 1 << 20, false).has_value(), json::parse(ok, nullptr, 1 << 20, true).has_value());
    for (const char* bad : {"{\"a\":1,\"a\":2}", "{\"a\":}", "[1,]", "{\"a\" 1}", "[1", ""}) {
        PB_CHECK(!json::parse(bad, nullptr, 1 << 20, false).has_value());
        PB_CHECK(!json::parse(bad, nullptr, 1 << 20, true).has_value());
    }
    std::string deep(40, '[');
    deep += std::string(40, ']');
    PB_CHECK(!json::parse(deep, nullptr, 1 << 20, true).has_value());
    PB_CHECK(!json::parse("[1,2,3]", nullptr, 3, true).has_value());   // the size cap is the same
}

PB_TEST(without_spans_the_values_carry_none) {
    auto v = json::parse("{\"a\": [1]}", nullptr, 1 << 20, false);
    PB_CHECK(v.has_value());
    PB_CHECK_EQ(v->span.begin, size_t(0));
    PB_CHECK_EQ(v->span.end, size_t(0));
    PB_CHECK(v->key_begin.empty());
    auto def = json::parse("{\"a\": [1]}");   // the old call still works
    PB_CHECK(def.has_value() && def->key_begin.empty());
}

PB_TEST_MAIN()
