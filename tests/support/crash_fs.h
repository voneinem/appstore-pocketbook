#pragma once
// Test double: an in-memory reader volume that counts every mutating call and can lose power at a chosen
// call. In the pessimistic mode only what was flushed survives: a file whose data was not flushed comes back
// with zero bytes, and a directory entry that no flush_dir/flush_volume covered is reverted. Used by the
// executor, recovery and fault-matrix tests (spec SC-002, SC-005, SC-006).

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "pbstore/install/reader_fs.h"

namespace pbtest {

struct PowerLoss {};

// A plain description of a volume's content.
struct Tree {
    std::map<std::string, std::string> files;   // path -> content
    std::set<std::string> dirs;                 // directory paths
    bool operator==(const Tree& o) const { return files == o.files && dirs == o.dirs; }
};

class CrashFs : public pbstore::install::ReaderFs {
public:
    using Kind = pbstore::install::Kind;
    using Stat = pbstore::install::Stat;
    using DirEntry = pbstore::install::DirEntry;
    using Space = pbstore::install::Space;
    using Path = pbstore::install::Path;

    CrashFs() { reset(); }

    // ---- ReaderFs ------------------------------------------------------------------------------------
    Stat stat(const Path& p) override {
        check_alive();
        Stat s;
        if (gone_ || !pbstore::install::valid_relative_path(p)) return s;
        bool link = false;
        Node* n = walk(live_.get(), p, &link);
        if (link) { s.kind = Kind::Link; return s; }
        if (!n) return s;
        s.kind = n->kind;
        if (n->kind == Kind::File) s.size = n->file->data.size();
        return s;
    }
    std::vector<DirEntry> list(const Path& dir) override {
        check_alive();
        std::vector<DirEntry> out;
        if (gone_ || !pbstore::install::valid_relative_path(dir)) return out;
        Node* n = walk(live_.get(), dir, nullptr);
        if (!n || n->kind != Kind::Dir) return out;
        for (const auto& kv : n->kids) {
            DirEntry e;
            e.name = kv.first;
            e.kind = kv.second->kind;
            if (e.kind == Kind::File) e.size = kv.second->file->data.size();
            out.push_back(e);
        }
        return out;
    }
    bool read_file(const Path& p, uint64_t max_bytes, std::string* out) override {
        check_alive();
        if (gone_ || p.empty() || !pbstore::install::valid_relative_path(p)) return false;
        if (read_failures_.count(p)) return false;
        Node* n = walk(live_.get(), p, nullptr);
        if (!n || n->kind != Kind::File || n->file->data.size() > max_bytes) return false;
        if (out) *out = n->file->data;
        return true;
    }
    bool create_exclusive(const Path& p, const std::string& data, bool flush) override {
        if (!begin_mutation()) return false;
        Node* parent = writable_parent(p);
        if (!parent) return false;
        const std::string name = pbstore::install::base_name(p);
        if (parent->kids.count(name)) return false;
        auto n = std::make_shared<Node>();
        n->kind = Kind::File;
        n->file = std::make_shared<FileObj>();
        n->file->data = data;
        n->file->durable = flush;
        parent->kids[name] = n;
        return true;
    }
    bool rename_free(const Path& from, const Path& to) override {
        if (!begin_mutation()) return false;
        Node* pf = writable_parent(from);
        Node* pt = writable_parent(to);
        if (!pf || !pt) return false;
        const std::string nf = pbstore::install::base_name(from), nt = pbstore::install::base_name(to);
        auto it = pf->kids.find(nf);
        if (it == pf->kids.end() || it->second->kind == Kind::Link) return false;
        if (pt->kids.count(nt)) return false;
        if (it->second->kind == Kind::Dir && (to == from || to.compare(0, from.size() + 1, from + "/") == 0)) return false;
        auto node = it->second;
        pf->kids.erase(it);
        pt->kids[nt] = node;
        return true;
    }
    bool remove_file(const Path& p) override {
        if (!begin_mutation()) return false;
        Node* parent = writable_parent(p);
        if (!parent) return false;
        auto it = parent->kids.find(pbstore::install::base_name(p));
        if (it == parent->kids.end() || it->second->kind == Kind::Dir) return false;
        parent->kids.erase(it);
        return true;
    }
    bool make_dir(const Path& p) override {
        if (!begin_mutation()) return false;
        Node* parent = writable_parent(p);
        if (!parent) return false;
        const std::string name = pbstore::install::base_name(p);
        if (parent->kids.count(name)) return false;
        auto n = std::make_shared<Node>();
        n->kind = Kind::Dir;
        parent->kids[name] = n;
        return true;
    }
    bool remove_dir(const Path& p) override {
        if (!begin_mutation()) return false;
        Node* parent = writable_parent(p);
        if (!parent) return false;
        auto it = parent->kids.find(pbstore::install::base_name(p));
        if (it == parent->kids.end() || it->second->kind != Kind::Dir || !it->second->kids.empty()) return false;
        parent->kids.erase(it);
        return true;
    }
    bool flush_dir(const Path& dir) override {
        if (!begin_mutation()) return false;
        if (!pbstore::install::valid_relative_path(dir)) return false;
        Node* l = walk(live_.get(), dir, nullptr);
        if (!l || l->kind != Kind::Dir) return false;
        l->dkids = l->kids;   // the entries of this directory are now on the medium
        return true;
    }
    bool flush_volume() override {
        if (!begin_mutation()) return false;
        flush_all(live_.get());
        return true;
    }
    Space space() override {
        check_alive();
        if (gone_) { Space s; s.writable = false; return s; }
        return space_;
    }
    std::unique_ptr<pbstore::install::Lock> lock() override {
        check_alive();
        if (gone_ || *lock_held_) return nullptr;
        *lock_held_ = true;
        return std::make_unique<Held>(lock_held_);
    }

