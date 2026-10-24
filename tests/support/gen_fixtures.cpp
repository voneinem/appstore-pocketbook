// gen_fixtures OUTDIR: writes one anonymized invalid package per package rule (and variant) as
// E-PKG-<nn>-<what>.zip, built at run time from the test helpers. Used by the contract tests and
// by quickstart scenario 2. No real app or reader data is involved.
#include <sys/stat.h>

#include <fstream>
#include <iostream>

#include "package_fixtures.h"

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: gen_fixtures OUTDIR\n";
        return 2;
    }
    const std::string dir = argv[1];
    ::mkdir(dir.c_str(), 0755);
    for (const pbtest::Fixture& f : pbtest::invalid_package_fixtures()) {
        const std::string path = dir + "/" + f.rule + "-" + f.what + ".zip";
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << f.zip;
        if (!out) {
            std::cerr << "cannot write " << path << "\n";
            return 2;
        }
        std::cout << path << "\n";
    }
    return 0;
}
