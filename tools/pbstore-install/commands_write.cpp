#include "cli.h"
#include "pbstore/install/text_safety.h"
#include "progress_terminal.h"
#include "prompts.h"
#include "signals.h"

namespace pbstore {
namespace install {
namespace cli {

namespace {

struct ConfirmationInfo {
    const char* rule;       // "" if it is not a rule
    const char* option;
    const char* question;
    const char* message;
};

ConfirmationInfo info(Confirmation c) {
    switch (c) {
        case Confirmation::Plan: return {"", "--confirm-plan", "Carry out this plan?", "the plan needs your confirmation"};
        case Confirmation::EditedFiles:
            return {"E-INS-32", "--confirm-edited-files", "Files of this app were changed since the install. Replace or remove them anyway?",
                    "files of this app were changed since the install and would be replaced or removed"};
        case Confirmation::UnknownFirmware:
            return {"E-INS-11", "--confirm-unknown-firmware", "The firmware version of the reader cannot be read. Go on without checking it?",
                    "the firmware version of the reader cannot be read, so it cannot be checked against what the app needs"};
        case Confirmation::UnknownModel:
            return {"E-INS-09", "--confirm-unknown-model", "The model of the reader cannot be read. Go on without checking that it is an InkPad One?",
                    "the model of the reader cannot be read, so it cannot be checked that this is an InkPad One"};
        case Confirmation::ExistingDataFolder:
            return {"E-INS-17", "--confirm-existing-data-folder", "A data folder of this name already exists and no installed app owns it. Use it as it is (its contents stay untouched)?",
                    "the data folder already exists and no installed app owns it; its contents stay untouched"};
        case Confirmation::ReplaceOutside:
            return {"E-INS-38", "--confirm-replace", "This app was installed outside the installer. Replace its files as listed?",
                    "an app installed outside the installer would be replaced"};
        case Confirmation::Repair: return {"E-INS-23", "--confirm-repair", "Carry out the repair?", "an interrupted operation has to be repaired"};
        case Confirmation::DeleteData1: return {"", "--confirm-delete-data-1", "", "the first data-deletion confirmation is missing"};
        case Confirmation::DeleteData2: return {"", "--confirm-delete-data-2", "", "the second data-deletion confirmation is missing"};
    }
    return {"", "", "", ""};
}

Action::Type action_for(PlanKind k) {
    switch (k) {
        case PlanKind::Update: return Action::Type::Update;
        case PlanKind::Remove: return Action::Type::Remove;
        case PlanKind::Adopt: return Action::Type::Adopt;
        case PlanKind::Replace: return Action::Type::Replace;
        default: return Action::Type::Install;
    }
}

void print_success(Context& ctx, const Plan& plan, const ExecResult&) { ctx.out << completion_text(plan); }

// The confirmations the plan needs: the ones given are taken; the others are asked on a terminal, one by one,
// or reported. Returns 0 to go on, otherwise the exit status.
int collect_confirmations(Context& ctx, const Options& opts, const Plan& plan, Confirmations* given) {
    *given = opts.confirmed;
    std::vector<Confirmation> missing;
    for (Confirmation c : plan.confirmations) {
        if (given->count(c)) continue;
        if (ctx.interactive && info(c).question[0] != '\0') {
            if (ask_yes_no(info(c).question)) {
                given->insert(c);
                continue;
            }
            ctx.err << "Not confirmed; nothing was changed.\n";
            return 3;
        }
        missing.push_back(c);
    }
    if (missing.empty()) return 0;
    std::vector<Finding> findings;
    for (Confirmation c : missing) {
        const ConfirmationInfo i = info(c);
        if (i.rule[0] != '\0') findings.push_back(make_error(i.rule, "reader", std::string(i.message) + "; to accept, give " + i.option));
    }
    if (!findings.empty()) print_findings(ctx, findings, ReaderEffect::Unchanged);
    for (Confirmation c : missing) {
        const ConfirmationInfo i = info(c);
        if (i.rule[0] == '\0') {
            ctx.err << "The plan was not carried out: " << i.message << ".\n";
            if (findings.empty()) ctx.err << reader_effect_text(ReaderEffect::Unchanged) << "\n";
            ctx.err << "Next: give " << i.option << " to confirm it (the plan is printed above), or stop here.\n";
        }
    }
    return 2;
}

// The data folder after a completed removal: two confirmations that differ in wording and in what they ask for.
int delete_data_step(Context& ctx, const Options& opts, ReaderHandle& reader, const Plan& removal, const ProgressFn& progress, CancelToken& cancel) {
    auto planned = ctx.engine.plan_delete_data(reader, removal);
    print_notices(ctx, planned.notices);
    if (!planned.ok()) {
        if (planned.findings.empty()) return 0;   // no data folder, or it is already gone
        ctx.err << "The data folder was kept.\n";
        return print_findings(ctx, planned.findings, ReaderEffect::Unchanged);
    }
    PlanHandle& h = *planned.value;
    ctx.out << h.text;
    std::string t1, t2;
    if (opts.delete_token_1) t1 = *opts.delete_token_1;
    if (opts.delete_token_2) t2 = *opts.delete_token_2;
    if (!opts.delete_token_1 && ctx.interactive) t1 = ask_line(delete_data_question_1(h.plan));
    if (!opts.delete_token_2 && ctx.interactive) t2 = ask_line(delete_data_question_2(h.plan));
    if ((!opts.delete_token_1 && !ctx.interactive) || (!opts.delete_token_2 && !ctx.interactive)) {
        ctx.err << "The data folder " << h.plan.data->path << " was kept: ";
        if (!opts.delete_token_1) ctx.err << "give --confirm-delete-data-1=" << delete_data_token_1(h.plan) << " ";
        if (!opts.delete_token_2) ctx.err << "give --confirm-delete-data-2=" << delete_data_token_2(h.plan);
        ctx.err << "\n";
        return 2;
    }
    DeleteResult d = ctx.engine.execute_delete_data(reader, h, t1, t2, progress, cancel);
    finish_terminal_progress();
    if (d.wrong_token_1 || d.wrong_token_2) {
        ctx.err << "The data folder " << h.plan.data->path << " was kept: ";
        if (d.wrong_token_1) ctx.err << "the first confirmation does not match the folder " << delete_data_token_1(h.plan) << ". ";
        if (d.wrong_token_2) ctx.err << "the second confirmation does not match the number of files, " << delete_data_token_2(h.plan) << ".";
        ctx.err << "\n";
        return 2;
    }
    if (!d.done) return print_findings(ctx, d.findings, ReaderEffect::Unchanged);
    ctx.out << "The data folder " << h.plan.data->path << " was deleted (" << d.files_deleted << " files).\n";
    return 0;
}

int run_write(Context& ctx, const Options& opts, PlanKind kind) {
    std::shared_ptr<ReaderHandle> reader;
    int status = 0;
    if (!open_reader_for(ctx, opts, &reader, &status)) return status;
    bool handled = false;
    const int s = handle_interrupted(ctx, opts, *reader, &handled);
    if (handled) return s;

    CancelToken cancel;
    SigintCancel sigint(&cancel);
    ProgressFn progress = terminal_progress();
    Action action;
    action.type = opts.local_file.empty() ? action_for(kind) : Action::Type::InstallLocal;
    action.app_id = opts.app_id;
    action.local_file = opts.local_file;
    action.delete_data = opts.delete_data;
    auto planned = ctx.engine.plan(*reader, action, progress, cancel);
    finish_terminal_progress();
    print_notices(ctx, planned.notices);
    if (!planned.ok()) return print_findings(ctx, planned.findings, planned.effect);
    PlanHandle& handle = *planned.value;
    if (handle.nothing_to_do) {
        ctx.out << "Nothing to do: " << opts.app_id << " is already as offered.\n";
        return 0;
    }
    ctx.out << handle.text;
    if (opts.dry_run) {
        ctx.out << "This was a dry run: nothing was written to the reader.\n";
        return 0;
    }
    Confirmations given;
    const int cs = collect_confirmations(ctx, opts, handle.plan, &given);
    if (cs != 0) return cs;

    ExecResult res = ctx.engine.execute(*reader, handle, given, progress, cancel);
    finish_terminal_progress();
    if (!res.ok()) return print_findings(ctx, res.findings, res.effect);
    print_success(ctx, handle.plan, res);
    if (kind == PlanKind::Remove && opts.delete_data) return delete_data_step(ctx, opts, *reader, handle.plan, progress, cancel);
    return 0;
}

}  // namespace

int cmd_install(Context& ctx, const Options& opts) { return run_write(ctx, opts, PlanKind::Install); }
int cmd_update(Context& ctx, const Options& opts) { return run_write(ctx, opts, PlanKind::Update); }
int cmd_remove(Context& ctx, const Options& opts) { return run_write(ctx, opts, PlanKind::Remove); }
int cmd_adopt(Context& ctx, const Options& opts) { return run_write(ctx, opts, PlanKind::Adopt); }
int cmd_replace(Context& ctx, const Options& opts) { return run_write(ctx, opts, PlanKind::Replace); }

}  // namespace cli
}  // namespace install
}  // namespace pbstore
