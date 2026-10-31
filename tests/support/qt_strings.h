#pragma once
// The bridge tests read texts the way the program does: the controller's tr("key") and the QML's qsTr("key") are keys of bridge/strings/strings.tsv.
// Call install_strings() once after the QCoreApplication exists.
#include <QCoreApplication>
#include <QFile>
#include <stdexcept>

#include "table_translator.h"

inline void install_strings() {
    static TableTranslator translator;
    static bool done = false;
    if (done) return;
    QFile file(QStringLiteral(PBSTORE_SOURCE_DIR "/bridge/strings/strings.tsv"));
    if (!file.open(QIODevice::ReadOnly) || !translator.loadTable(file.readAll())) throw std::runtime_error("the strings table cannot be loaded");
    QCoreApplication::installTranslator(&translator);
    done = true;
}
