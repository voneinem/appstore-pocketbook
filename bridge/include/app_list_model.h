#pragma once
// The list of apps as a Qt model: one row per AppRow of the service, with the state both as words and as an icon name (a state is
// never shown by colour or position only, spec FR-008). Text roles come from the service already passed through store_text.

#include <QAbstractListModel>
#include <vector>

#include "pbstore/store/app_row.h"

class AppListModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Role { AppIdRole = Qt::UserRole + 1, NameRole, VersionRole, ShortDescriptionRole, StateWordsRole, StateIconRole, NotesRole,
                OfferedVersionRole, InstalledVersionRole, ActionsRole, StateKeyRole };

    explicit AppListModel(QObject* parent = nullptr) : QAbstractListModel(parent) {}
    int rowCount(const QModelIndex& parent = QModelIndex()) const override { return parent.isValid() ? 0 : static_cast<int>(rows_.size()); }
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setRows(std::vector<pbstore::store::AppRow> rows);
    const pbstore::store::AppRow* find(const QString& id) const;
    const std::vector<pbstore::store::AppRow>& rows() const { return rows_; }

private:
    std::vector<pbstore::store::AppRow> rows_;
};
