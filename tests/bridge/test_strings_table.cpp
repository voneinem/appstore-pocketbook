// T075: the interface texts are keys of bridge/strings/strings.tsv. The table is read the way the program reads it; a key that is not in it is
// shown as itself (so a missing one cannot go unnoticed); every key of the real table gives its text. That the sources use exactly the keys of
// the table is checked by scripts/extract-strings.py --check (ctest check_strings).
#include <QCoreApplication>
#include <QFile>
#include <QRegularExpression>

#include "qt_strings.h"
#include "test_support.h"

namespace {
QCoreApplication& app() {
    static int argc = 1;
    static char name[] = "test";
    static char* argv[] = {name, nullptr};
    static QCoreApplication a(argc, argv);
    return a;
}
}  // namespace

PB_TEST(a_table_gives_the_text_of_a_key_with_its_escapes_and_ignores_comments) {
    TableTranslator t;
    PB_CHECK(t.loadTable("# a comment\n\nhome.title\tPB Store\nhome.two\tfirst\\nsecond\\tcolumn \\\\ end\r\n"));
    PB_CHECK_EQ(t.entryCount(), 2);
    PB_CHECK_EQ(t.translate("x", "home.title").toStdString(), std::string("PB Store"));
    PB_CHECK_EQ(t.translate("x", "home.two").toStdString(), std::string("first\nsecond\tcolumn \\ end"));
    PB_CHECK(t.translate("x", "home.unknown").isNull());   // Qt then shows the key itself
}

PB_TEST(a_malformed_table_is_refused_as_a_whole) {
    TableTranslator t;
    PB_CHECK(t.loadTable("a.b\tone\n"));
    PB_CHECK(!t.loadTable("a.b\tone\nno tab here\n"));
    PB_CHECK(t.isEmpty());
    PB_CHECK(!t.loadTable("a.b\tone\na.b\ttwo\n"));        // a key twice
    PB_CHECK(!t.loadTable("a.b\tone\ttoo many\n"));
    PB_CHECK(t.isEmpty());
}

PB_TEST(a_missing_key_is_shown_as_the_key) {
    app();
    install_strings();
    PB_CHECK_EQ(QCoreApplication::translate("x", "no.such.key").toStdString(), std::string("no.such.key"));
    PB_CHECK_EQ(QCoreApplication::translate("x", "home.refresh").toStdString(), std::string("Refresh"));
}

PB_TEST(every_key_of_the_real_table_has_a_text_of_its_own_with_complete_placeholders) {
    app();
    QFile file(QStringLiteral(PBSTORE_SOURCE_DIR "/bridge/strings/strings.tsv"));
    PB_CHECK(file.open(QIODevice::ReadOnly));
    TableTranslator t;
    PB_CHECK(t.loadTable(file.readAll()));
    PB_CHECK(t.entryCount() > 50);
    const QRegularExpression placeholder(QStringLiteral("%([1-9])"));
    for (const QString& key : t.keys()) {
        const QString text = t.translate("x", key.toUtf8().constData());
        if (text.trimmed().isEmpty()) PB_FAIL_AT("empty text: " + key.toStdString());
        if (text == key) PB_FAIL_AT("the text is the key: " + key.toStdString());
        int highest = 0, count = 0;
        QSet<int> seen;
        for (auto it = placeholder.globalMatch(text); it.hasNext();) {
            const int n = it.next().captured(1).toInt();
            highest = std::max(highest, n);
            if (!seen.contains(n)) { seen.insert(n); ++count; }
        }
        if (highest != count) PB_FAIL_AT("placeholders with a gap: " + key.toStdString());   // %1 and %3 without %2
    }
}

PB_TEST_MAIN()