    // ---- faults --------------------------------------------------------------------------------------
    // Power is lost at the n-th mutating call counted from now (that call is not applied); n == 0 schedules
    // no loss but sets the mode, for lose_power_now().
    void lose_power_at(int n, bool pessimistic) {
        loss_at_ = n > 0 ? calls_ + n : 0;
        pessimistic_ = pessimistic;
    }
    void lose_power_now() { lost_ = true; }
    bool lost() const { return lost_; }
    // The volume after the power came back: what survived. The object is usable again; the call counter and
    // all scheduled faults start from zero.
    Tree reboot() {
        std::shared_ptr<Node> survivor = survive(live_, pessimistic_);
        live_ = survivor;
        flush_all(live_.get());
        lost_ = false;
        calls_ = 0;
        loss_at_ = 0;
        fail_at_ = 0;
        vanish_at_ = 0;
        lock_held_ = std::make_shared<bool>(false);
        return tree();
    }
    int mutating_calls() const { return calls_; }
    // The n-th mutating call counted from now returns false and changes nothing.
    void fail_mutating_call(int n) { fail_at_ = calls_ + n; }
    void set_read_failure(const Path& p) { read_failures_.insert(p); }
    void set_space(uint64_t free_bytes, uint64_t cluster, bool writable, bool read_only) {
        space_.free_bytes = free_bytes;
        space_.cluster_bytes = cluster;
        space_.writable = writable;
        space_.read_only = read_only;
    }
    // The reader was unplugged: every call fails from now on (until it reappears).
    void vanish() { gone_ = true; }
    void reappear() { gone_ = false; }
    // The reader vanishes at the n-th mutating call counted from now: that call and all later ones fail.
    void vanish_at_call(int n) { vanish_at_ = calls_ + n; }
    // Puts a link-like object at the path (the parent must exist). Not a ReaderFs call: tests only.
    bool plant_link(const Path& p) {
        Node* parent = walk(live_.get(), pbstore::install::parent_path(p), nullptr);
        if (!parent || parent->kind != Kind::Dir) return false;
        auto n = std::make_shared<Node>();
        n->kind = Kind::Link;
        parent->kids[pbstore::install::base_name(p)] = n;
        return true;
    }

    // ---- loading and inspecting (not counted, not faulted) ------------------------------------------------
    void load_tree(const Tree& t) {
        reset();
        for (const std::string& d : t.dirs) put(d, nullptr);
        for (const auto& kv : t.files) put(kv.first, &kv.second);
        flush_all(live_.get());
    }
    Tree tree() const {
        Tree t;
        collect(live_.get(), "", &t);
        return t;
    }

private:
    struct FileObj {
        std::string data;
        bool durable = false;
    };
    struct Node {
        Kind kind = Kind::Dir;
        std::shared_ptr<FileObj> file;
        std::map<std::string, std::shared_ptr<Node>> kids;    // the entries now
        std::map<std::string, std::shared_ptr<Node>> dkids;   // the entries as of the last flush of this directory
    };
    class Held : public pbstore::install::Lock {
    public:
        explicit Held(std::shared_ptr<bool> flag) : flag_(std::move(flag)) {}
        ~Held() override { *flag_ = false; }
    private:
        std::shared_ptr<bool> flag_;
    };

