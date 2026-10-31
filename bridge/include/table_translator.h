#pragma once
// The interface texts from one plain table (bridge/strings/strings.tsv), because lrelease and .qm files are not usable with the SDK. The source
// "text" of every qsTr() in the QML and every tr() of the bridge is a key (`home.refresh`); the table gives the English text of the key.
// One line per text: the key, a TAB, the text. \n, \t and \\ are escapes. Lines starting with # and empty lines are ignored.
// A key that is not in the table is shown as the key itself (an obvious fault; scripts/extract-strings.py --check and test_strings_table
// make sure none is missing). Install it with QCoreApplication::installTranslator before the QML is loaded.

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QTranslator>

class TableTranslator : public QTranslator {
public:
    explicit TableTranslator(QObject* parent = nullptr) : QTranslator(parent) {}

    // Replaces the table. A malformed table (a line without exactly one TAB, a key twice) is refused as a whole: false, and the table stays empty.
    bool loadTable(const QByteArray& utf8);
    int entryCount() const { return static_cast<int>(table_.size()); }
    QStringList keys() const { return table_.keys(); }

    QString translate(const char* context, const char* sourceText, const char* disambiguation = nullptr, int n = -1) const override;
    bool isEmpty() const override { return table_.isEmpty(); }

private:
    QHash<QString, QString> table_;
};
