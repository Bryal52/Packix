#include "ArchiveWindow.h"

#include "Actions.h"
#include "ArchiveModel.h"
#include "Archiver.h"
#include "Dialogs.h"
#include "Effects.h"
#include "Formats.h"
#include "Icons.h"
#include "Jobs.h"
#include "Lang.h"
#include "LogViewer.h"
#include "Platform.h"
#include "Preview.h"
#include "Temp.h"
#include "Theme.h"

#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDir>
#include <QDrag>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QMimeData>
#include <QPainter>
#include <QPushButton>
#include <QShortcut>
#include <QStackedWidget>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QTreeView>
#include <QUrl>
#include <QVBoxLayout>

#include <functional>
#include <optional>

namespace packix::ui {

namespace {

// The book with the clasp, small and half transparent, at the right end of encrypted names.
class NameDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* p, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        const bool locked = index.column() == ArchiveModel::Name && index.data(ArchiveModel::EncryptedRole).toBool();
        QStyleOptionViewItem opt = option;
        const int side = Theme::get().metric("lockIconSize", 12);
        if (locked)
            opt.rect.setRight(opt.rect.right() - side - 8);
        QStyledItemDelegate::paint(p, opt, index);
        if (locked) {
            // Right after the name, not at the far end of the column.
            QStyleOptionViewItem o = opt;
            initStyleOption(&o, index);
            const QStyle* style = option.widget ? option.widget->style() : QApplication::style();
            const QRect text = style->subElementRect(QStyle::SE_ItemViewItemText, &o, option.widget);
            const int x = qMin(text.left() + o.fontMetrics.horizontalAdvance(o.text) + 8, option.rect.right() - side - 4);
            p->save();
            p->setOpacity(0.8);
            paintBookLock(*p, QRectF(x, option.rect.center().y() - side / 2.0, side, side), Theme::get().color("accent"), 0);
            p->restore();
        }
    }

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        QSize s = QStyledItemDelegate::sizeHint(option, index);
        s.setHeight(qMax(s.height(), Theme::get().metric("rowHeight", 26)));
        return s;
    }
};

// Unique name among the names already in a folder of the archive: "a (1).txt".
QString uniqueAmong(const QStringList& taken, const QString& name)
{
    if (!taken.contains(name, Qt::CaseInsensitive))
        return name;
    const auto [base, ext] = Formats::instance().splitName(name);
    for (int n = 1;; ++n) {
        const QString candidate = QStringLiteral("%1 (%2)").arg(base).arg(n) + (ext.isEmpty() ? QString() : QLatin1Char('.') + ext);
        if (!taken.contains(candidate, Qt::CaseInsensitive))
            return candidate;
    }
}

bool copyTree(const QString& from, const QString& to)
{
    const QFileInfo fi(from);
    if (!fi.isDir() || fi.isSymLink())
        return QFile::copy(from, to);
    if (!QDir().mkpath(to))
        return false;
    for (const QString& n : QDir(from).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System)) {
        if (!copyTree(QDir(from).filePath(n), QDir(to).filePath(n)))
            return false;
    }
    return true;
}

} // namespace

// ---------------------------------------------------------------------------

class ArchiveView : public QTreeView {
public:
    explicit ArchiveView(ArchiveWindow* owner) : QTreeView(owner), m_owner(owner)
    {
        setAcceptDrops(true);
        setDragEnabled(true);
        setDragDropMode(QAbstractItemView::DragDrop);
        setDropIndicatorShown(true);
    }

protected:
    void startDrag(Qt::DropActions) override { m_owner->dragOut(); }

