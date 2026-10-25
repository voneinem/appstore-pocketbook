#ifdef PBSTORE_TEST_BUILD

#include "test_hooks.h"

#include <cstdlib>
#include <iostream>
#include <sstream>

namespace pbstore {
namespace install {
namespace cli {

namespace {

class EnvMountSource : public MountSource {
public:
    explicit EnvMountSource(const std::string& spec) {
        std::istringstream in(spec);
        std::string item;
        while (std::getline(in, item, ';')) {
            if (item.empty()) continue;
            Mount m;
            const size_t a = item.find('|'), b = item.find('|', a == std::string::npos ? 0 : a + 1);
            m.mount_point = item.substr(0, a);
            if (a != std::string::npos) m.fstype = item.substr(a + 1, b == std::string::npos ? std::string::npos : b - a - 1);
            if (b != std::string::npos) m.size_bytes = std::strtoull(item.c_str() + b + 1, nullptr, 10);
            mounts_.push_back(m);
        }
    }
    std::vector<Mount> mounts() override { return mounts_; }
    std::vector<std::string> extra_roots() override { return {}; }
    std::vector<std::string> usb_manufacturers() override { return list_of("PBSTORE_TEST_USB"); }
    std::vector<std::string> mtp_mounts() override { return list_of("PBSTORE_TEST_MTP"); }

private:
    static std::vector<std::string> list_of(const char* name) {
        std::vector<std::string> out;
        if (const char* v = std::getenv(name)) {
            std::istringstream in(v);
            std::string item;
            while (std::getline(in, item, ';'))
                if (!item.empty()) out.push_back(item);
        }
        return out;
    }
    std::vector<Mount> mounts_;
};

class EnvIdentity : public IdentitySource {
public:
    std::optional<std::string> model(ReaderFs&) override { return get("PBSTORE_TEST_MODEL"); }
    std::optional<std::string> firmware(ReaderFs&) override { return get("PBSTORE_TEST_FIRMWARE"); }

private:
    static std::optional<std::string> get(const char* name) {
        const char* v = std::getenv(name);
        if (!v || !*v) return std::nullopt;
        return std::string(v);
    }
};

// Faults for the tests: cut-off after N changes, the reader vanishing after N changes, a small or read-only volume.
class FaultFs : public ReaderFs {
public:
    FaultFs(std::unique_ptr<ReaderFs> inner, int stop_after, int vanish_after, long long free_bytes, bool read_only)
        : inner_(std::move(inner)), stop_after_(stop_after), vanish_after_(vanish_after), free_bytes_(free_bytes), read_only_(read_only) {}
    Stat stat(const Path& p) override { return gone_ ? Stat() : inner_->stat(p); }
    std::vector<DirEntry> list(const Path& d) override { return gone_ ? std::vector<DirEntry>() : inner_->list(d); }
    bool read_file(const Path& p, uint64_t max, std::string* out) override { return !gone_ && inner_->read_file(p, max, out); }
    bool create_exclusive(const Path& p, const std::string& data, bool flush) override { return tick() && inner_->create_exclusive(p, data, flush); }
    bool rename_free(const Path& a, const Path& b) override { return tick() && inner_->rename_free(a, b); }
    bool remove_file(const Path& p) override { return tick() && inner_->remove_file(p); }
    bool make_dir(const Path& p) override { return tick() && inner_->make_dir(p); }
    bool remove_dir(const Path& p) override { return tick() && inner_->remove_dir(p); }
    bool flush_dir(const Path& d) override { return tick() && inner_->flush_dir(d); }
    bool flush_volume() override { return tick() && inner_->flush_volume(); }
    Space space() override {
        if (gone_) {
            Space s;
            s.writable = false;
            return s;
        }
        Space s = inner_->space();
        if (free_bytes_ >= 0) s.free_bytes = static_cast<uint64_t>(free_bytes_);
        if (read_only_) {
            s.read_only = true;
            s.writable = false;
        }
        return s;
    }
    std::unique_ptr<Lock> lock() override { return gone_ ? nullptr : inner_->lock(); }

private:
    // false when the reader is gone
    bool tick() {
        if (gone_) return false;
        ++calls_;
        if (stop_after_ > 0 && calls_ >= stop_after_) std::_Exit(99);   // as if the cable was pulled: no clean-up
        if (vanish_after_ > 0 && calls_ > vanish_after_) {
            gone_ = true;
            return false;
        }
        return true;
    }
    std::unique_ptr<ReaderFs> inner_;
    int stop_after_, vanish_after_;
    long long free_bytes_;
    bool read_only_;
    int calls_ = 0;
    bool gone_ = false;
};

}  // namespace

MountSource* test_mount_source_or(MountSource* real) {
    const char* spec = std::getenv("PBSTORE_TEST_MOUNTS");
    if (!spec) return real;
    static EnvMountSource env(spec);
    return &env;
}

Downloader apply_test_host_map(Downloader inner) {
    const char* spec = std::getenv("PBSTORE_TEST_HOST_MAP");
    if (!spec) return inner;
    const std::string s = spec;
    const size_t eq = s.find('=');
    if (eq == std::string::npos) return inner;
    const std::string from = "https://" + s.substr(0, eq), to = "https://" + s.substr(eq + 1);
    return [inner, from, to](const std::string& url, uint64_t max, long secs, const DownloadProgress& p) {
        if (url.compare(0, from.size(), from) == 0) return inner(to + url.substr(from.size()), max, secs, p);
        return inner(url, max, secs, p);
    };
}

ReaderFsFactory apply_test_stop_after(ReaderFsFactory inner) {
    auto number = [](const char* name, long long fallback) {
        const char* v = std::getenv(name);
        return v && *v ? std::atoll(v) : fallback;
    };
    const int stop = static_cast<int>(number("PBSTORE_TEST_STOP_AFTER", 0));
    const int vanish = static_cast<int>(number("PBSTORE_TEST_VANISH_AFTER", 0));
    const long long free_bytes = number("PBSTORE_TEST_FREE_BYTES", -1);
    const bool read_only = number("PBSTORE_TEST_READ_ONLY", 0) != 0;
    if (!stop && !vanish && free_bytes < 0 && !read_only) return inner;
    return [inner, stop, vanish, free_bytes, read_only](const std::string& root) -> std::unique_ptr<ReaderFs> {
        std::unique_ptr<ReaderFs> fs = inner(root);
        if (!fs) return fs;
        return std::make_unique<FaultFs>(std::move(fs), stop, vanish, free_bytes, read_only);
    };
}

namespace {
class PrintOpenUrl : public OpenUrl {
public:
    bool open(const std::string& url) override {
        std::cout << "OPENED " << url << std::endl;
        return true;
    }
};
}  // namespace

OpenUrl* test_open_url() {
    if (!std::getenv("PBSTORE_TEST_OPEN_URL")) return nullptr;
    static PrintOpenUrl opener;
    return &opener;
}

namespace {
class StillClock : public Clock {
public:
    Utc now() override {
        Utc u;
        u.year = 2026; u.month = 10; u.day = 25; u.hour = 12;
        return u;
    }
    LocalTime local_now() override {
        LocalTime l;
        l.year = 2026; l.month = 10; l.day = 25; l.hour = 14; l.offset_minutes = 120;
        return l;
    }
};
}  // namespace

Clock* test_clock() {
    if (!std::getenv("PBSTORE_TEST_NOW")) return nullptr;
    static StillClock clock;
    return &clock;
}

IdentitySource* test_identity_source() {
    if (!std::getenv("PBSTORE_TEST_MODEL") && !std::getenv("PBSTORE_TEST_FIRMWARE")) return nullptr;
    static EnvIdentity source;
    return &source;
}

}  // namespace cli
}  // namespace install
}  // namespace pbstore

#endif  // PBSTORE_TEST_BUILD
