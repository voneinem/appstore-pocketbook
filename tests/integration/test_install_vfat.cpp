// T136 (label "vfat"): the same operations on a real FAT volume through PosixReaderFs: a small image is
// loop-mounted with sudo. Skips (status 77) without mkfs.vfat, sudo that needs no password, or the loop device.
#include "install_rig.h"
#include "test_support.h"

#include <sys/statvfs.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdlib>

using namespace pbstore;
using namespace pbstore::install;
using namespace pbtest;

namespace {

int sh(const std::string& cmd) { return std::system((cmd + " >/dev/null 2>&1").c_str()); }

class LoopMount {
public:
    LoopMount(const TmpDir& dir, int mib) : dir_(dir) {
        if (sh("command -v mkfs.vfat") != 0) { why_ = "mkfs.vfat is not installed"; return; }
        if (sh("sudo -n true") != 0) { why_ = "sudo needs a password"; return; }
        image_ = dir.file("fat.img");
        mnt_ = dir.file("mnt");
        ::mkdir(mnt_.c_str(), 0755);
        if (sh("truncate -s " + std::to_string(mib) + "M " + image_) != 0 || sh("mkfs.vfat -F 16 -n PB1030 " + image_) != 0) { why_ = "no FAT image could be made"; return; }
        const std::string opts = "loop,uid=" + std::to_string(::getuid()) + ",gid=" + std::to_string(::getgid()) + ",umask=022,shortname=mixed";
        if (sh("sudo -n mount -o " + opts + " " + image_ + " " + mnt_) != 0) { why_ = "the loop mount is not possible here"; return; }
        mounted_ = true;
    }
    ~LoopMount() {
        if (mounted_) sh("sudo -n umount " + mnt_);
    }
    bool ok() const { return mounted_; }
    const std::string& why() const { return why_; }
    const std::string& path() const { return mnt_; }

private:
    const TmpDir& dir_;
    std::string image_, mnt_, why_;
    bool mounted_ = false;
};

// throws PowerLoss after N mutating calls: a cut-off on the real volume
class ThrowAfterFs : public ForwardingFs {
public:
    ThrowAfterFs(ReaderFs& base, int limit) : ForwardingFs(base), limit_(limit) {}
    bool create_exclusive(const Path& p, const std::string& d, bool f) override { tick(); return ForwardingFs::create_exclusive(p, d, f); }
    bool rename_free(const Path& a, const Path& b) override { tick(); return ForwardingFs::rename_free(a, b); }
    bool remove_file(const Path& p) override { tick(); return ForwardingFs::remove_file(p); }
    bool make_dir(const Path& p) override { tick(); return ForwardingFs::make_dir(p); }
    bool remove_dir(const Path& p) override { tick(); return ForwardingFs::remove_dir(p); }
    bool flush_dir(const Path& d) override { tick(); return ForwardingFs::flush_dir(d); }
    bool flush_volume() override { tick(); return ForwardingFs::flush_volume(); }

private:
    void tick() {
        if (++calls_ > limit_) throw PowerLoss{};
    }
    int limit_, calls_ = 0;
};

}  // namespace

PB_TEST(the_fat_volume_refuses_to_replace_folds_case_and_reports_its_cluster_size) {
    TmpDir dir;
    LoopMount m(dir, 16);
    if (!m.ok()) PB_SKIP(m.why());
    PosixReaderFs fs(m.path());
    PB_CHECK(fs.make_dir("applications"));
    PB_CHECK(fs.create_exclusive("applications/Readme.TXT", "x", true));
    PB_CHECK(!fs.create_exclusive("applications/readme.txt", "y", true));        // the volume ignores letter case
    PB_CHECK(fs.stat("applications/README.TXT").kind == Kind::File);
    PB_CHECK(fs.create_exclusive("applications/a", "A", true));
    PB_CHECK(!fs.rename_free("applications/a", "applications/Readme.txt"));       // never replaces
    PB_CHECK(!fs.rename_free("applications/a", "applications/readme.TXT"));
    PB_CHECK(fs.rename_free("applications/a", "applications/b"));
    PB_CHECK(fs.flush_dir("applications"));
    PB_CHECK(fs.flush_volume());
    struct statvfs sv;
    PB_CHECK_EQ(::statvfs(m.path().c_str(), &sv), 0);
    PB_CHECK_EQ(fs.space().cluster_bytes, uint64_t(sv.f_bsize));
    PB_CHECK(fs.space().cluster_bytes >= 512);
    PB_CHECK(fs.space().writable);
    // a name with a character FAT does not allow is refused by the volume
    PB_CHECK(!fs.create_exclusive("applications/bad:name", "x", true));
}

PB_TEST(install_update_and_remove_work_on_a_real_fat_volume_and_a_cut_off_is_repaired) {
    TmpDir dir;
    LoopMount m(dir, 16);
    if (!m.ok()) PB_SKIP(m.why());
    FakeReader().write_to(m.path());
    PosixReaderFs fs(m.path());
    {   // install
        Built b = make_planned(fs, nested_sample_app());
        ExecResult r = run_execute(fs, b);
        if (!r.ok()) PB_FAIL_AT("install failed: " + (r.findings.empty() ? std::string("?") : r.findings[0].message));
        PB_CHECK(fs.stat("applications/testapp.app").kind == Kind::File);
        PB_CHECK(!journal_exists(fs));
    }
    {   // update
        Built b = make_planned(fs, nested_sample_app_v2("testapp", "U_TestApp", "1.3.0"), PlanKind::Update);
        PB_CHECK(run_execute(fs, b).ok());
        PB_CHECK(fs.stat("applications/testapp/new.bin").kind == Kind::File);
        PB_CHECK(!fs.stat("applications/testapp/lib/extra").exists());
    }
    {   // remove
        Built b = make_planned(fs, nested_sample_app_v2("testapp", "U_TestApp", "1.3.0"), PlanKind::Remove);
        PB_CHECK(run_execute(fs, b).ok());
        PB_CHECK(!fs.stat("applications/testapp.app").exists());
    }
    // a cut-off at a sample of points, then the repair: the volume holds the old or the new version
    for (int n : {5, 14, 22, 30, 38}) {
        Built b = make_planned(fs, nested_sample_app());
        const Snapshot old_live = without_installer_area(snapshot(fs));
        ThrowAfterFs cut(fs, n);
        try { run_execute(cut, b); } catch (const PowerLoss&) {}
        RepairCheck rc = plan_repair(fs);
        if (rc.plan) PB_CHECK(repair(fs, *rc.plan).done);
        PB_CHECK(!journal_exists(fs));
        const bool installed = fs.stat("applications/testapp.app").exists();
        if (installed) {   // the new version is complete: remove it again for the next round
            Built rm = make_planned(fs, nested_sample_app(), PlanKind::Remove);
            PB_CHECK(run_execute(fs, rm).ok());
        }
        PB_CHECK(compare(old_live, without_installer_area(snapshot(fs))).identical);
    }
}

PB_TEST_MAIN()
