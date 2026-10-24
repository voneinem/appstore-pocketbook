#include "pbstore/zip_package.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <stdexcept>

#include "miniz.h"
#include "pbstore/sha256.h"
#include "pbstore/validate.h"

namespace pbstore {

struct ZipArchive::Impl {
    std::string bytes;
    mz_zip_archive zip;
    bool open = false;
    std::vector<ZipEntry> entries;
    Impl() { std::memset(&zip, 0, sizeof zip); }
    ~Impl() { close(); }
    void close() {
        if (open) mz_zip_reader_end(&zip);
        open = false;
        std::memset(&zip, 0, sizeof zip);
        entries.clear();
    }
};

ZipArchive::ZipArchive() : impl_(new Impl) {}
ZipArchive::~ZipArchive() = default;

const std::vector<ZipEntry>& ZipArchive::entries() const { return impl_->entries; }

namespace {

std::string lower_ascii(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

std::string shown_name(const std::string& n) {
    std::string t = n.size() > 80 ? n.substr(0, 77) + "..." : n;
    for (char& c : t)
        if (static_cast<unsigned char>(c) < 0x20 || static_cast<unsigned char>(c) >= 0x7F) c = '?';
    return t;
}

std::string mib(uint64_t v) { return std::to_string(v / (1024 * 1024)) + " MiB"; }

}  // namespace

std::vector<Finding> ZipArchive::open(const std::string& bytes, const ZipLimits& limits) {
    impl_->close();
    std::vector<Finding> out;
    const size_t kMaxFindings = 20;
    auto add = [&](const char* id, const std::string& subject, const std::string& message) {
        if (out.size() < kMaxFindings) out.push_back(make_error(id, subject, message));
    };
    if (bytes.size() > limits.max_compressed_bytes) {
        add("E-PKG-02", "package", "package is " + std::to_string(bytes.size()) + " bytes, the limit is " +
                                   std::to_string(limits.max_compressed_bytes) + " bytes (" + mib(limits.max_compressed_bytes) + ")");
        return out;
    }
    impl_->bytes = bytes;
    if (!mz_zip_reader_init_mem(&impl_->zip, impl_->bytes.data(), impl_->bytes.size(), 0)) {
        add("E-PKG-01", "package", std::string("not a readable ZIP archive (") +
                                   mz_zip_get_error_string(mz_zip_get_last_error(&impl_->zip)) + ")");
        std::memset(&impl_->zip, 0, sizeof impl_->zip);
        return out;
    }
    impl_->open = true;
    const mz_uint count = mz_zip_reader_get_num_files(&impl_->zip);
    if (count > limits.max_entries) {
        add("E-PKG-02", "package", "package has " + std::to_string(count) + " entries, the limit is " + std::to_string(limits.max_entries));
        impl_->close();
        return out;
    }
    std::map<std::string, std::string> seen;   // lower-case name -> original name
    uint64_t total = 0;
    std::vector<ZipEntry> entries;
    for (mz_uint i = 0; i < count; ++i) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&impl_->zip, i, &st)) {
            add("E-PKG-01", "package", "entry " + std::to_string(i) + " cannot be read");
            continue;
        }
        const mz_uint name_len = mz_zip_reader_get_filename(&impl_->zip, i, nullptr, 0);   // includes the NUL
        std::string name(name_len > 0 ? name_len - 1 : 0, '\0');
        if (name_len > 1) mz_zip_reader_get_filename(&impl_->zip, i, &name[0], name_len);
        ZipEntry e;
        e.name = name;
        e.is_directory = !name.empty() && name.back() == '/';
        e.size = st.m_uncomp_size;
        e.compressed_size = st.m_comp_size;
        e.index = i;
        const std::string label = shown_name(name);
        // names
        std::string path = e.is_directory ? name.substr(0, name.size() - 1) : name;
        if (!validate::is_safe_path(path)) {
            add("E-PKG-03", label, "unsafe file name " + std::string("\"") + label +
                                   "\" (allowed: relative paths of A-Z a-z 0-9 . _ - and spaces, no \"..\", no hidden names, at most 200 characters)");
        } else {
            auto ins = seen.emplace(lower_ascii(path), path);
            if (!ins.second)
                add("E-PKG-03", label, "name collides with \"" + shown_name(ins.first->second) + "\" (names must differ by more than letter case)");
        }
        // symbolic links
        if ((st.m_version_made_by >> 8) == 3 && ((st.m_external_attr >> 16) & 0xF000) == 0xA000)
            add("E-PKG-03", label, "symbolic links are not allowed");
        // encryption and compression
        if (st.m_is_encrypted || (st.m_bit_flag & 1))
            add("E-PKG-01", label, "entry is encrypted");
        else if (st.m_method != 0 && st.m_method != MZ_DEFLATED)
            add("E-PKG-01", label, "entry uses unsupported compression method " + std::to_string(st.m_method) + " (only stored and deflate are allowed)");
        // sizes
        if (e.size > limits.max_uncompressed_bytes)
            add("E-PKG-02", label, "entry expands to " + std::to_string(e.size) + " bytes, the limit is " + std::to_string(limits.max_uncompressed_bytes) + " bytes");
        total += e.size;
        if (!e.is_directory && e.size >= limits.ratio_guard_min_bytes &&
            (e.compressed_size == 0 || e.size / e.compressed_size > limits.max_ratio))
            add("E-PKG-02", label, "compression ratio of " + (e.compressed_size ? std::to_string(e.size / e.compressed_size) : std::string("infinity")) +
                                   " exceeds the limit of " + std::to_string(limits.max_ratio) + " for entries of " + mib(limits.ratio_guard_min_bytes) + " or more");
        entries.push_back(std::move(e));
    }
    if (total > limits.max_uncompressed_bytes)
        add("E-PKG-02", "package", "entries expand to " + std::to_string(total) + " bytes together, the limit is " +
                                   std::to_string(limits.max_uncompressed_bytes) + " bytes (" + mib(limits.max_uncompressed_bytes) + ")");
    if (!out.empty()) {
        impl_->close();
        return out;
    }
    impl_->entries = std::move(entries);
    return out;
}