    void reset() {
        live_ = std::make_shared<Node>();
        space_.free_bytes = 1ull << 30;
        lock_held_ = std::make_shared<bool>(false);
    }
    void check_alive() const {
        if (lost_) throw PowerLoss{};
    }
    // returns false when this call is made to fail; throws PowerLoss when power is (now) lost
    bool begin_mutation() {
        check_alive();
        if (gone_) return false;
        ++calls_;
        if (vanish_at_ != 0 && calls_ == vanish_at_) {
            gone_ = true;
            vanish_at_ = 0;
            return false;
        }
        if (loss_at_ != 0 && calls_ == loss_at_) {
            lost_ = true;
            throw PowerLoss{};
        }
        if (fail_at_ != 0 && calls_ == fail_at_) return false;
        return true;
    }
    // The directory that holds the last component of p, for a change; null if p is invalid or a link is on the way.
    Node* writable_parent(const Path& p) {
        if (p.empty() || !pbstore::install::valid_relative_path(p)) return nullptr;
        if (!space_.writable) return nullptr;
        bool link = false;
        Node* parent = walk(live_.get(), pbstore::install::parent_path(p), &link);
        if (!parent || parent->kind != Kind::Dir) return nullptr;
        return parent;
    }
    // Walks a path without following links. A link anywhere on the way sets *link and returns null.
    static Node* walk(Node* root, const Path& p, bool* link) {
        if (link) *link = false;
        Node* n = root;
        size_t start = 0;
        while (start < p.size()) {
            const size_t slash = p.find('/', start);
            const std::string part = p.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
            if (n->kind == Kind::Link) { if (link) *link = true; return nullptr; }
            if (n->kind != Kind::Dir) return nullptr;
            auto it = n->kids.find(part);
            if (it == n->kids.end()) return nullptr;
            n = it->second.get();
            if (slash == std::string::npos) break;
            start = slash + 1;
        }
        if (n->kind == Kind::Link && !p.empty()) {
            if (link) *link = true;
            return nullptr;
        }
        return n;
    }
    void put(const std::string& path, const std::string* data) {
        Node* n = live_.get();
        size_t start = 0;
        while (start <= path.size()) {
            const size_t slash = path.find('/', start);
            const std::string part = path.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
            const bool last = slash == std::string::npos;
            auto it = n->kids.find(part);
            if (it == n->kids.end()) {
                auto k = std::make_shared<Node>();
                if (last && data) {
                    k->kind = Kind::File;
                    k->file = std::make_shared<FileObj>();
                    k->file->data = *data;
                }
                it = n->kids.emplace(part, k).first;
            }
            n = it->second.get();
            if (last) break;
            start = slash + 1;
        }
    }
    // What a volume looks like after the power came back. Clean: as it was. Pessimistic: only what was flushed.
    static std::shared_ptr<Node> survive(const std::shared_ptr<Node>& src, bool pessimistic) {
        auto n = std::make_shared<Node>();
        n->kind = src->kind;
        if (src->kind == Kind::File) {
            n->file = std::make_shared<FileObj>();
            n->file->data = (!pessimistic || src->file->durable) ? src->file->data : std::string();
            return n;
        }
        const auto& entries = pessimistic ? src->dkids : src->kids;
        for (const auto& kv : entries) n->kids[kv.first] = survive(kv.second, pessimistic);
        return n;
    }
    // Everything below `n` is now on the medium.
    static void flush_all(Node* n) {
        if (n->file) n->file->durable = true;
        n->dkids = n->kids;
        for (auto& kv : n->kids) flush_all(kv.second.get());
    }
    static void collect(const Node* n, const std::string& path, Tree* t) {
        for (const auto& kv : n->kids) {
            const std::string p = path.empty() ? kv.first : path + "/" + kv.first;
            if (kv.second->kind == Kind::Dir) {
                t->dirs.insert(p);
                collect(kv.second.get(), p, t);
            } else if (kv.second->kind == Kind::File) {
                t->files[p] = kv.second->file->data;
            }
        }
    }

    std::shared_ptr<Node> live_;
    Space space_;
    std::shared_ptr<bool> lock_held_;
    std::set<std::string> read_failures_;
    int calls_ = 0, loss_at_ = 0, fail_at_ = 0, vanish_at_ = 0;
    bool lost_ = false, pessimistic_ = false, gone_ = false;
};

}  // namespace pbtest
