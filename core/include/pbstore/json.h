#pragma once
// Minimal, defensive JSON reader and writer (no external dependency, no Qt).
// Parsing fails as a whole on any syntax error, duplicate object key, nesting deeper than 32 levels
// or input larger than the caller's limit: callers never see partial results.
// Adapted from the sibling projects' core/src/json.cpp.

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace pbstore {
namespace json {

class Value {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Type type = Type::Null;
    bool boolean = false;
    double number = 0;
    bool is_integer = false;  // the number was written without fraction/exponent
    std::string string;
    std::vector<Value> array;
    std::vector<std::pair<std::string, Value>> object;  // members in document order

    bool is_null() const { return type == Type::Null; }
    bool is_bool() const { return type == Type::Bool; }
    bool is_number() const { return type == Type::Number; }
    bool is_string() const { return type == Type::String; }
    bool is_array() const { return type == Type::Array; }
    bool is_object() const { return type == Type::Object; }

    // Object member lookup; nullptr if this is not an object or the key is absent.
    const Value* find(const std::string& key) const;
    // Typed accessors: nullopt if absent or of the wrong type.
    std::optional<std::string> get_string(const std::string& key) const;
    std::optional<int64_t> get_int(const std::string& key) const;
    std::optional<bool> get_bool(const std::string& key) const;

    // Builders for the writer.
    static Value make_object();
    static Value make_array();
    static Value from_string(std::string s);
    static Value from_int(int64_t n);
    static Value from_bool(bool b);
    // Appends a member (replaces the value of an existing key, keeping its position).
    void set(const std::string& key, Value v);
    // Appends an element to an array.
    void push(Value v);
};

// Parses a complete JSON document (trailing whitespace allowed). On failure returns nullopt and
// sets *error (if given). `max_bytes` bounds the accepted document size.
std::optional<Value> parse(const std::string& text, std::string* error = nullptr,
                           size_t max_bytes = 16u * 1024u * 1024u);

// JSON string literal with quotes and escapes (UTF-8 passes through unchanged).
std::string quote(const std::string& s);

// Deterministic writer: two-space indentation, "\n" line endings, members in the order given,
// arrays of scalars on one line, a trailing newline.
std::string write(const Value& v);

}  // namespace json
}  // namespace pbstore