namespace {

struct Sink {
    std::string* out = nullptr;     // collects data (read) or null (digest)
    Sha256* hash = nullptr;
    uint64_t cap = 0;               // the most bytes accepted
    uint64_t total = 0;
    bool overflow = false;
};

size_t sink_write(void* opaque, mz_uint64 /*offset*/, const void* data, size_t len) {
    Sink* s = static_cast<Sink*>(opaque);
    if (s->total + len > s->cap) {
        s->overflow = true;
        return 0;   // makes miniz stop
    }
    if (s->out) s->out->append(static_cast<const char*>(data), len);
    if (s->hash) s->hash->update(data, len);
    s->total += len;
    return len;
}

}  // namespace

bool ZipArchive::read(const ZipEntry& entry, uint64_t max_bytes, std::string* out, std::string* error) const {
    out->clear();
    if (!impl_->open || entry.is_directory) { if (error) *error = "not a readable file entry"; return false; }
    if (entry.size > max_bytes) { if (error) *error = "entry is larger than the allowed " + std::to_string(max_bytes) + " bytes"; return false; }
    Sink sink;
    sink.out = out;
    sink.cap = std::min<uint64_t>(max_bytes, entry.size);
    const bool ok = mz_zip_reader_extract_to_callback(&impl_->zip, static_cast<mz_uint>(entry.index), sink_write, &sink, 0) &&
                    sink.total == entry.size;
    if (!ok) {
        out->clear();
        if (error) *error = sink.overflow ? "entry expands beyond its declared size" : "entry data is corrupt (size or CRC does not match)";
    }
    return ok;
}

bool ZipArchive::digest(const ZipEntry& entry, std::string* sha256_hex, uint64_t* actual_size, std::string* error) const {
    if (!impl_->open || entry.is_directory) { if (error) *error = "not a readable file entry"; return false; }
    Sha256 hash;
    Sink sink;
    sink.hash = &hash;
    sink.cap = entry.size;
    const bool ok = mz_zip_reader_extract_to_callback(&impl_->zip, static_cast<mz_uint>(entry.index), sink_write, &sink, 0) &&
                    sink.total == entry.size;
    if (!ok) {
        if (error) *error = sink.overflow ? "entry expands beyond its declared size" : "entry data is corrupt (size or CRC does not match)";
        return false;
    }
    *sha256_hex = hash.finish_hex();
    *actual_size = sink.total;
    return true;
}

namespace {
void put16(std::string& s, uint16_t v) { s.push_back(static_cast<char>(v & 0xFF)); s.push_back(static_cast<char>(v >> 8)); }
void put32(std::string& s, uint32_t v) { put16(s, static_cast<uint16_t>(v & 0xFFFF)); put16(s, static_cast<uint16_t>(v >> 16)); }
}  // namespace

std::string write_zip(std::vector<ZipFile> files) {
    std::sort(files.begin(), files.end(), [](const ZipFile& a, const ZipFile& b) { return a.name < b.name; });
    std::string out, central;
    for (const ZipFile& f : files) {
        if (f.data.size() > 0xFFFFFFFFu || f.name.size() > 0xFFFF) throw std::runtime_error("file too large for the package");
        std::string payload;
        uint16_t method = 0;
        if (!f.data.empty()) {
            size_t n = 0;
            void* p = tdefl_compress_mem_to_heap(f.data.data(), f.data.size(), &n, 128 /* probes */);
            if (p) {
                if (n < f.data.size()) { payload.assign(static_cast<const char*>(p), n); method = MZ_DEFLATED; }
                mz_free(p);
            }
        }
        if (method == 0) payload = f.data;
        const uint32_t crc = static_cast<uint32_t>(mz_crc32(MZ_CRC32_INIT, reinterpret_cast<const unsigned char*>(f.data.data()), f.data.size()));
        const uint32_t offset = static_cast<uint32_t>(out.size());
        put32(out, 0x04034b50); put16(out, 20); put16(out, 0); put16(out, method);
        put16(out, 0); put16(out, 0x0021);                       // 1980-01-01 00:00
        put32(out, crc); put32(out, static_cast<uint32_t>(payload.size())); put32(out, static_cast<uint32_t>(f.data.size()));
        put16(out, static_cast<uint16_t>(f.name.size())); put16(out, 0);
        out += f.name;
        out += payload;
        put32(central, 0x02014b50); put16(central, (3 << 8) | 20); put16(central, 20); put16(central, 0); put16(central, method);
        put16(central, 0); put16(central, 0x0021);
        put32(central, crc); put32(central, static_cast<uint32_t>(payload.size())); put32(central, static_cast<uint32_t>(f.data.size()));
        put16(central, static_cast<uint16_t>(f.name.size())); put16(central, 0); put16(central, 0); put16(central, 0); put16(central, 0);
        put32(central, 0x81A40000u);                             // Unix regular file, mode 0644
        put32(central, offset);
        central += f.name;
    }
    const uint32_t cd_offset = static_cast<uint32_t>(out.size());
    out += central;
    put32(out, 0x06054b50); put16(out, 0); put16(out, 0);
    put16(out, static_cast<uint16_t>(files.size())); put16(out, static_cast<uint16_t>(files.size()));
    put32(out, static_cast<uint32_t>(central.size())); put32(out, cd_offset); put16(out, 0);
    return out;
}

}  // namespace pbstore
