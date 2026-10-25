#pragma once
// The facade that every front end uses: the command line, the assistant and, later, the store app. The
// engine never prints and never asks questions; front ends do both. Everything it reads or writes on a reader
// goes through ReaderFs; the operating system is reached through the small interfaces of Deps.

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "pbstore/catalog.h"
#include "pbstore/install/app_state.h"
#include "pbstore/install/clock.h"
#include "pbstore/install/confirmations.h"
#include "pbstore/install/delete_data.h"
#include "pbstore/install/executor.h"
#include "pbstore/install/identity.h"
#include "pbstore/install/local_input.h"
#include "pbstore/install/locator.h"
#include "pbstore/install/log.h"
#include "pbstore/install/mount_source.h"
#include "pbstore/install/net.h"
#include "pbstore/install/package_pipeline.h"
#include "pbstore/install/plan.h"
#include "pbstore/install/planner.h"
#include "pbstore/install/progress.h"
#include "pbstore/install/recovery.h"
#include "pbstore/install/state_dirs.h"
#include "pbstore/install/state_store.h"

namespace pbstore {
namespace install {

struct Config {
    std::string catalog_url;
    std::vector<std::string> trusted_keys;   // raw 32-byte Ed25519 public keys
    std::string program_version;             // "0.2.0"
};

struct Deps {
    ReaderFsFactory open_fs;
    MountSource* mounts = nullptr;
    StateDirs* state = nullptr;
    Downloader download;
    Clock* clock = nullptr;
    Log* log = nullptr;
    Redactor* redactor = nullptr;
    std::vector<IdentitySource*> identity_sources;
    LocalFileReader read_local_file;       // reads a package file chosen by the user
};

// An opened reader. The lock is not held; the executor and the repair take it when they write.
struct ReaderHandle {
    Candidate candidate;
    std::shared_ptr<ReaderFs> fs;
    Identity identity;
};

// What a call returned: a value, or the findings that say why not, plus notices and what the reader's state is.
template <class T>
struct Result {
    std::optional<T> value;
    std::vector<Finding> findings;
    std::vector<Finding> notices;
    ReaderEffect effect = ReaderEffect::Unchanged;
    bool ok() const { return value.has_value(); }
};

struct Listing {
    std::vector<AppEntry> apps;
    bool catalog_loaded = false;
    std::string catalog_generated_at;
    std::optional<RepairPlan> interrupted;   // an operation that was left unfinished
};

struct Action {
    enum class Type { Install, Update, Remove, Adopt, Replace, InstallLocal, DeleteData };
    Type type = Type::Install;
    std::string app_id;       // not for InstallLocal
    std::string local_file;   // InstallLocal: the package on this computer
    bool delete_data = false; // Remove: also delete the data folder afterwards (two more confirmations)
};

struct PlanHandle {
    Plan plan;
    std::shared_ptr<PackageContent> content;
    std::vector<Finding> notices;
    bool nothing_to_do = false;      // W-INS-01: already at the offered version
    bool preview_only = false;       // the assistant's dry run: it cannot be executed
    std::string text;                // render_plan(plan)
};

class Engine {
public:
    Engine(Config config, Deps deps);

    LocateResult readers();
    // The names of the folders directly below `path` on this computer (no files, no names of files). For the
    // assistant's folder list; the caller decides which places may be looked at.
    std::vector<std::string> subfolders(const std::string& path);
    // A candidate chosen by the caller, a folder chosen by hand, or (neither) the single qualifying drive.
    Result<std::shared_ptr<ReaderHandle>> open_reader(const std::optional<std::string>& folder);
    Result<std::shared_ptr<ReaderHandle>> open_candidate(const Candidate& candidate);

    // What is on the reader (works offline).
    Result<Listing> installed(ReaderHandle& reader);
    // The catalog with the state of each app on this reader.
    Result<Listing> list(ReaderHandle& reader, const ProgressFn& progress, CancelToken& cancel);

    // Builds the plan; downloads and verifies the package when one is needed; never writes to the reader.
    Result<PlanHandle> plan(ReaderHandle& reader, const Action& action, const ProgressFn& progress, CancelToken& cancel);
    std::string render(const Plan& plan) const;

    // Carries a plan out after the confirmations it names. Never called for a dry run.
    ExecResult execute(ReaderHandle& reader, PlanHandle& handle, const Confirmations& given, const ProgressFn& progress, CancelToken& cancel);

    // After a completed removal: the plan for deleting the data folder that the removal's plan named.
    Result<PlanHandle> plan_delete_data(ReaderHandle& reader, const Plan& removal_plan);
    // Needs both tokens (the folder as shown, the number of files); a wrong one changes nothing.
    DeleteResult execute_delete_data(ReaderHandle& reader, PlanHandle& handle, const std::string& token1, const std::string& token2, const ProgressFn& progress,
                                     CancelToken& cancel);

    RepairCheck repair_plan(ReaderHandle& reader);
    RepairResult repair(ReaderHandle& reader, const ProgressFn& progress, CancelToken& cancel);

    const Config& config() const { return config_; }
    Log& log() { return *deps_.log; }

private:
    Result<std::shared_ptr<ReaderHandle>> finish_open(const Candidate& candidate, std::vector<Finding> findings);
    bool ensure_catalog(std::vector<Finding>* findings, std::vector<Finding>* notices, const ProgressFn& progress);
    Identity identify_reader(ReaderFs& fs);

    Config config_;
    Deps deps_;
    std::optional<Catalog> catalog_;
};

}  // namespace install
}  // namespace pbstore
