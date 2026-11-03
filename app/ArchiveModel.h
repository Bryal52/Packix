#pragma once

#include "Icons.h"
#include "Types.h"

#include <QAbstractItemModel>

#include <memory>

namespace packix::ui {

// The archive as a tree. Folders first, natural name order (like Folix); a search filter keeps
// matching items with their folders.
class ArchiveModel : public QAbstractItemModel {
    Q_OBJECT
public:
    enum Column { Name, Type, Size, Packed, Date, ColumnCount };
    enum Role { PathRole = Qt::UserRole + 1, IsDirRole, EncryptedRole };

    explicit ArchiveModel(QObject* parent = nullptr);

    void setArchive(const ArchiveInfo& info);
    void setFilter(const QString& text);
    QString filter() const { return m_filter; }
    void retranslate();

    QString pathOf(const QModelIndex& index) const;
    bool isDir(const QModelIndex& index) const;
    QModelIndex indexOfPath(const QString& path) const;
    // Names directly inside a folder of the archive ("" = top level).
    QStringList childNames(const QString& folder) const;
    qint64 sizeOf(const QModelIndex& index) const;

    QModelIndex index(int row, int column, const QModelIndex& parent = {}) const override;
    QModelIndex parent(const QModelIndex& child) const override;
    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    void sort(int column, Qt::SortOrder order) override;

private:
    struct Node {
        QString name;
        QString path;
        bool isDir = false;
        bool encrypted = false;
        qint64 size = 0;
        qint64 packed = -1;
        QDateTime modified;
        TypeInfo type;
        Node* parent = nullptr;
        std::vector<std::unique_ptr<Node>> children;
        int row = 0;
    };

    void rebuild();
    void sortNode(Node* n);
    Node* nodeOf(const QModelIndex& index) const;

    ArchiveInfo m_info;
    std::unique_ptr<Node> m_root;
    QString m_filter;
    int m_sortColumn = Name;
    Qt::SortOrder m_sortOrder = Qt::AscendingOrder;
};

} // namespace packix::ui
