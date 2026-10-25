// T042: the journal (contracts/journal.md): intent and markers with their checksum line, the writing rule,
// and the newer-format refusal.
#include "install_fixtures.h"
#include "test_support.h"

using namespace pbstore::install;
using namespace pbtest;

namespace {
Built sample_plan(Volume& v) { return make_install_plan(v.fat, nested_sample_app()); }
}  // namespace

PB_TEST(the_intent_has_the_fields_of_the_contract) {
    FakeReader reader;
    Volume v(reader);
    Built b = sample_plan(v);
    Intent in = intent_from_plan(b.plan);
    PB_CHECK_EQ(in.journal_format, 1);
    PB_CHECK_EQ(in.kind, std::string("install"));
    PB_CHECK_EQ(in.app_id, std::string("testapp"));
    PB_CHECK_EQ(in.to_version, std::string("1.2.0"));
    PB_CHECK_EQ(in.from_version, std::string(""));
    PB_CHECK_EQ(in.items.size(), b.plan.items.size());
    PB_CHECK_EQ(in.items.back().path, b.plan.launcher);            // the launcher is placed last
    for (const auto& it : in.items) {
        PB_CHECK(!it.new_sha256.empty());
        PB_CHECK(it.new_size > 0);
        PB_CHECK(it.old_sha256.empty());
        PB_CHECK(!it.vacate);
    }
    PB_CHECK_EQ(in.directories_to_create, b.plan.directories_to_create);
    PB_CHECK_EQ(in.view_old_sha256, b.plan.view_old_sha256);
    PB_CHECK_EQ(in.view_new_sha256, b.plan.view_new_sha256);
    PB_CHECK_EQ(in.record_new_sha256, b.plan.record_new_sha256);
    PB_CHECK(in.record_old_sha256.empty());
    PB_CHECK_EQ(in.backup_name, b.plan.backup.name);
    auto j = pbstore::json::parse(intent_text(in).substr(0, intent_text(in).rfind("sha256:")));
    PB_CHECK(j.has_value());
    PB_CHECK_EQ(j->get_int("journal_format").value_or(0), int64_t(1));
    for (const char* key : {"kind", "app_id", "from_version", "to_version", "items", "directories_to_create", "view", "record", "backup", "launcher"})
        PB_CHECK(j->find(key) != nullptr);
}

PB_TEST(the_intent_round_trips_through_its_text) {
    FakeReader reader;
    Volume v(reader);
    Built b = sample_plan(v);
    Intent in = intent_from_plan(b.plan);
    const std::string text = intent_text(in);
    IntentParse p = parse_intent(text);
    PB_CHECK(p.intent.has_value());
    PB_CHECK_EQ(intent_text(*p.intent), text);
    PB_CHECK_EQ(p.sha256, pbstore::sha256_hex(text));
}

PB_TEST(a_file_without_a_matching_last_line_is_read_as_absent) {
    FakeReader reader;
    Volume v(reader);
    Built b = sample_plan(v);
    const std::string text = intent_text(intent_from_plan(b.plan));
    PB_CHECK(!parse_intent(text.substr(0, text.size() / 2)).intent.has_value());                 // cut off
    PB_CHECK(!parse_intent(text.substr(0, text.rfind("sha256:"))).intent.has_value());          // no last line
    std::string tampered = text;
    tampered[10] = tampered[10] == 'x' ? 'y' : 'x';
    PB_CHECK(!parse_intent(tampered).intent.has_value());                                        // changed content
    std::string wrong = text;
    wrong.replace(wrong.rfind("sha256:") + 7, 4, "0000");
    PB_CHECK(!parse_intent(wrong).intent.has_value());                                           // wrong hash
    PB_CHECK(!parse_intent("").intent.has_value());
    std::string body;
    PB_CHECK(!strip_checksum_line("no line at all", &body));
    PB_CHECK(strip_checksum_line(with_checksum_line("abc\n"), &body));
    PB_CHECK_EQ(body, std::string("abc\n"));
}

