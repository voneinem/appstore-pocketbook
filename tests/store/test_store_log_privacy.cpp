// T135 (spec SC-010, research R-20): nothing personal reaches the log of the store or a message it shows. A full cycle - a refresh that fails
// with a network error full of personal strings, a refresh, a Wi-Fi switch that is refused, a refusal for a low battery, an install, a
// removal, an adoption, an interrupted install and its repair - runs against StoreRig; then pbstore.log* and every message text are searched
// for an IPv4 address, an IPv6 address, a hardware address, a Wi-Fi name, a serial number, an e-mail address and the user name.
#include <fstream>
#include <sstream>

#include "recording_fs.h"
#include "store_rig.h"
#include "test_support.h"
#include "throwing_fs.h"
#include "pbstore/store/store_redact.h"
#include "pbstore/store/store_rules.h"

using namespace pbstore;
using namespace pbstore::install;
using namespace pbstore::store;
using namespace pbtest;
using pbstore::install::Message;

namespace {

bool has(const std::string& t, const std::string& n) { return t.find(n) != std::string::npos; }

const char* kIPv4 = "192.168.178.23";
const char* kIPv6 = "2001:db8:85a3::8a2e:370:7334";
const char* kLinkLocal = "fe80::1c2b:3d4e:5f60:7182";
const char* kMac = "a4:5e:60:c8:11:9f";
const char* kWifi = "HomeWLAN-Meier-5G";
const char* kSerial = "SN-9X7Q-2026-ABCDEF";
const char* kMail = "max.mustermann@example.org";
const char* kUser = "volker";

std::vector<std::string> personal() { return {kIPv4, kIPv6, kLinkLocal, kMac, kWifi, kSerial, kMail, kUser}; }

// What a network library may say when it fails: every personal string in one text.
std::string hostile_error() {
    return std::string("connect to ") + kIPv4 + " port 443 failed; via [" + kIPv6 + "] and " + kLinkLocal + "; interface " + kMac + " on network " + kWifi +
           " of device " + kSerial + ", account " + kMail + ", home /home/" + kUser + "/books";
}

Downloader failing_download() {
    return [](const std::string&, uint64_t, long, const DownloadProgress&) {
        FetchResult r;
        r.ok = false;
        r.kind = FetchKind::Unreachable;
        r.error = hostile_error();
        return r;
    };
}

std::string slurp(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

struct Seen {
    std::string text;   // every message text shown, one after another
    void add(const std::vector<Message>& messages) { for (const Message& m : messages) text += m.text() + "\n"; }
    void add(const std::vector<Finding>& findings) { for (const Finding& f : findings) text += store_message(f).text() + "\n"; }
};

struct Cycle {
    StoreRig rig;
    std::unique_ptr<StoreService> service;
    CancelToken cancel;
    Seen seen;
    explicit Cycle(const FakeReader& r = FakeReader()) : rig(r) {
        rig.redactor.set_user_name(kUser);
        rig.redactor.add_secret(kWifi);
        rig.redactor.add_secret(kSerial);
    }
    std::string log() {
        std::string all;
        for (const char* suffix : {"", ".1", ".2"}) all += slurp(rig.root() + "/system/pbstore/app/pbstore.log" + suffix);
        return all;
    }
    void start(const Downloader& download = nullptr) {
        rig.make_engine(download);
        service = rig.make_service();
        seen.add(service->start_check().notices);
    }
    std::vector<std::string> consents(const PlanView& p) {
        std::vector<std::string> v;
        for (const ConfirmationItem& i : p.confirmations) v.push_back(i.name);
        return v;
    }
};

void check_nothing_personal(const std::string& where, const std::string& text) {
    for (const std::string& secret : personal())
        if (has(text, secret)) PB_FAIL_AT(where + ": \"" + secret + "\" appears in\n" + text);
}

}  // namespace

PB_TEST(store_redact_replaces_addresses_of_every_shape_and_leaves_ordinary_text_alone) {
    const std::string out = store_redact(hostile_error());
    for (const char* secret : {kIPv4, kIPv6, kLinkLocal, kMac, kMail}) PB_CHECK(!has(out, secret));
    PB_CHECK(has(out, "port 443 failed"));                                // the rest of the sentence stays readable
    PB_CHECK(has(out, "<address>"));
    PB_CHECK(has(out, "<hardware address>"));
    PB_CHECK(has(out, "<e-mail address>"));
    // an address with a port, at the end of a sentence, in brackets, and next to a colon
    PB_CHECK_EQ(store_redact("connect to 192.168.1.5:443 failed"), std::string("connect to <address>:443 failed"));
    PB_CHECK_EQ(store_redact("no route to 10.0.0.7."), std::string("no route to <address>."));
    PB_CHECK_EQ(store_redact("host [fe80::1]:443 refused"), std::string("host [<address>]:443 refused"));
    PB_CHECK_EQ(store_redact("interface aa:bb:cc:dd:ee:ff: down"), std::string("interface <hardware address>: down"));
    // things that look a little like addresses but are not
    for (const char* keep : {"Plan: Update 1.2.0 to 1.3.0 at 2026-10-30T13:09:00Z", "firmware 6.11.1437 on model 1030", "version 0.1.0 of 3 files, 12.5 MiB", "E-INS-25 (free 23167.4 MiB)",
                             "3 of 12 files at 10:42", "std::string and main::run", "https://github.com/voneinem/appstore-pocketbook/releases/download/x/y.zip"})
        PB_CHECK_EQ(store_redact(keep), std::string(keep));
    // credentials and query strings of an address are dropped by the installer's own redactor, which the store's builds on
    StoreRedactor r;
    r.set_user_name(kUser);
    r.add_secret(kWifi);
    const std::string url = r.redact("get https://max:secret@host.example/catalog.json?token=abc#frag failed, user volker, network HomeWLAN-Meier-5G");
    PB_CHECK(!has(url, "secret") && !has(url, "token=abc") && !has(url, kUser) && !has(url, kWifi));
    PB_CHECK(has(url, "host.example/catalog.json"));
    // a user name too short to be one (USER=pb, app) is not replaced either: it would hide ordinary words
    StoreRedactor shortUser;
    shortUser.set_user_name("pb");
    PB_CHECK_EQ(shortUser.redact("pb store app log"), std::string("pb store app log"));
    // a secret too short to be one is not replaced everywhere
    StoreRedactor shorty;
    shorty.add_secret("ab");
    PB_CHECK_EQ(shorty.redact("table of contents"), std::string("table of contents"));
}

PB_TEST(a_full_cycle_leaves_nothing_personal_in_the_log_or_in_any_message) {
    // a network that fails with personal strings, then a normal one
    Cycle c;
    if (!c.rig.ready()) PB_SKIP("openssl CLI not available");
    c.rig.host.add_app(nested_sample_app("testapp", "1.2.0"));
    c.rig.host.publish(100);
    c.rig.write_reader();
    c.start(failing_download());
    StoreOutcome failed = c.service->refresh(c.cancel, nullptr);
    PB_CHECK(!failed.ok);
    c.seen.add(failed.findings);
    c.seen.add(failed.messages());
    c.start();
    PB_CHECK(c.service->refresh(c.cancel, nullptr).ok);
    // a Wi-Fi switch that is refused
    c.rig.radio.set_connect(FakeRadio::Connect::Refused);
    PlanView refused = c.service->plan_action(RowAction::Install, "testapp", c.cancel, nullptr);
    PB_CHECK(!refused.ok);
    c.seen.add(refused.messages());
    c.rig.radio = FakeRadio();
    // a refusal for a low battery
    c.rig.battery.set_percent(20);
    PlanView low = c.service->plan_action(RowAction::Install, "testapp", c.cancel, nullptr);
    PB_CHECK(!low.ok);
    c.seen.add(low.messages());
    c.rig.battery.set_percent(80);
    // install and remove
    PlanView plan = c.service->plan_action(RowAction::Install, "testapp", c.cancel, nullptr);
    PB_CHECK(plan.ok);
    ExecView installed = c.service->execute_plan(c.consents(plan), false, c.cancel, nullptr);
    PB_CHECK(installed.ok);
    c.seen.text += installed.text + "\n" + plan.text + "\n" + plan.summary + "\n";
    PlanView removal = c.service->plan_action(RowAction::Remove, "testapp", c.cancel, nullptr);
    PB_CHECK(removal.ok);
    ExecView removed = c.service->execute_plan(c.consents(removal), false, c.cancel, nullptr);
    PB_CHECK(removed.ok);
    c.seen.text += removed.text + "\n";

    // an interrupted install and its repair
    const ReaderFsFactory normal = c.rig.storage.open_fs;
    c.rig.storage.open_fs = [normal](const std::string& root) -> std::unique_ptr<ReaderFs> { return std::make_unique<ThrowingFs>(normal(root), 14); };
    c.start();
    PlanView again = c.service->plan_action(RowAction::Install, "testapp", c.cancel, nullptr);
    PB_CHECK(again.ok);
    bool lost = false;
    try { c.service->execute_plan(c.consents(again), false, c.cancel, nullptr); } catch (const PowerLoss&) { lost = true; }
    PB_CHECK(lost);
    c.rig.storage.open_fs = normal;
    c.start();
    PB_CHECK(c.service->repair_status().pending);
    c.seen.text += c.service->repair_status().text + "\n";
    ExecView fixed = c.service->repair(false, c.cancel, nullptr);
    PB_CHECK(fixed.ok);
    c.seen.text += fixed.text + "\n";

    PB_CHECK(!c.log().empty());
    check_nothing_personal("pbstore.log", c.log());
    check_nothing_personal("a shown message", c.seen.text);
    PB_CHECK(has(c.seen.text, "port 443 failed"));                       // the cause is still told, without the personal part
}

PB_TEST(adopting_and_the_log_of_an_adoption_hold_nothing_personal) {
    FakeReader hand;
    hand.add_hand_installed(nested_sample_app("testapp", "1.2.0"));
    hand.put_file("system/config/device.cfg", std::string("serial=") + kSerial + "\n");   // a serial on the storage must not travel into the log
    Cycle c(hand);
    if (!c.rig.ready()) PB_SKIP("openssl CLI not available");
    c.rig.host.add_app(nested_sample_app("testapp", "1.2.0"));
    c.rig.host.publish(100);
    c.rig.write_reader();
    c.start();
    PB_CHECK(c.service->refresh(c.cancel, nullptr).ok);
    PlanView plan = c.service->plan_action(RowAction::Check, "testapp", c.cancel, nullptr);
    PB_CHECK(plan.ok);
    PB_CHECK(c.service->execute_plan(c.consents(plan), false, c.cancel, nullptr).ok);
    check_nothing_personal("pbstore.log", c.log());
    PB_CHECK(!c.log().empty());
}

PB_TEST_MAIN()
