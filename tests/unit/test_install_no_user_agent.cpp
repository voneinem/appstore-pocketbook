// Engine addition E5 (contracts/engine-additions.md): an empty NetOptions::user_agent means "send no User-Agent header" (spec FR-043,
// constitution Principle V: only what an HTTPS request inevitably contains); the installer passes its own agent and is unchanged.
#include "install_rig.h"
#include "test_support.h"

using namespace pbstore;
using namespace pbstore::install;
using namespace pbtest;

namespace {

bool has_header(const std::string& request, const std::string& name) {
    std::string low = request;
    for (char& c : low) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    std::string n = "\r\n" + name + ":";
    for (char& c : n) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return low.find(n) != std::string::npos;
}

std::vector<std::string> header_names(const std::string& request) {
    std::vector<std::string> names;
    size_t pos = request.find("\r\n");
    while (pos != std::string::npos && pos + 2 < request.size()) {
        const size_t next = request.find("\r\n", pos + 2);
        const std::string line = request.substr(pos + 2, next == std::string::npos ? std::string::npos : next - pos - 2);
        if (line.empty()) break;
        names.push_back(line.substr(0, line.find(':')));
        pos = next;
    }
    return names;
}

}  // namespace

PB_TEST(an_empty_user_agent_sends_no_user_agent_header_and_nothing_beyond_host_and_accept) {
    KeyPair key = make_keypair();
    CatalogHost host(key);
    if (!host.ready()) PB_SKIP("openssl CLI not available");
    host.add_app(nested_sample_app());
    host.publish(100);
    FetchResult r = host.downloader("")(host.catalog_url(), 1 << 20, 30, nullptr);
    PB_CHECK(r.ok);
    PB_CHECK(!host.raw_requests().empty());
    for (const std::string& req : host.raw_requests()) {
        PB_CHECK(!has_header(req, "User-Agent"));
        PB_CHECK((header_names(req) == std::vector<std::string>{"Host", "Accept"}));
    }
}

PB_TEST(the_installers_own_agent_is_sent_exactly_as_before) {
    KeyPair key = make_keypair();
    CatalogHost host(key);
    if (!host.ready()) PB_SKIP("openssl CLI not available");
    host.add_app(nested_sample_app());
    host.publish(100);
    FetchResult r = host.downloader("pbstore-install/0.2.0")(host.catalog_url(), 1 << 20, 30, nullptr);
    PB_CHECK(r.ok);
    for (const std::string& req : host.raw_requests()) PB_CHECK(req.find("\r\nUser-Agent: pbstore-install/0.2.0\r\n") != std::string::npos);
}

PB_TEST_MAIN()