    void dragEnterEvent(QDragEnterEvent* e) override
    {
        if (e->source() != this && e->mimeData()->hasUrls())
            e->acceptProposedAction();
        else
            e->ignore();
    }
    void dragMoveEvent(QDragMoveEvent* e) override
    {
        if (e->source() == this || !e->mimeData()->hasUrls()) {
            e->ignore();
            return;
        }
        QTreeView::dragMoveEvent(e);
        e->setDropAction(Qt::CopyAction);
        e->accept();
    }
    void dropEvent(QDropEvent* e) override
    {
        QStringList paths;
        for (const QUrl& u : e->mimeData()->urls()) {
            if (u.isLocalFile())
                paths << u.toLocalFile();
        }
        // Dropped on a folder: into it; on a file: next to it.
        const QModelIndex at = indexAt(e->position().toPoint());
        QString folder;
        if (at.isValid()) {
            const auto* m = static_cast<ArchiveModel*>(model());
            const QString p = m->pathOf(at);
            folder = m->isDir(at) ? p : p.left(qMax<qsizetype>(0, p.lastIndexOf(QLatin1Char('/'))));
        }
        e->setDropAction(Qt::CopyAction);
        e->accept();
        QTimer::singleShot(0, m_owner, [owner = m_owner, paths, folder] { owner->dropIn(paths, folder); });
    }
    void keyPressEvent(QKeyEvent* e) override
    {
        if (e->key() == Qt::Key_Space && e->modifiers() == Qt::NoModifier) {
            m_owner->quickLook();
            return;
        }
        if ((e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) && currentIndex().isValid()) {
            m_owner->openItem(currentIndex());
            return;
        }
        // ⌫ / Delete in the list removes the selection (asks first).
        if ((e->key() == Qt::Key_Backspace || e->key() == Qt::Key_Delete)
            && (e->modifiers() & ~(Qt::KeypadModifier | Qt::ControlModifier)) == Qt::NoModifier) {
            m_owner->deleteSelected();
            return;
        }
        QTreeView::keyPressEvent(e);
    }

private:
    ArchiveWindow* m_owner;
};

// ---------------------------------------------------------------------------

ArchiveWindow::ArchiveWindow(const QString& path, Origin origin)
    : QWidget(nullptr, Qt::Window), m_path(QFileInfo(path).absoluteFilePath()), m_origin(std::move(origin))
{
    setAttribute(Qt::WA_DeleteOnClose);
    resize(860, 540);
    Theme::get().apply(this);
    buildUi();
    m_watcher = new QFileSystemWatcher(this);
    connect(m_watcher, &QFileSystemWatcher::fileChanged, this, &ArchiveWindow::fileEdited);
    connect(&Lang::instance(), &Lang::changed, this, &ArchiveWindow::retranslate);
    retranslate();
}

ArchiveWindow::~ArchiveWindow()
{
    for (const QString& d : std::as_const(m_tempDirs))
        QDir(d).removeRecursively();
}

