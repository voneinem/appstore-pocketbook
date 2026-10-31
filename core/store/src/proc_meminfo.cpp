#include "pbstore/store/proc_meminfo.h"

#include <cctype>
#include <fstream>
#include <sstream>

namespace pbstore {
namespace store {

std::optional<uint64_t> parse_mem_available(const std::string& text) {
    static const std::string kName = "MemAvailable:";
    size_t pos = 0;
    while (pos < text.size()) {
        const size_t eol = text.find('\n', pos);
        const std::string line = text.substr(pos, eol == std::string::npos ? std::string::npos : eol - pos);
        pos = eol == std::string::npos ? text.size() : eol + 1;
        if (line.compare(0, kName.size(), kName) != 0) continue;
        size_t i = kName.size();
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
        if (i >= line.size() || !std::isdigit(static_cast<unsigned char>(line[i]))) return std::nullopt;
        uint64_t kib = 0;
        while (i < line.size() && std::isdigit(static_cast<unsigned char>(line[i]))) {
            const uint64_t digit = static_cast<uint64_t>(line[i] - '0');
            if (kib > (UINT64_MAX / 1024 - digit) / 10) return std::nullopt;   // too large to be bytes
            kib = kib * 10 + digit;
            ++i;
        }
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
        if (line.compare(i, std::string::npos, "kB") != 0) return std::nullopt;
        return kib * 1024;
    }
    return std::nullopt;
}

std::optional<uint64_t> ProcMeminfo::available_bytes() {
    std::ifstream f(path_);
    if (!f) return std::nullopt;
    std::ostringstream ss;
    ss << f.rdbuf();
    return parse_mem_available(ss.str());
}

}  // namespace store
}  // namespace pbstore
