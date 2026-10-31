#include "pbstore/install/install_rules.h"

#include <algorithm>

namespace pbstore {
namespace install {

namespace {
constexpr ReaderEffect U = ReaderEffect::Unchanged;
constexpr ReaderEffect R = ReaderEffect::Restored;
constexpr ReaderEffect N = ReaderEffect::RepairNeeded;
}  // namespace

const std::vector<InstallRule>& install_rule_registry() {
    static const std::vector<InstallRule> rules = {
        {"E-INS-01", "No reader was found", U,
         "Connect the reader with the USB cable, set USB storage mode on its screen, wait a few seconds and try again, or name the reader's folder yourself.", 2},
        {"E-INS-02", "Several drives could be the reader", U, "Name the reader's folder with --reader (or choose the drive in the assistant).", 2},
        {"E-INS-03", "The folder is not a reader", U, "Choose the top folder of the reader; it holds the folders applications/ and system/.", 2},
        {"E-INS-04", "A PocketBook seems to be connected but shows no storage", U,
         "Set USB storage mode on the reader's screen (not MTP, not charge only), wait a few seconds and try again.", 2},
        {"E-INS-05", "The storage is mounted read-only", U, "Mount it for reading and writing again, or check the write-protect setting, and try again.", 2},
        {"E-INS-06", "This user cannot write to the drive", U,
         "Check who owns the drive and with which options it is mounted; the installer never asks for elevated rights.", 2},
        {"E-INS-07", "There is not enough free space on the reader", U, "Free some space on the reader and try again.", 2},
        {"E-INS-08", "The reader is not an InkPad One", U, "Only the InkPad One is supported.", 1},
        {"E-INS-09", "The model of the reader cannot be read", U, "Accept the risk with --confirm-unknown-model, or stop here.", 2},
        {"E-INS-10", "The firmware of the reader is older than the app needs", U, "Update the firmware of the reader first.", 1},
        {"E-INS-11", "The firmware version of the reader cannot be read", U, "Accept the risk with --confirm-unknown-firmware, or stop here.", 2},
        {"E-INS-12", "The desktop configuration is not valid JSON", U,
         "Restore the file system/config/desktop/view.json from a backup, or repair it by hand; nothing was written.", 1},
        {"E-INS-13", "The desktop configuration lacks expected parts", U,
         "Restore the file system/config/desktop/view.json from a backup, or repair it by hand; nothing was written.", 1},
        {"E-INS-14", "The menu group of the app does not exist on the reader", U,
         "Nothing was written and no group is created; create the group on the reader first.", 1},
        {"E-INS-15", "The menu entry is already used by another app", U, "Remove the other app first, or choose another app.", 1},
        {"E-INS-16", "The data folder is claimed by another installed app", U, "Remove the other app first, or choose another app.", 1},
        {"E-INS-17", "The data folder exists and no installed app owns it", U,
         "Its contents stay untouched; accept with --confirm-existing-data-folder, or stop here.", 2},
        {"E-INS-18", "The data folder name is reserved", U, "Choose another package; the data folder of an app may not be a system name.", 1},
        {"E-INS-19", "A planned path is outside the places the installer may write", U, "Nothing was written; report this to the author of the app.", 1},
        {"E-INS-20", "A file that the installer did not create is in the way", U,
         "Move or rename that file on the reader yourself, or use replace for an app installed by hand.", 1},
        {"E-INS-21", "A path belongs to another installed app", U, "Remove the other app first, or choose another app.", 1},
        {"E-INS-22", "A link lies on a path the installer would write", U, "Remove the link on the reader yourself; the installer never follows links.", 1},
        {"E-INS-23", "An earlier operation was interrupted", N, "Run repair (or give --confirm-repair) to finish or undo it before anything else.", 4},
        {"E-INS-24", "The repair cannot finish", N, "Look at the file named in the message; everything else was handled.", 1},
        {"E-INS-25", "The reader changed since the plan was made", U, "Run the action again to get a new plan.", 2},
        {"E-INS-26", "The catalog or the package could not be downloaded", U,
         "Check the internet connection and the clock of the computer; installing a local file and seeing what is installed still work.", 2},
        {"E-INS-27", "The local package is not a regular readable file, or is too large", U, "Choose a ZIP file of at most 64 MiB.", 2},
        {"E-INS-28", "The app is not offered", U, "Choose an app that is listed in the catalog and not withdrawn.", 1},
        {"E-INS-29", "(not used; the number is kept so that ids cited elsewhere stay stable)", U, "", 1},
        {"E-INS-30", "The app is not installed", U, "Check the name with list or installed.", 1},
        {"E-INS-31", "The app has no install record", U, "Adopt it if its files equal the catalog package, or replace it first.", 1},
        {"E-INS-32", "Files of the app were changed since the install", U, "Accept with --confirm-edited-files, or stop here.", 2},
        {"E-INS-33", "Cancelled by the user", R, "Run the action again when you want to.", 3},
        {"E-INS-34", "Another instance is working on this reader", U, "Close the other instance first, then try again.", 2},
        {"E-INS-35", "A record or journal on the reader is not understood", U, "The installer may need updating; nothing was touched.", 1},
        {"E-INS-36", "A network step made no progress for 30 seconds", U, "Check the internet connection and try again.", 2},
        {"E-INS-37", "A write to the reader failed", R, "Check the reader; the previous version is intact.", 2},
        {"E-INS-38", "An app installed outside the installer would be replaced", U, "Accept with --confirm-replace, or stop here.", 2},
        {"W-INS-01", "Nothing to do: the app is already at the offered version", U, "", 0},
        {"W-INS-02", "The menu entry was changed since the install", U, "", 0},
        {"W-INS-03", "No earlier catalog serial was on record", U, "", 0},
        {"W-INS-04", "PB Store was removed; its own cache and log in system/pbstore/app/ were kept", U, "", 0},
    };
    return rules;
}

const InstallRule* find_install_rule(const std::string& id) {
    for (const InstallRule& r : install_rule_registry())
        if (id == r.id) return &r;
    return nullptr;
}

int exit_status_for(const std::vector<Finding>& findings) {
    int status = 0;
    for (const Finding& f : findings) {
        if (f.severity == Severity::Notice) continue;
        const InstallRule* r = find_install_rule(f.rule_id);
        status = std::max(status, r ? r->exit_status : 1);
    }
    return status;
}

}  // namespace install
}  // namespace pbstore