void ArchiveWindow::buildUi()
{
    m_extract = new QPushButton(this);
    m_extract->setStyleSheet(Theme::get().accentButtonStyle());
    m_delete = new QPushButton(this);
    m_passwordBtn = new QPushButton(this);
    connect(m_extract, &QPushButton::clicked, this, &ArchiveWindow::extractSelected);
    connect(m_delete, &QPushButton::clicked, this, &ArchiveWindow::deleteSelected);

    m_lockIcon = new QLabel(this);
    m_lockText = new QLabel(this);
    m_lockText->setForegroundRole(QPalette::PlaceholderText);
    m_search = new QLineEdit(this);
    m_search->setClearButtonEnabled(true);
    m_search->setFixedWidth(220);

    auto* top = new QHBoxLayout;
    top->setContentsMargins(10, 8, 10, 8);
    top->setSpacing(8);
    top->addWidget(m_extract);
    top->addWidget(m_delete);
    top->addWidget(m_passwordBtn);
    top->addStretch();
    top->addWidget(m_lockIcon);
    top->addWidget(m_lockText);
    top->addSpacing(8);
    top->addWidget(m_search);

    // Contents
    m_model = new ArchiveModel(this);
    m_view = new ArchiveView(this);
    m_view->setModel(m_model);
    m_view->setItemDelegate(new NameDelegate(m_view));
    m_view->setUniformRowHeights(true);
    m_view->setAllColumnsShowFocus(true);
    m_view->setAlternatingRowColors(false);
    m_view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_view->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_view->setIconSize(QSize(Theme::get().metric("fileIconSize", 20), Theme::get().metric("fileIconSize", 20)));
    m_view->setFrameShape(QFrame::NoFrame);
    m_view->setSortingEnabled(true);
    m_view->sortByColumn(ArchiveModel::Name, Qt::AscendingOrder);
    m_view->setAnimated(Theme::get().motion("treeMs", 0) > 0 && !Theme::reduceMotion());
    QHeaderView* header = m_view->header();
    header->setStretchLastSection(false);
    header->setSectionResizeMode(ArchiveModel::Name, QHeaderView::Stretch);
    header->resizeSection(ArchiveModel::Type, 170);
    header->resizeSection(ArchiveModel::Size, 90);
    header->resizeSection(ArchiveModel::Packed, 90);
    header->resizeSection(ArchiveModel::Date, 128);
    header->setSectionsMovable(false);
    connect(m_view, &QTreeView::doubleClicked, this, &ArchiveWindow::openItem);

    // Photo and video preview on hover, as in Folix.
    m_view->setMouseTracking(true);
    m_view->viewport()->installEventFilter(this);
    m_view->installEventFilter(this);
    m_hoverTimer = new QTimer(this);
    m_hoverTimer->setSingleShot(true);
    m_hoverTimer->setInterval(Theme::get().motion("previewDelayMs", 500));
    connect(m_hoverTimer, &QTimer::timeout, this, [this] {
        if (!m_hoverIndex.isValid() || !isActiveWindow())
            return;
        const Item item = m_model->itemAt(m_hoverIndex);
        if (PreviewPopup::wants(item.path.section(QLatin1Char('/'), -1), item.isDir))
            PreviewPopup::instance()->showFor(m_info, m_password, item, QCursor::pos());
    });
    connect(m_view->selectionModel(), &QItemSelectionModel::selectionChanged, this, &ArchiveWindow::updateState);
    connect(m_search, &QLineEdit::textChanged, this, [this](const QString& t) {
        m_model->setFilter(t);
        if (!t.trimmed().isEmpty())
            m_view->expandAll();
        updateStatus();
    });

    // Encrypted names: big book, password field.
    auto* locked = new QWidget(this);
    m_lockedBook = new BookLockWidget(96, locked);
    m_lockedTitle = new QLabel(locked);
    QFont tf = m_lockedTitle->font();
    tf.setBold(true);
    tf.setPointSizeF(tf.pointSizeF() + 3);
    m_lockedTitle->setFont(tf);
    m_lockedTitle->setAlignment(Qt::AlignCenter);
    m_lockedText = new QLabel(locked);
    m_lockedText->setAlignment(Qt::AlignCenter);
    m_lockedText->setWordWrap(true);
    m_lockedText->setForegroundRole(QPalette::PlaceholderText);
    m_lockedEdit = new QLineEdit(locked);
    m_lockedEdit->setEchoMode(QLineEdit::Password);
    m_lockedEdit->setFixedWidth(300);
    m_lockedError = new QLabel(locked);
    QPalette ep = m_lockedError->palette();
    ep.setColor(QPalette::WindowText, Theme::get().color("warning"));
    m_lockedError->setPalette(ep);
    m_lockedError->hide();
    m_lockedRemember = new QCheckBox(locked);
    m_lockedOpen = new QPushButton(locked);
    m_lockedOpen->setStyleSheet(Theme::get().accentButtonStyle());
    m_lockedOpen->setDefault(true);
    connect(m_lockedOpen, &QPushButton::clicked, this, &ArchiveWindow::tryLockedPassword);
    connect(m_lockedEdit, &QLineEdit::returnPressed, this, &ArchiveWindow::tryLockedPassword);
    connect(m_lockedEdit, &QLineEdit::textChanged, m_lockedError, &QWidget::hide);
    auto* lv = new QVBoxLayout(locked);
    lv->addStretch(2);
    lv->addWidget(m_lockedBook, 0, Qt::AlignHCenter);
    lv->addSpacing(10);
    lv->addWidget(m_lockedTitle);
    lv->addWidget(m_lockedText);
    lv->addSpacing(8);
    lv->addWidget(m_lockedEdit, 0, Qt::AlignHCenter);
    lv->addWidget(m_lockedError, 0, Qt::AlignHCenter);
    lv->addWidget(m_lockedRemember, 0, Qt::AlignHCenter);
    lv->addWidget(m_lockedOpen, 0, Qt::AlignHCenter);
    lv->addStretch(3);

    m_empty = new DropZone(this);
    connect(m_empty, &DropZone::filesDropped, this, [this](const QStringList& paths) { dropIn(paths, QString()); });

    m_stack = new QStackedWidget(this);
    m_stack->addWidget(m_view);
    m_stack->addWidget(locked);
    m_stack->addWidget(m_empty);

    // Status line, log button in the bottom-right corner.
    m_statusLeft = new QLabel(this);
    m_statusRight = new QLabel(this);
    for (QLabel* l : {m_statusLeft, m_statusRight}) {
        l->setForegroundRole(QPalette::PlaceholderText);
        QFont f = l->font();
        f.setPointSizeF(f.pointSizeF() - 1);
        l->setFont(f);
    }
    auto* bottom = new QHBoxLayout;
    bottom->setContentsMargins(10, 3, 4, 4);
    bottom->addWidget(m_statusLeft);
    bottom->addStretch();
    bottom->addWidget(m_statusRight);
    bottom->addWidget(new LogButton(this));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addLayout(top);
    layout->addWidget(m_stack, 1);
    layout->addLayout(bottom);

    // Keys: ⌘E unpack, ⌫ / ⌘⌫ delete, ⌘F search, Esc clears the search, then closes.
    auto shortcut = [this](const QKeySequence& k, auto fn) {
        auto* s = new QShortcut(k, this);
        connect(s, &QShortcut::activated, this, fn);
    };
    shortcut(QKeySequence(Qt::CTRL | Qt::Key_E), [this] { extractSelected(); });
    shortcut(QKeySequence(Qt::CTRL | Qt::Key_Backspace), [this] { deleteSelected(); });
    shortcut(QKeySequence::Find, [this] {
        m_search->setFocus();
        m_search->selectAll();
    });
    shortcut(QKeySequence(Qt::Key_Escape), [this] {
        if (!m_search->text().isEmpty()) {
            m_search->clear();
            m_view->setFocus();
        } else {
            close();
        }
    });
}

