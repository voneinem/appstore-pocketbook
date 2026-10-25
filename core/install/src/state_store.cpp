#include "pbstore/install/state_store.h"

#include "pbstore/json.h"

namespace pbstore {
namespace install {

std::optional<int64_t> StateStore::last_serial() const {
    std::string text;
    if (!dirs_.read("state.json", &text)) return std::nullopt;
    auto v = json::parse(text, nullptr, 4096);
    if (!v || !v->is_object()) return std::nullopt;
    const auto version = v->get_string("schema_version");
    if (!version || version->compare(0, 2, "1.") != 0) return std::nullopt;
    const auto serial = v->get_int("last_serial");
    if (!serial || *serial < 1) return std::nullopt;
    return serial;
}

bool StateStore::record(int64_t serial) {
    if (serial < 1) return false;
    const auto current = last_serial();
    if (current && serial <= *current) return true;
    return dirs_.write_atomic("state.json", "{\"schema_version\":\"1.0\",\"last_serial\":" + std::to_string(serial) + "}\n");
}

}  // namespace install
}  // namespace pbstore
