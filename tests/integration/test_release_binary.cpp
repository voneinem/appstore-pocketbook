// T133 (label "release"): checks the released file of the installer. It skips (status 77) unless
// PBSTORE_RELEASE_BINARY names that file; the release workflow and the pull-request job set it.
#include "run_tool.h"
#include "test_support.h"
#include "tmp_dir.h"
#include "fake_reader.h"

#include <elf.h>
#include <sys/stat.h>

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

using namespace pbtest;

namespace {

std::string binary_path() {
    const char* p = std::getenv("PBSTORE_RELEASE_BINARY");
    return p ? p : "";
}

std::string slurp(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

bool has(const std::string& t, const std::string& n) { return t.find(n) != std::string::npos; }

}  // namespace

PB_TEST(the_file_is_a_static_executable) {
    const std::string path = binary_path();
    if (path.empty()) PB_SKIP("PBSTORE_RELEASE_BINARY is not set");
    const std::string data = slurp(path);
    PB_CHECK(data.size() > sizeof(Elf64_Ehdr));
    PB_CHECK(data.compare(0, 4, "\x7f" "ELF") == 0);
    Elf64_Ehdr eh;
    std::memcpy(&eh, data.data(), sizeof eh);
    PB_CHECK_EQ(int(eh.e_ident[EI_CLASS]), int(ELFCLASS64));
    PB_CHECK_EQ(int(eh.e_machine), int(EM_X86_64));
    // no interpreter and no dynamic section: nothing is loaded from the computer's libraries
    for (int i = 0; i < eh.e_phnum; ++i) {
        Elf64_Phdr ph;
        std::memcpy(&ph, data.data() + eh.e_phoff + static_cast<size_t>(i) * eh.e_phentsize, sizeof ph);
        PB_CHECK(ph.p_type != PT_INTERP);
        PB_CHECK(ph.p_type != PT_DYNAMIC);
    }
}

PB_TEST(version_names_the_program_and_the_supported_schemas) {
    const std::string path = binary_path();
    if (path.empty()) PB_SKIP("PBSTORE_RELEASE_BINARY is not set");
    RunResult r = run_program(path.c_str(), {"--version"});
    PB_CHECK_EQ(r.code, 0);
    const std::string version = [] { std::string v = slurp(std::string(PBSTORE_SOURCE_DIR) + "/VERSION"); while (!v.empty() && std::isspace(static_cast<unsigned char>(v.back()))) v.pop_back(); return v; }();
    PB_CHECK(has(r.out, "pbstore-install " + version));
    PB_CHECK(has(r.out, "catalog 1"));
    PB_CHECK(has(r.out, "install record 1"));
    PB_CHECK(has(r.out, "journal 1"));
}

PB_TEST(the_file_holds_no_test_override_and_not_the_test_key) {
    const std::string path = binary_path();
    if (path.empty()) PB_SKIP("PBSTORE_RELEASE_BINARY is not set");
    const std::string data = slurp(path);
    PB_CHECK(data.size() > 1000);
    for (const char* name : {"PBSTORE_TEST_", "PBSTORE_TEST_CATALOG_URL", "PBSTORE_TEST_CA_FILE", "PBSTORE_TEST_STATE_DIR", "PBSTORE_TEST_STOP_AFTER", "PBSTORE_TEST_MOUNTS", "PBSTORE_TEST_HOST_MAP"})
        if (has(data, name)) PB_FAIL_AT(std::string("the released file contains ") + name);
    // the throw-away key of the test build
    const std::string key_inc = slurp(std::string(PBSTORE_BUILD_DIR) + "/test_key/test_trusted_key.inc");
    const size_t a = key_inc.find('"');
    const size_t b = key_inc.find('"', a + 1);
    if (a != std::string::npos && b != std::string::npos) {
        const std::string key = key_inc.substr(a + 1, b - a - 1);
        PB_CHECK(key.size() == 44);
        PB_CHECK(!has(data, key));
    }
    // the real address (an https address; the compiler may share the ending "catalog.json" with other texts) and the
    // real key of keys/trusted are in the file
    PB_CHECK(has(data, "https://voneinem.github.io/appstore-pocketbook/"));
    std::string trusted = slurp(std::string(PBSTORE_SOURCE_DIR) + "/keys/trusted/current.pub");
    while (!trusted.empty() && std::isspace(static_cast<unsigned char>(trusted.back()))) trusted.pop_back();
    PB_CHECK_EQ(trusted.size(), size_t(44));
    // the key that the file trusts is the one of keys/trusted (asked of the file itself; the compiler is free to lay out its texts)
    RunResult v = run_program(path.c_str(), {"--version"});
    PB_CHECK(has(v.out, "trusted catalog keys: 1"));
    PB_CHECK(has(v.out, trusted));
}

PB_TEST(it_runs_without_any_library_in_a_bare_container) {
    const std::string path = binary_path();
    if (path.empty()) PB_SKIP("PBSTORE_RELEASE_BINARY is not set");
    if (std::system("docker info >/dev/null 2>&1") != 0) PB_SKIP("no container runtime");
    TmpDir d;
    FakeReader().write_to(d.path() + "/reader");
    // an image with nothing in it: no libraries, no shell
    PB_CHECK_EQ(std::system("tar -cT /dev/null | docker import - pbstore-empty-test >/dev/null 2>&1"), 0);
    auto run = [&](const std::string& args) {
        const std::string cmd = "docker run --rm --network none -v " + path + ":/pbstore-install:ro -v " + d.path() + "/reader:/reader:ro pbstore-empty-test /pbstore-install " + args + " > " + d.file("out.txt") + " 2> " + d.file("err.txt");
        return std::system(cmd.c_str());
    };
    PB_CHECK_EQ(run("--version"), 0);
    PB_CHECK(has(d.read("out.txt"), "pbstore-install"));
    PB_CHECK_EQ(run("installed --reader /reader"), 0);
    PB_CHECK(has(d.read("out.txt"), "U_AlphaApp"));
    const int readers = run("readers");
    PB_CHECK_EQ(readers, 0);
    std::system("docker rmi -f pbstore-empty-test >/dev/null 2>&1");
}

PB_TEST_MAIN()