bool ArchiveWindow::eventFilter(QObject* watched, QEvent* event)
{
    switch (event->type()) {
    case QEvent::MouseMove:
        if (watched == m_view->viewport()) {
            const QModelIndex at = m_view->indexAt(static_cast<QMouseEvent*>(event)->position().toPoint());
            const QModelIndex row = at.isValid() ? at.siblingAtColumn(0) : QModelIndex();
            if (row != m_hoverIndex) {
                PreviewPopup::instance()->hidePopup();
                m_hoverIndex = row;
                if (row.isValid())
                    m_hoverTimer->start();
                else
                    m_hoverTimer->stop();
            }
        }
        break;
    case QEvent::Leave:
    case QEvent::MouseButtonPress:
    case QEvent::Wheel:
    case QEvent::KeyPress:
    case QEvent::FocusOut:
        m_hoverTimer->stop();
        m_hoverIndex = QModelIndex();
        PreviewPopup::instance()->hidePopup();
        break;
    default:
        break;
    }
    return QWidget::eventFilter(watched, event);
}

void ArchiveWindow::retranslate()
{
    QString title = QFileInfo(m_path).fileName();
    if (m_origin.window)
        title = m_origin.window->windowTitle() + QStringLiteral(" › ") + title;
    setWindowTitle(title);
    m_extract->setText(L("archive.extract"));
    m_extract->setToolTip(platform::keys(L("archive.extractTip")));
    m_delete->setText(L("archive.delete"));
    m_search->setPlaceholderText(L("archive.search"));
    m_lockText->setText(L("archive.protected"));
    m_lockedTitle->setText(L("password.title", {{"name", QFileInfo(m_path).fileName()}}));
    m_lockedText->setText(L("archive.namesEncrypted"));
    m_lockedEdit->setPlaceholderText(L("password.placeholder"));
    m_lockedRemember->setText(L("password.remember"));
    m_lockedOpen->setText(L("password.open"));
    m_empty->setText(L("archive.empty"));
    m_model->retranslate();
    updateState();
}

bool ArchiveWindow::start()
{
    QPointer<ArchiveWindow> self(this);
    ArchiveInfo info;
    const QString path = m_path;
    Status st = Dialogs::runAsync([&info, path] { return Archiver::open(path, {}, info); });
    if (!self)
        return false;
    if (st.error == Error::NeedPassword) {
        // Names are encrypted: saved passwords first, quietly.
        for (const QString& pw : platform::savedPasswords()) {
            st = Dialogs::runAsync([&info, path, pw] { return Archiver::open(path, pw, info); });
            if (st.ok()) {
                m_password = pw;
                break;
            }
        }
        if (!st.ok()) {
            showLocked();
            show();
            raise();
            activateWindow();
            return true;
        }
    }
    if (!st.ok()) {
        Dialogs::showError(nullptr, st, QFileInfo(m_path).fileName());
        deleteLater();
        return false;
    }
    m_info = info;
    showContents();
    show();
    raise();
    activateWindow();
    return true;
}

void ArchiveWindow::showLocked()
{
    m_stack->setCurrentIndex(1);
    m_lockedBook->setOpen(false);
    m_lockedEdit->setFocus();
    updateState();
}