PB_TEST(a_journal_of_a_newer_format_is_not_touched_and_says_so) {
    FakeReader reader;
    Volume v(reader);
    Built b = sample_plan(v);
    Intent in = intent_from_plan(b.plan);
    in.journal_format = 2;
    IntentParse p = parse_intent(intent_text(in));
    PB_CHECK(!p.intent.has_value());
    PB_CHECK(p.newer_format);
    // the recovery reports E-INS-35 and removes nothing
    PB_CHECK(v.fat.make_dir("system/pbstore") || true);
    v.fat.make_dir("system/pbstore/journal");
    v.fat.make_dir("system/pbstore/journal/current");
    PB_CHECK(write_journal_file(v.fat, kJournalDir, kIntentName, intent_text(in)));
    Snapshot before = snapshot(v.fat);
    RepairCheck rc = plan_repair(v.fat);
    PB_CHECK(!rc.plan.has_value());
    PB_CHECK(!rc.findings.empty());
    PB_CHECK_EQ(rc.findings[0].rule_id, std::string("E-INS-35"));
    PB_CHECK(compare(before, snapshot(v.fat)).identical);
}

PB_TEST(markers_name_the_intent_they_belong_to) {
    const std::string intent_hash = pbstore::sha256_hex("an intent");
    const std::string m = marker_text(kPreparedName, intent_hash);
    PB_CHECK(marker_valid(m, kPreparedName, intent_hash));
    PB_CHECK(!marker_valid(m, kCommitName, intent_hash));                          // the other marker
    PB_CHECK(!marker_valid(m, kPreparedName, pbstore::sha256_hex("another")));    // another operation
    PB_CHECK(!marker_valid(m.substr(0, m.size() - 3), kPreparedName, intent_hash));
    PB_CHECK(!marker_valid("", kPreparedName, intent_hash));
}

PB_TEST(a_journal_file_is_written_by_temporary_name_flush_rename_and_directory_flush) {
    CrashFs mem;
    FakeReader().load_into(mem);
    TraceFs fs(mem);
    PB_CHECK(fs.make_dir("system/pbstore") || fs.stat("system/pbstore").exists());
    fs.make_dir("system/pbstore/journal");
    fs.make_dir("system/pbstore/journal/current");
    fs.ops.clear();
    PB_CHECK(write_journal_file(fs, kJournalDir, "10-intent.json", "content"));
    std::vector<std::string> names;
    for (const auto& op : fs.ops) if (op.mutating) names.push_back(op.name + ":" + op.a + (op.b.empty() ? "" : ">" + op.b));
    const std::string dir = kJournalDir;
    PB_CHECK_EQ(names, (std::vector<std::string>{"create:" + dir + "/10-intent.json.tmp", "rename:" + dir + "/10-intent.json.tmp>" + dir + "/10-intent.json", "flush_dir:" + dir}));
    std::string out;
    PB_CHECK(mem.read_file(dir + "/10-intent.json", 100, &out));
    PB_CHECK_EQ(out, std::string("content"));
    PB_CHECK(!mem.stat(dir + "/10-intent.json.tmp").exists());
}

PB_TEST(a_journal_file_is_never_renamed_onto_an_existing_name) {
    CrashFs mem;
    FakeReader().load_into(mem);
    mem.make_dir("system/pbstore");
    mem.make_dir("system/pbstore/journal");
    mem.make_dir("system/pbstore/journal/current");
    PB_CHECK(write_journal_file(mem, kJournalDir, "20-prepared", "first"));
    PB_CHECK(!write_journal_file(mem, kJournalDir, "20-prepared", "second"));
    std::string out;
    PB_CHECK(mem.read_file(std::string(kJournalDir) + "/20-prepared", 100, &out));
    PB_CHECK_EQ(out, std::string("first"));
    PB_CHECK(!mem.stat(std::string(kJournalDir) + "/20-prepared.tmp").exists());   // no leftover
}

PB_TEST(the_journal_folder_can_be_deleted_with_everything_in_it) {
    CrashFs mem;
    FakeReader().load_into(mem);
    for (const char* d : {"system/pbstore", "system/pbstore/journal", "system/pbstore/journal/current", "system/pbstore/journal/current/new", "system/pbstore/journal/current/new/applications"})
        mem.make_dir(d);
    mem.create_exclusive("system/pbstore/journal/current/new/applications/a", "x", true);
    mem.create_exclusive("system/pbstore/journal/current/10-intent.json", "x", true);
    PB_CHECK(journal_exists(mem));
    PB_CHECK(delete_journal(mem));
    PB_CHECK(!journal_exists(mem));
    PB_CHECK(!mem.stat("system/pbstore/journal/current").exists());
    PB_CHECK(mem.stat("system/pbstore/journal").exists());          // the installer area itself stays
}

PB_TEST_MAIN()
