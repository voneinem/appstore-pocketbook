#pragma once
// The one door to a reader's storage. Everything the installer reads or writes on the reader goes through
// this interface, so the engine can be tested against an in-memory volume that behaves like the reader's
// FAT file system, and so the later store app can use the same engine with its own implementation.
//
// Paths are relative to the reader's root, use "/" and contain no empty, "." or ".." parts and no leading
// or trailing "/" (the empty path is the root). An implementation refuses anything else.
//
// Links are never followed: a link-like object anywhere on a path is reported by stat() and refused by every
// operation that would read or write through it.

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace pbstore {
namespace install {

using Path = std::string;

enum class Kind { None, File, Dir, Link, Other };

struct Stat {
    Kind kind = Kind::None;
    uint64_t size = 0;   // files only
    bool exists() const { return kind != Kind::None; }
    bool link_like() const { return kind == Kind::Link; }
};

struct DirEntry {
    std::string name;
    Kind kind = Kind::None;
    uint64_t size = 0;
};

struct Space {
    uint64_t free_bytes = 0;
    uint64_t cluster_bytes = 4096;   // allocation unit; files are rounded up to whole clusters
    bool writable = true;            // this user can create files on the volume
    bool read_only = false;          // the volume is mounted read-only (a cause of writable == false)
};

// Held while one operation works on a reader; released when destroyed.
class Lock {
public:
    virtual ~Lock() = default;
};

class ReaderFs {
public:
    virtual ~ReaderFs() = default;

    virtual Stat stat(const Path& p) = 0;                          // no link following
    virtual std::vector<DirEntry> list(const Path& dir) = 0;       // empty if it is not a directory
    // Reads a regular file; fails if it is missing, not a regular file, or larger than `max_bytes`.
    virtual bool read_file(const Path& p, uint64_t max_bytes, std::string* out) = 0;

    // Creates a file that does not exist yet (fails if the name is taken, also by another letter case).
    // With `flush` the data is on the medium when the call returns.
    virtual bool create_exclusive(const Path& p, const std::string& data, bool flush) = 0;
    // Renames to a name that is free; fails if `to` exists. Never replaces anything.
    virtual bool rename_free(const Path& from, const Path& to) = 0;
    virtual bool remove_file(const Path& p) = 0;
    virtual bool make_dir(const Path& p) = 0;                      // fails if it exists
    virtual bool remove_dir(const Path& p) = 0;                    // fails if it is not empty

    virtual bool flush_dir(const Path& dir) = 0;                   // the directory's entries are on the medium
    virtual bool flush_volume() = 0;                               // everything is on the medium (syncfs)

    virtual Space space() = 0;
    // The exclusive lock for working on this reader; null if another holder has it (or it cannot be made).
    virtual std::unique_ptr<Lock> lock() = 0;
};

// Opens the storage found at a root path (a mount point or a folder chosen by hand).
using ReaderFsFactory = std::function<std::unique_ptr<ReaderFs>(const std::string& root)>;

// True for a path as described at the top: relative, "/"-separated, no empty, "." or ".." part.
bool valid_relative_path(const Path& p);
// "a/b/c" -> "a/b" ; "a" -> "" ; "" -> ""
Path parent_path(const Path& p);
// "a/b/c" -> "c"
std::string base_name(const Path& p);
Path join_path(const Path& dir, const std::string& name);
// Creates the directory and any missing parents; true if it exists as a directory afterwards.
bool ensure_directory(ReaderFs& fs, const Path& dir);

}  // namespace install
}  // namespace pbstore