void ArchiveWindow::tryLockedPassword()
{
    const QString pw = m_lockedEdit->text();
    if (pw.isEmpty())
        return;
    m_lockedOpen->setEnabled(false);
    ArchiveInfo info;
    const QString path = m_path;
    const Status st = Dialogs::runAsync([&info, path, pw] { return Archiver::open(path, pw, info); });
    m_lockedOpen->setEnabled(true);
    if (st.error == Error::WrongPassword || st.error == Error::NeedPassword) {
        m_lockedEdit->clear();
        m_lockedError->setText(L("password.wrong"));
        m_lockedError->show();
        shake(this);
        return;
    }
    if (!st.ok()) {
        Dialogs::showError(this, st, QFileInfo(m_path).fileName());
        return;
    }
    m_password = pw;
    m_info = info;
    if (m_lockedRemember->isChecked())
        platform::savePassword(pw);
    m_lockedBook->animateOpen([this] { showContents(); });
}

void ArchiveWindow::showContents()
{
    m_model->setArchive(m_info);
    // One folder at the top (the usual case): open it right away.
    if (m_model->rowCount() == 1)
        m_view->expand(m_model->index(0, 0));
    m_stack->setCurrentIndex(m_info.items.isEmpty() ? 2 : 0);
    m_view->setFocus();
    updateState();
}

bool ArchiveWindow::reload()
{
    ArchiveInfo info;
    const QString path = m_path;
    const QString pw = m_password;
    const Status st = Dialogs::runAsync([&info, path, pw] { return Archiver::open(path, pw, info); });
    if (!st.ok()) {
        Dialogs::showError(this, st, QFileInfo(m_path).fileName());
        return false;
    }
    // Keep open folders open.
    QStringList expanded;
    std::function<void(const QModelIndex&)> collect = [&](const QModelIndex& parent) {
        for (int r = 0; r < m_model->rowCount(parent); ++r) {
            const QModelIndex i = m_model->index(r, 0, parent);
            if (m_view->isExpanded(i)) {
                expanded << m_model->pathOf(i);
                collect(i);
            }
        }
    };
    collect({});
    m_info = info;
    m_model->setArchive(m_info);
    for (const QString& p : std::as_const(expanded))
        m_view->expand(m_model->indexOfPath(p));
    m_stack->setCurrentIndex(m_info.items.isEmpty() ? 2 : 0);
    updateState();
    return true;
}

void ArchiveWindow::updateState()
{
    const bool loaded = m_stack->currentIndex() != 1;
    const bool modifiable = loaded && Archiver::canModify(m_info) && !m_busy;
    const QStringList sel = selectedPaths();
    m_extract->setEnabled(loaded && !m_info.items.isEmpty());
    m_extract->setText(sel.isEmpty() ? L("archive.extract") : L("archive.extractSelected"));
    m_delete->setEnabled(modifiable && !sel.isEmpty());
    m_delete->setToolTip(Archiver::canModify(m_info) || !loaded
                             ? platform::keys(L("archive.deleteTip"))
                             : (m_info.volumes.size() > 1 ? L("archive.readOnlyVolumes") : L("archive.readOnlyFormat")));

    // Password: set one, or a pull-down to change / remove the existing one.
    QMenu* menu = m_passwordBtn->menu();
    m_passwordBtn->disconnect(this);
    const bool canRepack = loaded && m_info.volumes.size() <= 1 && !m_busy;
    if (m_info.anyEncrypted) {
        if (!menu)
            menu = new QMenu(m_passwordBtn);
        menu->clear();
        menu->addAction(L("archive.changePassword"), this, &ArchiveWindow::changePassword);
        menu->addAction(L("archive.removePassword"), this, &ArchiveWindow::removePassword);
        m_passwordBtn->setMenu(menu);
        m_passwordBtn->setText(L("archive.passwordMenu"));
    } else {
        m_passwordBtn->setMenu(nullptr);
        delete menu;
        m_passwordBtn->setText(L("archive.setPassword"));
        connect(m_passwordBtn, &QPushButton::clicked, this, &ArchiveWindow::setPassword);
    }
    m_passwordBtn->setEnabled(canRepack);

    const bool protectedArchive = m_info.anyEncrypted || m_stack->currentIndex() == 1;
    const int side = 18;
    m_lockIcon->setPixmap(bookLockIcon(!m_password.isEmpty()).pixmap(side, side));
    m_lockIcon->setVisible(protectedArchive);
    m_lockText->setVisible(protectedArchive);
    m_search->setEnabled(loaded);
    updateStatus();
}

