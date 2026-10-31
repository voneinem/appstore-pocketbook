#include "app_list_model.h"

#include <QStringList>

using pbstore::store::AppRow;

namespace {
QString q(const std::string& s) { return QString::fromStdString(s); }

const char* state_key(pbstore::store::RowState s) {
    switch (s) {
        case pbstore::store::RowState::NotInstalled: return "not-installed";
        case pbstore::store::RowState::Installed: return "installed";
        case pbstore::store::RowState::UpdateAvailable: return "update-available";
        case pbstore::store::RowState::InstalledOutside: return "installed-outside";
        case pbstore::store::RowState::NotInCatalog: return "not-in-catalog";
    }
    return "";
}
}  // namespace

QVariant AppListModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(rows_.size())) return QVariant();
    const AppRow& r = rows_[static_cast<size_t>(index.row())];
    switch (role) {
        case AppIdRole: return q(r.id);
        case NameRole: return q(r.name);
        case VersionRole: return q(r.version);
        case ShortDescriptionRole: return q(r.short_description);
        case StateWordsRole: return QString::fromLatin1(pbstore::store::state_words(r.state));
        case StateIconRole: return QString::fromLatin1(pbstore::store::state_icon(r.state));
        case StateKeyRole: return QString::fromLatin1(state_key(r.state));
        case NotesRole: {
            QStringList notes;
            for (const std::string& n : r.notes) notes << q(n);
            return notes.join(QStringLiteral("; "));
        }
        case OfferedVersionRole: return q(r.offered_version);
        case InstalledVersionRole: return q(r.installed_version);
        case ActionsRole: {
            QStringList actions;
            for (pbstore::store::RowAction a : r.actions) actions << QString::fromLatin1(pbstore::store::action_words(a));
            return actions;
        }
    }
    return QVariant();
}

QHash<int, QByteArray> AppListModel::roleNames() const {
    return {{AppIdRole, "appId"}, {NameRole, "name"}, {VersionRole, "version"}, {ShortDescriptionRole, "shortDescription"},
            {StateWordsRole, "stateWords"}, {StateIconRole, "stateIcon"}, {StateKeyRole, "stateKey"}, {NotesRole, "notes"},
            {OfferedVersionRole, "offeredVersion"}, {InstalledVersionRole, "installedVersion"}, {ActionsRole, "actions"}};
}

void AppListModel::setRows(std::vector<AppRow> rows) {
    beginResetModel();
    rows_ = std::move(rows);
    endResetModel();
}

const AppRow* AppListModel::find(const QString& id) const {
    for (const AppRow& r : rows_)
        if (QString::fromStdString(r.id) == id) return &r;
    return nullptr;
}
