#include "args.h"

namespace pbstore {
namespace install {
namespace cli {

namespace {

bool takes_app(const std::string& c) { return c == "install" || c == "update" || c == "remove" || c == "adopt" || c == "replace"; }

bool known_command(const std::string& c) {
    return takes_app(c) || c == "readers" || c == "installed" || c == "list" || c == "repair" || c == "gui";
}

}  // namespace

Options parse_args(const std::vector<std::string>& args) {
    Options o;
    for (size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        auto value_of = [&](const std::string& name, std::string* out) {
            if (a == name) {
                if (i + 1 >= args.size()) {
                    o.usage_error = "the option " + name + " needs a value";
                    return true;
                }
                *out = args[++i];
                return true;
            }
            if (a.compare(0, name.size() + 1, name + "=") == 0) {
                *out = a.substr(name.size() + 1);
                return true;
            }
            return false;
        };
        std::string v;
        if (a == "--help" || a == "-h") o.help = true;
        else if (a == "--version") o.version = true;
        else if (a == "--dry-run") o.dry_run = true;
        else if (a == "--delete-data") o.delete_data = true;
        else if (a == "--confirm-plan") o.confirmed.insert(Confirmation::Plan);
        else if (a == "--confirm-edited-files") o.confirmed.insert(Confirmation::EditedFiles);
        else if (a == "--confirm-unknown-model") o.confirmed.insert(Confirmation::UnknownModel);
        else if (a == "--confirm-unknown-firmware") o.confirmed.insert(Confirmation::UnknownFirmware);
        else if (a == "--confirm-existing-data-folder") o.confirmed.insert(Confirmation::ExistingDataFolder);
        else if (a == "--confirm-replace") o.confirmed.insert(Confirmation::ReplaceOutside);
        else if (a == "--confirm-repair") o.confirmed.insert(Confirmation::Repair);
        else if (value_of("--reader", &v)) o.reader = v;
        else if (value_of("--file", &v)) o.local_file = v;
        else if (value_of("--confirm-delete-data-1", &v)) o.delete_token_1 = v;
        else if (value_of("--confirm-delete-data-2", &v)) o.delete_token_2 = v;
        else if (a.size() > 1 && a[0] == '-') o.usage_error = "unknown option " + a + " (see --help)";
        else if (o.command.empty()) o.command = a;
        else if (takes_app(o.command) && o.app_id.empty()) o.app_id = a;
        else o.usage_error = "unexpected argument " + a + " (see --help)";
        if (!o.usage_error.empty()) return o;
    }
    if (o.command.empty()) return o;
    if (!known_command(o.command)) {
        o.usage_error = "unknown command " + o.command + " (see --help)";
        return o;
    }
    if (o.command == "install" && !o.local_file.empty()) {
        if (!o.app_id.empty()) o.usage_error = "install --file takes the package file, not an app name";
        return o;
    }
    if (takes_app(o.command) && o.app_id.empty()) o.usage_error = "the command " + o.command + " needs an app name (see list)";
    if (!o.local_file.empty() && o.command != "install") o.usage_error = "--file belongs to install";
    if (o.delete_data && o.command != "remove") o.usage_error = "--delete-data belongs to remove";
    return o;
}

std::string help_text(const std::string& version) {
    return "pbstore-install " + version + " - install apps on a PocketBook InkPad One connected as USB storage\n"
           "\n"
           "Usage: pbstore-install COMMAND [OPTIONS]\n"
           "\n"
           "Commands:\n"
           "  readers                    list the drives that could be the reader\n"
           "  installed                  show what is installed on the reader (works offline)\n"
           "  list                       show the catalog and the state of each app on the reader\n"
           "  install APP                install an app of the catalog\n"
           "  install --file PACKAGE.zip install a package from this computer (not from the catalog)\n"
           "  update APP                 update an installed app to the offered version\n"
           "  remove APP                 remove an app (its data folder is kept unless you ask)\n"
           "  adopt APP                  let the installer manage an app that was copied by hand\n"
           "  replace APP                replace an app that was copied by hand\n"
           "  repair                     finish or undo an operation that was interrupted\n"
           "  gui                        open the guided assistant in your browser\n"
           "\n"
           "Options:\n"
           "  --reader DIR               the top folder of the reader (needed if several drives qualify)\n"
           "  --dry-run                  show the plan and write nothing\n"
           "  --confirm-plan             confirm the plan (always needed to write)\n"
           "  --confirm-unknown-model, --confirm-unknown-firmware, --confirm-existing-data-folder,\n"
           "  --confirm-edited-files, --confirm-replace, --confirm-repair\n"
           "                             the further confirmations a plan may ask for, one each\n"
           "  --delete-data              with remove: also delete the data folder (two more confirmations)\n"
           "  --confirm-delete-data-1=FOLDER, --confirm-delete-data-2=COUNT\n"
           "  --version, --help\n"
           "\n"
           "Without a terminal every needed confirmation must be given as an option; nothing is ever assumed.\n"
           "Exit status: 0 success, 1 content or verification failure, 2 usage or environment problem,\n"
           "3 declined or cancelled, 4 an interrupted operation was found and not repaired.\n";
}

}  // namespace cli
}  // namespace install
}  // namespace pbstore