void ArchiveWindow::updateStatus()
{
    if (m_stack->currentIndex() == 1) {
        m_statusLeft->clear();
        m_statusRight->clear();
        return;
    }
    QString left = Ln("files.count", m_info.fileCount()) + QStringLiteral(" · ") + humanSize(m_info.totalSize());
    if (m_info.volumes.size() > 1)
        left += QStringLiteral(" · ") + Ln("archive.volumes", m_info.volumes.size());
    m_statusLeft->setText(left);

    const QModelIndexList rows = m_view->selectionModel()->selectedRows();
    qint64 size = 0;
    for (const QModelIndex& i : rows)
        size += m_model->sizeOf(i);
    m_statusRight->setText(rows.isEmpty() ? QString()
                                          : L("archive.selected", {{"count", QString::number(rows.size())}, {"size", humanSize(size)}}));
}

QStringList ArchiveWindow::selectedPaths() const
{
    QStringList all;
    for (const QModelIndex& i : m_view->selectionModel()->selectedRows())
        all << m_model->pathOf(i);
    // A folder already brings what is inside it.
    QStringList out;
    for (const QString& p : std::as_const(all)) {
        bool covered = false;
        for (const QString& q : std::as_const(all))
            covered = covered || (p != q && p.startsWith(q + QLatin1Char('/')));
        if (!covered)
            out << p;
    }
    return out;
}

QString ArchiveWindow::unpackToTemp(const QStringList& items, const QString& prefix)
{
    if (!Actions::ensurePassword(this, m_info, &m_password))
        return {};
    const QString dir = Temp::newDir(prefix);
    m_tempDirs << dir;
    const ArchiveInfo info = m_info;
    const QString pw = m_password;
    QApplication::setOverrideCursor(Qt::BusyCursor);
    // Raw unpack keeps the paths from the archive (the views need them to put files back).
    const Status st = Dialogs::runAsync([info, dir, items, pw] {
        return engine()->extract(info.path, Archiver::formatOf(info), dir, items, pw, nullptr);
    });
    QApplication::restoreOverrideCursor();
    if (!st.ok()) {
        Dialogs::showError(this, st, QFileInfo(m_path).fileName());
        return {};
    }
    return dir;
}

// ---------------------------------------------------------------------------

void ArchiveWindow::extractSelected()
{
    if (m_stack->currentIndex() == 1 || m_info.items.isEmpty())
        return;
    Actions::extract(this, m_info, selectedPaths(), &m_password);
}

void ArchiveWindow::deleteSelected()
{
    const QStringList items = selectedPaths();
    if (items.isEmpty() || m_busy || !Archiver::canModify(m_info))
        return;
    const QString text = items.size() == 1 ? L("delete.textOne", {{"name", items.first().section(QLatin1Char('/'), -1)}})
                                           : Ln("delete.textMany", items.size());
    if (!Dialogs::confirm(this, L("delete.title"), text, L("delete.button"), true))
        return;
    m_busy = true;
    updateState();
    JobSpec spec;
    const QString name = QFileInfo(m_path).fileName();
    spec.title = L("job.deleting", {{"name", name}});
    spec.archiveName = name;
    const ArchiveInfo info = m_info;
    const QString pw = m_password;
    spec.work = [info, items, pw](Progress& progress, QString&) { return Archiver::remove(info, items, pw, &progress); };
    QPointer<ArchiveWindow> self(this);
    spec.done = [self](const Status& st, const QString&) {
        if (!self)
            return;
        self->m_busy = false;
        if (st.ok())
            self->afterChange();
        else
            self->updateState();
    };
    Jobs::instance().start(spec, this);
}

void ArchiveWindow::setPassword()
{
    Dialogs::NewPassword np;
    if (!Dialogs::askNewPassword(this, QFileInfo(m_path).fileName(), m_info.formatId, false, &np))
        return;
    runPasswordChange(np.password, np.targetFormatId);
}

void ArchiveWindow::changePassword()
{
    if (!Actions::ensurePassword(this, m_info, &m_password))
        return;
    Dialogs::NewPassword np;
    if (!Dialogs::askNewPassword(this, QFileInfo(m_path).fileName(), m_info.formatId, true, &np))
        return;
    runPasswordChange(np.password, np.targetFormatId);
}

void ArchiveWindow::removePassword()
{
    if (!Actions::ensurePassword(this, m_info, &m_password))
        return;
    if (!Dialogs::confirm(this, L("removePassword.title"), L("removePassword.text", {{"name", QFileInfo(m_path).fileName()}}),
                          L("removePassword.button")))
        return;
    runPasswordChange(QString(), QString());
}

