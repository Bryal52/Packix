#include "PathField.h"

#include "Icons.h"
#include "Lang.h"

#include <QCompleter>
#include <QDir>
#include <QFileDialog>
#include <QFileSystemModel>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMenu>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QToolButton>

namespace packix::ui {

namespace {

// "~/Загрузки" is shown with the tilde; typed paths may use it too.
QString expand(QString p)
{
    p = p.trimmed();
    if (p == QLatin1String("~") || p.startsWith(QLatin1String("~/")))
        p = QDir::homePath() + p.mid(1);
    return p.isEmpty() ? p : QDir::cleanPath(p);
}

QString shorten(const QString& p)
{
    const QString home = QDir::homePath();
    if (p == home)
        return QStringLiteral("~");
    if (p.startsWith(home + QLatin1Char('/')))
        return QLatin1Char('~') + p.mid(home.size());
    return p;
}

} // namespace

PathField::PathField(QWidget* parent) : QWidget(parent)
{
    m_edit = new QLineEdit(this);
    m_edit->setMinimumWidth(280);
    auto* fsModel = new QFileSystemModel(this);
    fsModel->setFilter(QDir::Dirs | QDir::NoDotAndDotDot);
    fsModel->setRootPath(QDir::rootPath());
    auto* completer = new QCompleter(fsModel, this);
    completer->setCompletionMode(QCompleter::PopupCompletion);
    m_edit->setCompleter(completer);

    m_more = new QToolButton(this);
    m_more->setText(QStringLiteral("⋯"));
    m_more->setToolTip(L("path.places"));
    m_more->setFocusPolicy(Qt::NoFocus);
    m_more->setAutoRaise(true);
    connect(m_more, &QToolButton::clicked, this, &PathField::showMenu);
    connect(m_edit, &QLineEdit::textChanged, this, &PathField::changed);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    layout->addWidget(m_edit, 1);
    layout->addWidget(m_more);
}

QString PathField::path() const
{
    return expand(m_edit->text());
}

void PathField::setPath(const QString& path)
{
    m_edit->setText(shorten(QDir::cleanPath(path)));
    m_edit->setCursorPosition(m_edit->text().size());
}

void PathField::addPlace(const QString& label, const QString& path)
{
    m_extra.append({label, path});
}

void PathField::showMenu()
{
    QMenu menu(this);
    auto add = [&](const QIcon& icon, const QString& label, const QString& path) {
        QAction* a = menu.addAction(icon, label);
        a->setToolTip(path);
        connect(a, &QAction::triggered, this, [this, path] { setPath(path); });
    };
    for (const auto& [label, path] : std::as_const(m_extra))
        add(fileIcon(FileCat::Folder), label, path);
    if (!m_extra.isEmpty())
        menu.addSeparator();

    add(placeIcon(QStringLiteral("home")), L("places.home"), QStandardPaths::writableLocation(QStandardPaths::HomeLocation));
    add(placeIcon(QStringLiteral("desktop")), L("places.desktop"), QStandardPaths::writableLocation(QStandardPaths::DesktopLocation));
    add(placeIcon(QStringLiteral("downloads")), L("places.downloads"), QStandardPaths::writableLocation(QStandardPaths::DownloadLocation));
    add(placeIcon(QStringLiteral("documents")), L("places.documents"), QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));

    bool first = true;
    for (const QStorageInfo& vol : QStorageInfo::mountedVolumes()) {
        if (!vol.isReady() || vol.isReadOnly())
            continue;
        const QString root = vol.rootPath();
#ifdef Q_OS_WIN
        const QString label = vol.name().isEmpty() ? L("places.drive", {{"letter", root.left(2)}})
                                                   : QStringLiteral("%1 (%2)").arg(vol.name(), root.left(2));
#else
        if (!root.startsWith(QLatin1String("/Volumes/")))
            continue;
        const QString label = vol.displayName();
#endif
        if (first) {
            menu.addSeparator();
            first = false;
        }
        add(placeIcon(QStringLiteral("volume")), label, root);
    }

    menu.addSeparator();
    QAction* browse = menu.addAction(L("path.browse"));
    connect(browse, &QAction::triggered, this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(window(), L("path.chooseFolder"), path());
        if (!dir.isEmpty())
            setPath(dir);
    });
    menu.exec(m_more->mapToGlobal(QPoint(0, m_more->height())));
}

} // namespace packix::ui
