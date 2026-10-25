#pragma once
// Adopting an app that was copied to the reader by hand: its files are compared with the package of the
// catalog's current version (only that version has hashes). If every file is equal the installer records
// the app and writes nothing else; otherwise the app counts as "installed outside the installer".

#include <string>
#include <vector>

#include "pbstore/install/reader_state.h"
#include "pbstore/manifest.h"

namespace pbstore {
namespace install {

struct AdoptionCheck {
    bool adoptable = false;               // every file is equal and the menu entry points at the app
    std::vector<std::string> differing;   // present, but the size or the SHA-256 differs
    std::vector<std::string> missing;     // listed in the package, not on the reader
    std::vector<std::string> extra;       // in the app's own folder (applications/<id>/), not in the package: left alone, not claimed
    std::string entry_problem;            // "" if the menu entry exists and points at the app
    std::string text() const;             // one explanation for people
};

// The snapshot must have been scanned with the manifest as the request's package.
AdoptionCheck compare_to_package(const ReaderSnapshot& snapshot, const Manifest& manifest);

}  // namespace install
}  // namespace pbstore
