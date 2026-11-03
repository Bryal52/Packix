#include "ArchiveModel.h"

#include "Lang.h"

#include <QApplication>
#include <QCollator>
#include <QHash>
#include <QLocale>
#include <QPalette>

#include <functional>

namespace packix::ui {

namespace {

} // namespace

ArchiveModel::ArchiveModel(QObject* parent) : QAbstractItemModel(parent), m_root(std::make_unique<Node>()) {}

void ArchiveModel::setArchive(const ArchiveInfo& info)
{
    m_info = info;
    rebuild();
}

void ArchiveModel::setFilter(const QString& text)
{
    if (m_filter == text.trimmed())
        return;
    m_filter = text.trimmed();
    rebuild();
}

void ArchiveModel::retranslate()
{
    rebuild();
}

void ArchiveModel::rebuild()
{
    beginResetModel();
    m_root = std::make_unique<Node>();
    m_root->isDir = true;
    QHash<QString, Node*> dirs;
    dirs.insert(QString(), m_root.get());

    // Folders get created on demand, so an item can come before its folder.
    std::function<Node*(const QString&)> folder = [&](const QString& path) -> Node* {
        if (Node* n = dirs.value(path))
            return n;
        const qsizetype slash = path.lastIndexOf(QLatin1Char('/'));
        Node* parent = folder(slash < 0 ? QString() : path.left(slash));
        auto node = std::make_unique<Node>();
        node->name = path.mid(slash + 1);
        node->path = path;
        node->isDir = true;
        node->parent = parent;
        Node* raw = node.get();
        parent->children.push_back(std::move(node));
        dirs.insert(path, raw);
        return raw;
    };

    const bool solid = m_info.solid;
    for (const Item& item : std::as_const(m_info.items)) {
        if (!m_filter.isEmpty()) {
            const QString name = item.path.mid(item.path.lastIndexOf(QLatin1Char('/')) + 1);
            if (!name.contains(m_filter, Qt::CaseInsensitive))
                continue;
        }
        const qsizetype slash = item.path.lastIndexOf(QLatin1Char('/'));
        Node* n = item.isDir ? folder(item.path) : nullptr;
        if (!n) {
            Node* parent = folder(slash < 0 ? QString() : item.path.left(slash));
            auto node = std::make_unique<Node>();
            node->name = item.path.mid(slash + 1);
            node->path = item.path;
            node->parent = parent;
            n = node.get();
            parent->children.push_back(std::move(node));
        }
        n->encrypted = item.encrypted;
        n->modified = item.modified;
        if (!item.isDir) {
            n->size = item.size;
            // Solid blocks report the whole block on one file: per-file numbers would mislead.
            n->packed = solid ? -1 : item.packed;
        }
    }

    // Folder totals and type texts, bottom-up.
    std::function<void(Node*)> finish = [&](Node* n) {
        if (n->isDir) {
            qint64 size = 0, packed = 0;
            bool packedKnown = true;
            for (auto& c : n->children) {
                finish(c.get());
                size += c->size;
                packedKnown = packedKnown && c->packed >= 0;
                packed += qMax<qint64>(0, c->packed);
                n->encrypted = n->encrypted || c->encrypted;
            }
            n->size = size;
            n->packed = packedKnown && !n->children.empty() ? packed : -1;
        }
        if (n != m_root.get())
            n->type = classify(n->name, n->isDir);
    };
    finish(m_root.get());
    sortNode(m_root.get());
    endResetModel();
}

void ArchiveModel::sortNode(Node* n)
{
    QCollator coll{QLocale(Lang::instance().current())};
    coll.setNumericMode(true);
    coll.setCaseSensitivity(Qt::CaseInsensitive);
    const int col = m_sortColumn;
    const bool asc = m_sortOrder == Qt::AscendingOrder;
    std::stable_sort(n->children.begin(), n->children.end(), [&](const auto& a, const auto& b) {
        if (a->isDir != b->isDir)
            return a->isDir;   // folders always first
        int c = 0;
        switch (col) {
        case Type: c = QString::compare(a->type.description, b->type.description, Qt::CaseInsensitive); break;
        case Size: c = a->size < b->size ? -1 : a->size > b->size; break;
        case Packed: c = a->packed < b->packed ? -1 : a->packed > b->packed; break;
        case Date: c = a->modified < b->modified ? -1 : a->modified > b->modified; break;
        default: break;
        }
        if (c == 0)
            c = coll.compare(a->name, b->name);
        return asc ? c < 0 : c > 0;
    });
    for (size_t i = 0; i < n->children.size(); ++i) {
        n->children[i]->row = int(i);
        sortNode(n->children[i].get());
    }
}

void ArchiveModel::sort(int column, Qt::SortOrder order)
{
    m_sortColumn = column;
    m_sortOrder = order;
    emit layoutAboutToBeChanged();
    const QModelIndexList before = persistentIndexList();
    QStringList paths;
    for (const QModelIndex& i : before)
        paths << pathOf(i);
    sortNode(m_root.get());
    QModelIndexList after;
    for (int k = 0; k < before.size(); ++k) {
        const QModelIndex found = indexOfPath(paths[k]);
        after << (found.isValid() ? index(found.row(), before[k].column(), found.parent()) : QModelIndex());
    }
    changePersistentIndexList(before, after);
    emit layoutChanged();
}

ArchiveModel::Node* ArchiveModel::nodeOf(const QModelIndex& index) const
{
    return index.isValid() ? static_cast<Node*>(index.internalPointer()) : m_root.get();
}

QString ArchiveModel::pathOf(const QModelIndex& index) const
{
    return index.isValid() ? nodeOf(index)->path : QString();
}

bool ArchiveModel::isDir(const QModelIndex& index) const
{
    return index.isValid() && nodeOf(index)->isDir;
}

qint64 ArchiveModel::sizeOf(const QModelIndex& index) const
{
    return index.isValid() ? nodeOf(index)->size : 0;
}

QModelIndex ArchiveModel::indexOfPath(const QString& path) const
{
    Node* n = m_root.get();
    if (path.isEmpty())
        return {};
    for (const QString& part : path.split(QLatin1Char('/'))) {
        Node* next = nullptr;
        for (auto& c : n->children) {
            if (c->name == part) {
                next = c.get();
                break;
            }
        }
        if (!next)
            return {};
        n = next;
    }
    return createIndex(n->row, 0, n);
}

QStringList ArchiveModel::childNames(const QString& folder) const
{
    QStringList out;
    // Look in the unfiltered contents, the filter must not hide name clashes.
    const QString prefix = folder.isEmpty() ? QString() : folder + QLatin1Char('/');
    for (const Item& i : m_info.items) {
        if (!i.path.startsWith(prefix))
            continue;
        const QString rest = i.path.mid(prefix.size());
        if (!rest.isEmpty() && !rest.contains(QLatin1Char('/')))
            out << rest;
    }
    return out;
}

QModelIndex ArchiveModel::index(int row, int column, const QModelIndex& parent) const
{
    Node* p = nodeOf(parent);
    if (row < 0 || row >= int(p->children.size()) || column < 0 || column >= ColumnCount)
        return {};
    return createIndex(row, column, p->children[size_t(row)].get());
}

QModelIndex ArchiveModel::parent(const QModelIndex& child) const
{
    if (!child.isValid())
        return {};
    Node* p = nodeOf(child)->parent;
    if (!p || p == m_root.get())
        return {};
    return createIndex(p->row, 0, p);
}

int ArchiveModel::rowCount(const QModelIndex& parent) const
{
    if (parent.column() > 0)
        return 0;
    return int(nodeOf(parent)->children.size());
}

int ArchiveModel::columnCount(const QModelIndex&) const
{
    return ColumnCount;
}

QVariant ArchiveModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid())
        return {};
    const Node* n = nodeOf(index);
    switch (role) {
    case Qt::DisplayRole:
        switch (index.column()) {
        case Name: return n->name;
        case Type: return n->type.description;
        case Size: return humanSize(n->size);
        case Packed: return n->packed >= 0 ? humanSize(n->packed) : QStringLiteral("—");
        case Date: return n->modified.isValid() ? n->modified.toString(QStringLiteral("dd.MM.yyyy HH:mm")) : QString();
        }
        break;
    case Qt::DecorationRole:
        if (index.column() == Name)
            return fileIcon(n->type.cat);
        break;
    case Qt::TextAlignmentRole:
        if (index.column() == Size || index.column() == Packed || index.column() == Date)
            return int(Qt::AlignRight | Qt::AlignVCenter);
        return int(Qt::AlignLeft | Qt::AlignVCenter);
    case Qt::ForegroundRole:
        if (index.column() != Name)
            return QApplication::palette().color(QPalette::PlaceholderText);
        break;
    case Qt::ToolTipRole:
        if (index.column() == Name && n->encrypted && !n->isDir)
            return L("archive.encryptedTip");
        break;
    case PathRole: return n->path;
    case IsDirRole: return n->isDir;
    case EncryptedRole: return n->encrypted && !n->isDir;
    }
    return {};
}

QVariant ArchiveModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal)
        return {};
    if (role == Qt::DisplayRole) {
        switch (section) {
        case Name: return L("column.name");
        case Type: return L("column.type");
        case Size: return L("column.size");
        case Packed: return L("column.packed");
        case Date: return L("column.date");
        }
    }
    if (role == Qt::TextAlignmentRole && (section == Size || section == Packed || section == Date))
        return int(Qt::AlignRight | Qt::AlignVCenter);
    return {};
}

Qt::ItemFlags ArchiveModel::flags(const QModelIndex& index) const
{
    if (!index.isValid())
        return Qt::ItemIsDropEnabled;
    Qt::ItemFlags f = Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled;
    if (nodeOf(index)->isDir)
        f |= Qt::ItemIsDropEnabled;
    return f;
}

} // namespace packix::ui
