// T059: property test of the planner: whatever the reader and the package look like, a plan never writes
// outside the allowed places, never deletes or replaces what no record owns with an equal hash, never ends
// with two apps on one entry id or data folder, never plans a file that only differs by letter case from one
// it does not own, and is a pure function of its input.
#include "install_fixtures.h"
#include "pbstore/install/planner.h"
#include "test_support.h"

#include <random>

using namespace pbstore;
using namespace pbstore::install;
using namespace pbtest;

namespace {

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
bool starts_with(const std::string& s, const std::string& p) { return s.compare(0, p.size(), p) == 0; }

struct Case {
    FakeReader reader;
    Manifest manifest;
    CrashFs mem;
};

}  // namespace

PB_TEST(plans_keep_the_invariants_over_many_random_readers_and_packages) {
    int planned = 0, refused = 0;
    for (unsigned seed = 1; seed <= 300; ++seed) {
        std::mt19937 rng(seed);
        auto pick = [&](int n) { return static_cast<int>(rng() % static_cast<unsigned>(n)); };

        const PlanKind kinds[] = {PlanKind::Install, PlanKind::Install, PlanKind::Update, PlanKind::Update, PlanKind::Replace, PlanKind::Remove, PlanKind::Adopt};
        const PlanKind kind = kinds[pick(7)];
        const char* ids[] = {"apple", "berry", "cherry"};
        const std::string subject = ids[pick(3)];

        // a reader with up to two installed apps (one of them the subject for update and remove), loose files, case variants
        FakeReader reader;
        std::vector<SampleApp> installed;
        for (int i = 0; i < 3; ++i) {
            const bool is_subject = subject == ids[i];
            const bool wanted = is_subject ? (kind == PlanKind::Update || kind == PlanKind::Remove) : pick(3) == 0;
            if (!wanted) continue;
            SampleApp a = nested_sample_app(ids[i], "1.0.0", std::string("U_") + ids[i] + "App", 3 + static_cast<unsigned>(i));
            if (!is_subject && pick(4) == 0) a.manifest.set("data_dir", pbstore::json::Value::from_string("system/shared"));
            try {
                reader = with_installed(reader, a);
                installed.push_back(a);
            } catch (const std::exception&) {
            }
        }
        if (kind == PlanKind::Replace || kind == PlanKind::Adopt) {
            SampleApp hand = nested_sample_app(subject, "1.0.0", std::string("U_") + subject + "App", 40);
            reader.add_hand_installed(hand);
            if (pick(2) == 0) reader.files["applications/" + subject + ".bin"] = "edited by hand";   // differs from the package
        }
        if (pick(8) == 0) reader.put_file("applications/zeta.app", "loose");
        if (pick(8) == 0) reader.put_file("applications/ZETA.BIN", "loose upper case");
        if (pick(5) == 0) reader.add_data_dir(subject, {{"x.db", "123"}});

        // the package: files from the app, sometimes names in other letter cases, other menu ids / data folders
        SampleApp app = nested_sample_app(subject, "2.0.0", std::string("U_") + subject + "App", pick(2) ? 9 : 40);
        if (pick(4) == 0) app.files.push_back(file("applications/" + subject + "/extra.bin", "extra"));
        app.manifest.set("files", files_value(app.files));
        Manifest m = manifest_of(app);
        if (pick(8) == 0) m.files.push_back({"applications/" + std::string(pick(2) ? "ZETA" : "zeta") + ".app", 3, std::string(64, 'a')});
        if (pick(8) == 0) m.menu_entry.id = "U_" + std::string(ids[pick(3)]) + "App";
        if (pick(8) == 0) m.data_dir = pick(2) ? "system/Shared" : "system/shared";
        if (pick(10) == 0) m.data_dir = "system/config";
        if (pick(10) == 0) m.files[0].path = "system/escape.bin";
        if (pick(10) == 0) m.menu_entry.group = "@Nowhere";

        CrashFs mem;
        reader.load_into(mem);
        FatRulesFs fat(mem);
        ScanRequest req;
        req.app_id = m.id;
        req.manifest = &m;
        ReaderSnapshot snap = scan(fat, Identity{std::string("1030"), std::string("6.11.1437")}, req);
        PlanRequest pr;
        pr.kind = kind;
        pr.app_id = m.id;
        pr.manifest = &m;
        pr.package_sha256 = std::string(64, 'b');
        FixedClock clock;
        pr.now = clock.now();
        pr.local_now = clock.local_now();
        pr.installed_by = "pbstore-install 0.2.0";
        PlanResult r1 = make_plan(pr, snap);
        PlanResult r2 = make_plan(pr, snap);

        // pure: the same input, the same output
        PB_CHECK_EQ(r1.plan.has_value(), r2.plan.has_value());
        if (!r1.plan) {
            PB_CHECK(!r1.findings.empty() || !r1.notices.empty());
            ++refused;
            continue;
        }
        ++planned;
        const Plan& p = *r1.plan;
        PB_CHECK_EQ(render_plan(p), render_plan(*r2.plan));
        PB_CHECK_EQ(p.fingerprint, r2.plan->fingerprint);

        // 1. every planned write lies in applications/ (files, folders); the rest is the single entry and the record
        for (const PlanItem& it : p.items) {
            if (!starts_with(it.path, "applications/") || it.path.find("..") != std::string::npos) PB_FAIL_AT("seed " + std::to_string(seed) + ": a planned item outside applications/: " + it.path);
        }
        for (const std::string& d : p.directories_to_create)
            if (!starts_with(d, "applications/")) PB_FAIL_AT("seed " + std::to_string(seed) + ": a planned folder outside applications/: " + d);
        PB_CHECK(p.menu.id.empty() || p.menu.id == m.menu_entry.id || kind == PlanKind::Remove);

        // 2. a planned deletion or replacement names a path that a record of this app lists with an equal hash
        //    (replace of an app installed outside the installer: only with the explicit confirmation)
        for (const PlanItem& it : p.items) {
            if (it.op != ItemOp::Remove && it.op != ItemOp::Replace) continue;
            const std::string& id = p.app_id;
            auto rec = snap.installed.find(id);
            if (rec == snap.installed.end()) {
                PB_CHECK(kind == PlanKind::Replace);
                PB_CHECK(p.confirmations.count(Confirmation::ReplaceOutside) == 1);
                continue;
            }
            bool recorded = false;
            for (const FileEntry& f : rec->second.record.files)
                if (f.path == it.path) {
                    recorded = true;
                    if (f.sha256 != it.old_sha256) PB_CHECK(p.confirmations.count(Confirmation::EditedFiles) == 1);
                }
            if (!recorded) PB_FAIL_AT("seed " + std::to_string(seed) + ": " + plan_kind_name(kind) + " touches an unrecorded file: " + it.path);
        }

        // 3. two apps never end with one menu entry id or one data folder
        if (kind == PlanKind::Install || kind == PlanKind::Update || kind == PlanKind::Replace) {
            for (const auto& other : snap.installed) {
                if (other.first == p.app_id) continue;
                PB_CHECK(other.second.record.menu_entry.id != m.menu_entry.id);
                if (other.second.record.data_dir && m.data_dir) PB_CHECK(lower(*other.second.record.data_dir) != lower(*m.data_dir));
            }
        }

        // 4. a new file whose path only differs by letter case from an existing entry it does not own is never planned
        std::set<std::string> owned;
        auto self = snap.installed.find(p.app_id);
        if (self != snap.installed.end())
            for (const FileEntry& f : self->second.record.files) owned.insert(f.path);
        for (const PlanItem& it : p.items) {
            if (it.op != ItemOp::Add) continue;
            const std::string dir = parent_path(it.path);
            for (const DirEntry& e : fat.list(dir)) {
                if (lower(e.name) == lower(base_name(it.path)) && !owned.count(join_path(dir, e.name)))
                    PB_FAIL_AT("seed " + std::to_string(seed) + ": plans " + it.path + " next to " + e.name);
            }
        }

        // 5. the data folder is never part of the files
        if (p.data)
            for (const PlanItem& it : p.items)
                if (starts_with(it.path, p.data->path + "/")) PB_FAIL_AT("seed " + std::to_string(seed) + ": a file in the data folder");
    }
    PB_CHECK(planned > 40);
    PB_CHECK(refused > 40);
}

PB_TEST_MAIN()
