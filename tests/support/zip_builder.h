#pragma once
// Test helper: writes ZIP archives with full control over every field, including hostile ones
// (unsafe names, symbolic links, encryption flag, lying sizes, unsupported compression).
// Independent of the core's own writer on purpose.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "miniz.h"

namespace pbtest {

struct ZEntry {
    std::string name;
    std::string data;
    int method = 8;                          // 0 stored, 8 deflate, anything else is written as-is
    uint16_t flags = 0;                      // bit 0: encrypted
    uint32_t external_attr = 0x81A40000u;    // Unix regular file 0644
    uint8_t made_by_host = 3;                // 3 = Unix
    std::optional<uint32_t> declared_usize;  // lie about the uncompressed size
    std::optional<uint32_t> crc_override;
};

inline ZEntry file(const std::string& name, const std::string& data) {
    ZEntry e;
    e.name = name;
    e.data = data;
    return e;
}
inline ZEntry directory(const std::string& name) {
    ZEntry e;
    e.name = name.back() == '/' ? name : name + "/";
    e.method = 0;
    e.external_attr = 0x41ED0000u | 0x10;   // Unix directory 0755 + DOS directory bit
    return e;
}
inline ZEntry symlink(const std::string& name, const std::string& target) {
    ZEntry e = file(name, target);
    e.method = 0;
    e.external_attr = 0xA1FF0000u;   // Unix symbolic link 0777
    return e;
}

namespace detail {
inline void put16(std::string& s, uint16_t v) { s.push_back(char(v & 0xFF)); s.push_back(char(v >> 8)); }
inline void put32(std::string& s, uint32_t v) { put16(s, uint16_t(v & 0xFFFF)); put16(s, uint16_t(v >> 16)); }
inline std::string raw_deflate(const std::string& data) {
    size_t out_len = 0;
    void* p = tdefl_compress_mem_to_heap(data.data(), data.size(), &out_len, 6 | TDEFL_GREEDY_PARSING_FLAG);
    std::string out(static_cast<const char*>(p), out_len);
    mz_free(p);
    return out;
}
}  // namespace detail

inline std::string build_zip(const std::vector<ZEntry>& entries) {
    using namespace detail;
    std::string out, central;
    for (const ZEntry& e : entries) {
        const std::string payload = e.method == 8 ? raw_deflate(e.data) : e.data;
        const uint32_t crc = e.crc_override ? *e.crc_override : static_cast<uint32_t>(mz_crc32(MZ_CRC32_INIT, reinterpret_cast<const unsigned char*>(e.data.data()), e.data.size()));
        const uint32_t usize = e.declared_usize ? *e.declared_usize : static_cast<uint32_t>(e.data.size());
        const uint32_t offset = static_cast<uint32_t>(out.size());
        put32(out, 0x04034b50);
        put16(out, 20);
        put16(out, e.flags);
        put16(out, static_cast<uint16_t>(e.method));
        put16(out, 0);       // time
        put16(out, 0x0021);  // date 1980-01-01
        put32(out, crc);
        put32(out, static_cast<uint32_t>(payload.size()));
        put32(out, usize);
        put16(out, static_cast<uint16_t>(e.name.size()));
        put16(out, 0);
        out += e.name;
        out += payload;
        put32(central, 0x02014b50);
        put16(central, uint16_t((e.made_by_host << 8) | 20));
        put16(central, 20);
        put16(central, e.flags);
        put16(central, static_cast<uint16_t>(e.method));
        put16(central, 0);
        put16(central, 0x0021);
        put32(central, crc);
        put32(central, static_cast<uint32_t>(payload.size()));
        put32(central, usize);
        put16(central, static_cast<uint16_t>(e.name.size()));
        put16(central, 0);   // extra
        put16(central, 0);   // comment
        put16(central, 0);   // disk
        put16(central, 0);   // internal attributes
        put32(central, e.external_attr);
        put32(central, offset);
        central += e.name;
    }
    const uint32_t cd_offset = static_cast<uint32_t>(out.size());
    out += central;
    put32(out, 0x06054b50);
    put16(out, 0);
    put16(out, 0);
    put16(out, static_cast<uint16_t>(entries.size()));
    put16(out, static_cast<uint16_t>(entries.size()));
    put32(out, static_cast<uint32_t>(central.size()));
    put32(out, cd_offset);
    put16(out, 0);
    return out;
}

}  // namespace pbtest