void ArchiveWindow::runPasswordChange(const QString& newPassword, const QString& targetFormat)
{
    m_busy = true;
    updateState();
    Archiver::PasswordRequest req;
    req.oldPassword = m_password;
    req.newPassword = newPassword;
    req.targetFormatId = targetFormat;
    JobSpec spec;
    const QString name = QFileInfo(m_path).fileName();
    spec.title = newPassword.isEmpty() ? L("job.removingPassword", {{"name", name}}) : L("job.settingPassword", {{"name", name}});
    spec.archiveName = name;
    const ArchiveInfo info = m_info;
    spec.work = [info, req](Progress& progress, QString& result) {
        progress.setTotalBytes(info.totalSize());
        return Archiver::changePassword(info, req, &progress, &result);
    };
    QPointer<ArchiveWindow> self(this);
    spec.done = [self, newPassword](const Status& st, const QString& result) {
        if (!self)
            return;
        self->m_busy = false;
        if (!st.ok()) {
            self->updateState();
            return;
        }
        self->m_password = newPassword;
        if (!result.isEmpty() && result != self->m_path) {   // became a .zip / .7z next to it
            self->m_path = result;
            self->retranslate();
        }
        self->afterChange();
    };
    Jobs::instance().start(spec, this);
}

void ArchiveWindow::afterChange()
{
    reload();
    if (m_origin.window)
        m_origin.window->childChanged(m_origin.workDir, m_origin.relPath);
}

void ArchiveWindow::childChanged(const QString& workDir, const QString& relPath)
{
    const QString name = QFileInfo(m_path).fileName();
    if (!Archiver::canModify(m_info)) {
        Dialogs::info(this, L("nested.readOnlyTitle"), L("nested.readOnlyText", {{"name", name}}));
        return;
    }
    if (!Dialogs::confirm(this, L("nested.saveTitle"), L("nested.saveText", {{"name", name}}), L("nested.saveButton")))
        return;
    JobSpec spec;
    spec.title = L("job.updating", {{"name", name}});
    spec.archiveName = name;
    const ArchiveInfo info = m_info;
    const QString pw = m_password;
    spec.work = [info, workDir, relPath, pw](Progress& progress, QString&) {
        return Archiver::update(info, workDir, {relPath}, pw, &progress);
    };
    QPointer<ArchiveWindow> self(this);
    spec.done = [self](const Status& st, const QString&) {
        if (self && st.ok())
            self->afterChange();
    };
    Jobs::instance().start(spec, this);
}

// ---------------------------------------------------------------------------

void ArchiveWindow::openItem(const QModelIndex& index)
{
    if (!index.isValid() || m_model->isDir(index))
        return;   // folders open with the arrow
    const QString rel = m_model->pathOf(index);
    const QString dir = unpackToTemp({rel}, QStringLiteral("open"));
    if (dir.isEmpty())
        return;
    const QString file = QDir(dir).filePath(rel);

    // An archive inside the archive: its own window.
    const Format f = Formats::instance().detect(QFileInfo(rel).fileName());
    if (f.isValid() && f.canOpen) {
        Origin origin{this, dir, rel};
        auto* w = new ArchiveWindow(file, origin);
        w->start();
        return;
    }
    m_opened.insert(file, {dir, rel});
    m_watcher->addPath(file);
    QDesktopServices::openUrl(QUrl::fromLocalFile(file));
}

void ArchiveWindow::fileEdited(const QString& file)
{
    // Editors often save by replacing the file; watch the new one again.
    if (QFileInfo::exists(file) && !m_watcher->files().contains(file))
        m_watcher->addPath(file);
    if (m_pendingEdits.contains(file) || !m_opened.contains(file))
        return;
    m_pendingEdits.insert(file);
    QTimer::singleShot(700, this, [this, file] {
        m_pendingEdits.remove(file);
        const auto [dir, rel] = m_opened.value(file);
        const QString name = QFileInfo(file).fileName();
        if (!Archiver::canModify(m_info) || m_busy)
            return;
        platform::activate();
        if (!Dialogs::confirm(this, L("edited.title"), L("edited.text", {{"name", name}}), L("edited.button")))
            return;
        JobSpec spec;
        spec.title = L("job.updating", {{"name", QFileInfo(m_path).fileName()}});
        spec.archiveName = QFileInfo(m_path).fileName();
        const ArchiveInfo info = m_info;
        const QString pw = m_password;
        spec.work = [info, dir, rel, pw](Progress& progress, QString&) { return Archiver::update(info, dir, {rel}, pw, &progress); };
        QPointer<ArchiveWindow> self(this);
        spec.done = [self](const Status& st, const QString&) {
            if (self && st.ok())
                self->afterChange();
        };
        Jobs::instance().start(spec, this);
    });
}

