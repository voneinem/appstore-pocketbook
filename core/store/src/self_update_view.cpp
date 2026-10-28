#include "pbstore/store/self_update_view.h"

#include "pbstore/semver.h"

namespace pbstore {
namespace store {

SelfUpdateView self_update_view(const SelfUpdateInputs& in) {
    SelfUpdateView v;
    const auto running = parse_semver(in.running_version);
    const auto recorded = parse_semver(in.record_version);
    v.about_text = "Version " + in.running_version + ".";
    if (running && in.last_run_version) {
        const auto last = parse_semver(*in.last_run_version);
        if (last && compare(*last, *running) < 0) {
            v.updated_to = in.running_version;
            v.updated_text = "PB Store was updated to " + in.running_version + ".";
        }
    }
    if (!running || !recorded) return v;
    const int c = compare(*recorded, *running);
    if (c == 0) return v;
    if (c > 0) {
        const bool failed = in.failed_present || in.message.kind == RunMessage::Kind::UpdateFailed;
        if (failed) {
            v.kind = SelfUpdateKind::RolledBack;
            v.failed_version = (in.message.kind == RunMessage::Kind::UpdateFailed && !in.message.detail.empty()) ? in.message.detail : in.record_version;
            v.list_note = "version " + v.failed_version + " failed to start; version " + in.running_version + " is running";
            v.about_text = "Version " + in.running_version + " is running. Version " + v.failed_version + " failed to start and was not kept.";
        } else {
            v.kind = SelfUpdateKind::UpdateStaged;
            v.list_note = "update staged: takes effect when you open PB Store again";
            v.about_text = "Version " + in.running_version + " is running. Version " + in.record_version + " is installed and takes effect when you open PB Store again.";
        }
        return v;
    }
    v.kind = SelfUpdateKind::Ahead;
    v.list_note = "this PB Store (" + in.running_version + ") is newer than its install record (" + in.record_version + ")";
    v.about_text = "Version " + in.running_version + " is running; the install record says " + in.record_version + ".";
    return v;
}

}  // namespace store
}  // namespace pbstore
