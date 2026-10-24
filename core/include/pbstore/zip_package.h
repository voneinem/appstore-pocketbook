#pragma once
// Safe reading of untrusted ZIP packages and deterministic writing (docs/contracts/package-manifest.md).
// The reader never extracts to disk and never trusts declared sizes: reads and digests are capped.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "pbstore/rules.h"

namespace pbstore {

struct ZipLimits {
    uint64_t max_compressed_bytes = 64ull * 1024 * 1024;     // the whole package
    uint64_t max_uncompressed_bytes = 200ull * 1024 * 1024;  // all entries together, and each entry
    size_t max_entries = 1000;
    uint64_t ratio_guard_min_bytes = 1024 * 1024;            // entries of at least this size ...
    uint64_t max_ratio = 200;                                // ... may not expand by more than this factor
};

struct ZipEntry {
    std::string name;            // as stored; directories end with "/"
    bool is_directory = false;
    uint64_t size = 0;           // declared uncompressed size
    uint64_t compressed_size = 0;
    size_t index = 0;
};

class ZipArchive {
public:
    ZipArchive();
    ~ZipArchive();
    ZipArchive(const ZipArchive&) = delete;
    ZipArchive& operator=(const ZipArchive&) = delete;

    // Opens an archive held in memory. Returns the findings (E-PKG-01, E-PKG-02, E-PKG-03); the
    // archive is usable only if the list is empty.
    std::vector<Finding> open(const std::string& bytes, const ZipLimits& limits = ZipLimits());
    const std::vector<ZipEntry>& entries() const;

    // Reads one file entry. Fails if it is larger than `max_bytes` or than its declared size, or
    // if the data is corrupt (CRC, truncated stream).
    bool read(const ZipEntry& entry, uint64_t max_bytes, std::string* out, std::string* error) const;
    // Streams one file entry through SHA-256, counting the real size. Same failure rules.
    bool digest(const ZipEntry& entry, std::string* sha256_hex, uint64_t* actual_size, std::string* error) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

struct ZipFile {
    std::string name;
    std::string data;
};

// Deterministic archive: entries sorted by name, fixed timestamps, no directory entries, Unix mode
// 0644, deflate only where it makes the entry smaller. The same input gives the same bytes.
std::string write_zip(std::vector<ZipFile> files);

}  // namespace pbstore