void ArchiveWindow::quickLook()
{
    QStringList files;
    for (const QString& p : selectedPaths()) {
        const QModelIndex i = m_model->indexOfPath(p);
        if (!m_model->isDir(i))
            files << p;
    }
    if (files.isEmpty())
        return;
    const QString dir = unpackToTemp(files, QStringLiteral("look"));
    if (dir.isEmpty())
        return;
    QStringList paths;
    for (const QString& f : std::as_const(files))
        paths << QDir(dir).filePath(f);
    platform::quickLook(paths);
}

void ArchiveWindow::dragOut()
{
    const QStringList items = selectedPaths();
    if (items.isEmpty())
        return;
    const QString dir = unpackToTemp(items, QStringLiteral("drag"));
    if (dir.isEmpty())
        return;
    QList<QUrl> urls;
    for (const QString& i : items) {
        const QString p = QDir(dir).filePath(i);
        if (QFileInfo::exists(p))
            urls << QUrl::fromLocalFile(p);
    }
    auto* mime = new QMimeData;
    mime->setUrls(urls);
    auto* drag = new QDrag(m_view);
    drag->setMimeData(mime);
    const QModelIndex first = m_model->indexOfPath(items.first());
    drag->setPixmap(m_model->data(first, Qt::DecorationRole).value<QIcon>().pixmap(32, 32));
    drag->exec(Qt::CopyAction);
}

void ArchiveWindow::dropIn(const QStringList& paths, const QString& folder)
{
    if (paths.isEmpty())
        return;
    if (!Archiver::canModify(m_info)) {
        Dialogs::info(this, L("error.readOnly.title"),
                      m_info.volumes.size() > 1 ? L("archive.readOnlyVolumes") : L("archive.readOnlyFormat"));
        return;
    }
    if (m_busy)
        return;
    if (m_info.anyEncrypted && !Actions::ensurePassword(this, m_info, &m_password))
        return;

    // Names already in that folder of the archive: ask like everywhere else.
    QStringList taken = m_model->childNames(folder);
    QStringList direct;
    QString renamedDir;
    std::optional<Conflict> all;
    for (const QString& p : paths) {
        const QString name = QFileInfo(p).fileName();
        if (!taken.contains(name, Qt::CaseInsensitive)) {
            direct << p;
            taken << name;
            continue;
        }
        Conflict c;
        if (all) {
            c = *all;
        } else {
            const Dialogs::ConflictAnswer a = Dialogs::askConflict(this, name, true);
            c = a.choice;
            if (a.applyToAll && c != Conflict::Cancel)
                all = c;
        }
        if (c == Conflict::Cancel)
            return;
        if (c == Conflict::Skip)
            continue;
        if (c == Conflict::Replace) {
            direct << p;
            continue;
        }
        // Keep both: a renamed copy goes in.
        if (renamedDir.isEmpty()) {
            renamedDir = Temp::newDir(QStringLiteral("add"));
            m_tempDirs << renamedDir;
        }
        const QString newName = uniqueAmong(taken, name);
        taken << newName;
        const QString copy = QDir(renamedDir).filePath(newName);
        if (!copyTree(p, copy)) {
            Dialogs::showError(this, Status::fail(Error::NoAccess, p), QFileInfo(m_path).fileName());
            return;
        }
        direct << copy;
    }
    if (direct.isEmpty())
        return;

    m_busy = true;
    updateState();
    JobSpec spec;
    const QString name = QFileInfo(m_path).fileName();
    spec.title = L("job.adding", {{"name", name}});
    spec.archiveName = name;
    const ArchiveInfo info = m_info;
    const QString pw = m_password;
    spec.work = [info, direct, folder, pw](Progress& progress, QString&) {
        progress.setTotalBytes(Archiver::diskSize(direct));
        return Archiver::add(info, direct, folder, pw, &progress);
    };
    QPointer<ArchiveWindow> self(this);
    spec.done = [self](const Status& st, const QString&) {
        if (!self)
            return;
        self->m_busy = false;
        if (st.ok())
            self->afterChange();
        else
            self->updateState();
    };
    Jobs::instance().start(spec, this);
}

} // namespace packix::ui
