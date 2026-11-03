#include "App.h"

#include "Actions.h"
#include "ArchiveWindow.h"
#include "Dialogs.h"
#include "Effects.h"
#include "Formats.h"
#include "Icons.h"
#include "Jobs.h"
#include "Lang.h"
#include "Platform.h"
#include "SettingsWindow.h"
#include "SevenZipEngine.h"
#include "Shared.h"
#include "Theme.h"

#include <QApplication>
#include <QCheckBox>
#include <QDesktopServices>
#include <QDialog>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileOpenEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

namespace packix::ui {

namespace {

bool isArchive(const QString& path)
{
    const QFileInfo fi(path);
    if (!fi.isFile())
        return false;
    if (Formats::instance().detect(fi.fileName()).canOpen)
        return true;
    // Parts: "Игра.7z.001", "Игра.r00", "Игра.z01"
    for (const VolumePattern& p : Formats::instance().volumePatterns()) {
        if (p.re.match(fi.fileName()).hasMatch())
            return true;
    }
    return false;
}

QLabel* logoLabel(int size, QWidget* parent)
{
    auto* l = new QLabel(parent);
    l->setPixmap(QApplication::windowIcon().pixmap(size, size));
    return l;
}

} // namespace

App::App(bool background)
{
    Q_UNUSED(background);
#ifdef Q_OS_MACOS
    m_tray = new QSystemTrayIcon(trayIcon(), this);   // single-colour, macOS tints it
#else
    m_tray = new QSystemTrayIcon(QApplication::windowIcon(), this);
#endif
    m_menu = new QMenu;
    connect(m_menu, &QMenu::aboutToShow, this, &App::rebuildMenu);
    m_tray->setContextMenu(m_menu);
    m_tray->setToolTip(QStringLiteral("Packix"));
    rebuildMenu();
    m_tray->show();
    qApp->installEventFilter(this);
    connect(&Jobs::instance(), &Jobs::changed, this, [this] { updateForeground(); });
}

bool App::eventFilter(QObject* watched, QEvent* event)
{
    switch (event->type()) {
    case QEvent::FileOpen:
        if (watched == qApp) {
            m_sawFile = true;
            const QString file = static_cast<QFileOpenEvent*>(event)->file();
            QTimer::singleShot(0, this, [this, file] { openArchive(file); });
            return true;
        }
        break;
    case QEvent::Show:
    case QEvent::Hide:
        if (auto* w = qobject_cast<QWidget*>(watched); w && w->isWindow())
            QTimer::singleShot(0, this, &App::updateForeground);
        break;
    default:
        break;
    }
    return QObject::eventFilter(watched, event);
}

void App::updateForeground()
{
    bool any = false;
    for (QWidget* w : QApplication::topLevelWidgets()) {
        const Qt::WindowType type = w->windowType();
        if (w->isVisible() && (type == Qt::Window || type == Qt::Dialog || type == Qt::Sheet))
            any = true;
    }
    if (any == m_foreground)
        return;
    m_foreground = any;
    platform::setForeground(any);
    if (any)
        platform::activate();
}

QString App::archiveFilter() const
{
    QStringList patterns;
    for (const Format& f : Formats::instance().all()) {
        if (!f.canOpen)
            continue;
        for (const QString& e : f.extensions)
            patterns << QStringLiteral("*.") + e;
    }
    patterns << QStringLiteral("*.001");
    return L("open.filter") + QStringLiteral(" (") + patterns.join(QLatin1Char(' ')) + QStringLiteral(");;")
        + L("open.allFiles") + QStringLiteral(" (*)");
}

void App::openArchive(const QString& path)
{
    m_sawFile = true;
    const QString abs = QFileInfo(path).absoluteFilePath();
    for (QWidget* w : QApplication::topLevelWidgets()) {
        auto* aw = qobject_cast<ArchiveWindow*>(w);
        if (aw && aw->isVisible() && aw->archivePath() == abs) {
            aw->raise();
            aw->activateWindow();
            platform::activate();
            return;
        }
    }
    if (!isArchive(abs) && QFileInfo(abs).exists()) {
        // Not an archive: it was handed over to be packed.
        archiveFiles({abs});
        return;
    }
    if (m_launcher)
        m_launcher->close();
    platform::activate();
    (new ArchiveWindow(abs))->start();
}

void App::request(const QString& mode, const QStringList& files)
{
    if (mode == QLatin1String("archive")) {
        for (const QString& f : files) {
            if (!m_pendingArchive.contains(f))
                m_pendingArchive << f;
        }
        if (!m_archiveBatch) {
            m_archiveBatch = new QTimer(this);
            m_archiveBatch->setSingleShot(true);
            connect(m_archiveBatch, &QTimer::timeout, this, [this] { archiveFiles(std::exchange(m_pendingArchive, {})); });
        }
        m_archiveBatch->start(400);   // the rest of the selection is still on its way
        return;
    }
    if (files.isEmpty()) {
        showLauncher();
        return;
    }
    for (const QString& f : files)
        openArchive(f);
}

void App::archiveFiles(const QStringList& files)
{
    m_sawFile = true;
    if (m_launcher)
        m_launcher->close();
    Actions::createArchive(files, nullptr);
}

void App::chooseAndOpen()
{
    platform::activate();
    const QStringList files = QFileDialog::getOpenFileNames(nullptr, L("open.title"),
                                                            QStandardPaths::writableLocation(QStandardPaths::DownloadLocation),
                                                            archiveFilter());
    for (const QString& f : files)
        openArchive(f);
}

void App::chooseAndCreate()
{
    platform::activate();
    QFileDialog dlg(nullptr, L("create.chooseTitle"), QStandardPaths::writableLocation(QStandardPaths::DesktopLocation));
    dlg.setFileMode(QFileDialog::ExistingFiles);
    dlg.setOption(QFileDialog::DontUseNativeDialog, false);
    dlg.setLabelText(QFileDialog::Accept, L("create.chooseButton"));
    if (dlg.exec() == QDialog::Accepted)
        archiveFiles(dlg.selectedFiles());
}

void App::rebuildMenu()
{
    m_menu->clear();
    m_menu->addAction(L("tray.open"), this, &App::chooseAndOpen);
    m_menu->addAction(L("tray.create"), this, &App::chooseAndCreate);

    const QList<Job*> jobs = Jobs::instance().running();
    if (!jobs.isEmpty()) {
        m_menu->addSeparator();
        for (Job* j : jobs) {
            const int pct = j->percent();
            const QString text = pct >= 0 ? L("tray.jobPercent", {{"title", j->title()}, {"percent", QString::number(pct)}})
                                          : L("tray.job", {{"title", j->title()}});
            QPointer<Job> job(j);
            m_menu->addAction(text, this, [job] {
                if (job)
                    job->showWindow();
            });
        }
    }

    m_menu->addSeparator();
    QAction* login = m_menu->addAction(L("tray.loginItem"));
    login->setCheckable(true);
    login->setChecked(platform::loginItemEnabled());
    connect(login, &QAction::toggled, this, [](bool on) { platform::setLoginItem(on); });

    Lang::instance().rescan();
    QMenu* lang = m_menu->addMenu(globeIcon(), L("tray.language"));
    for (const Lang::Info& info : Lang::instance().available()) {
        QAction* a = lang->addAction(info.name);
        a->setCheckable(true);
        a->setChecked(info.code == Lang::instance().current());
        a->setEnabled(info.error.isEmpty());
        const QString code = info.code;
        connect(a, &QAction::triggered, this, [code] { Lang::instance().setLanguage(code); });
    }
    m_menu->addAction(L("tray.settings"), this, &App::showSettings);
    m_menu->addAction(L("tray.about"), this, &App::showAbout);
    m_menu->addSeparator();
    m_menu->addAction(L("tray.quit"), qApp, &QApplication::quit);
}

void App::showLauncherIfIdle()
{
    if (m_sawFile)
        return;
    for (QWidget* w : QApplication::topLevelWidgets()) {
        if (w->isVisible() && w->isWindow())
            return;
    }
    showLauncher();
}

void App::showLauncher()
{
    if (m_launcher) {
        m_launcher->raise();
        m_launcher->activateWindow();
        return;
    }
    auto* w = new QWidget(nullptr, Qt::Window);
    w->setAttribute(Qt::WA_DeleteOnClose);
    w->setWindowTitle(QStringLiteral("Packix"));
    Theme::get().apply(w);
    auto* zone = new DropZone(w);
    zone->setText(L("launcher.drop"));
    zone->setMinimumSize(380, 200);
    connect(zone, &DropZone::filesDropped, this, [this](const QStringList& paths) {
        QStringList toPack;
        for (const QString& p : paths) {
            if (isArchive(p))
                openArchive(p);
            else
                toPack << p;
        }
        if (!toPack.isEmpty())
            archiveFiles(toPack);
    });
    auto* open = new QPushButton(L("tray.open"), w);
    auto* create = new QPushButton(L("tray.create"), w);
    connect(open, &QPushButton::clicked, this, &App::chooseAndOpen);
    connect(create, &QPushButton::clicked, this, &App::chooseAndCreate);
    auto* buttons = new QHBoxLayout;
    buttons->addStretch();
    buttons->addWidget(open);
    buttons->addWidget(create);
    buttons->addStretch();
    auto* layout = new QVBoxLayout(w);
    layout->setContentsMargins(14, 14, 14, 14);
    layout->addWidget(zone, 1);
    layout->addLayout(buttons);
    m_launcher = w;
    w->show();
    w->raise();
    w->activateWindow();
}

void App::showSettings()
{
    if (!m_settings)
        m_settings = new SettingsWindow;
    m_settings->show();
    m_settings->raise();
    m_settings->activateWindow();
}

void App::showAbout()
{
    QDialog dlg;
    dlg.setWindowTitle(L("about.windowTitle"));
    Theme::get().apply(&dlg);
    auto* name = new QLabel(QStringLiteral("Packix ") + QApplication::applicationVersion(), &dlg);
    QFont f = name->font();
    f.setBold(true);
    f.setPointSizeF(f.pointSizeF() + 4);
    name->setFont(f);
    const auto eng = engine();
    auto* text = new QLabel(L("about.text", {{"engine", eng->version()}}), &dlg);
    text->setWordWrap(true);
    text->setForegroundRole(QPalette::PlaceholderText);
    auto* license = new QPushButton(L("about.license"), &dlg);
    const QString licensePath = Shared::path(QStringLiteral("../7zip-License.txt"));
    license->setVisible(QFileInfo::exists(licensePath));
    connect(license, &QPushButton::clicked, &dlg, [licensePath] { QDesktopServices::openUrl(QUrl::fromLocalFile(licensePath)); });
    auto* ok = new QPushButton(L("common.ok"), &dlg);
    ok->setDefault(true);
    connect(ok, &QPushButton::clicked, &dlg, &QDialog::accept);
    auto* buttons = new QHBoxLayout;
    buttons->addWidget(license);
    buttons->addStretch();
    buttons->addWidget(ok);
    auto* texts = new QVBoxLayout;
    texts->addWidget(name);
    texts->addWidget(text);
    auto* head = new QHBoxLayout;
    head->addWidget(logoLabel(72, &dlg), 0, Qt::AlignTop);
    head->addLayout(texts, 1);
    auto* layout = new QVBoxLayout(&dlg);
    layout->setContentsMargins(20, 20, 20, 16);
    layout->addLayout(head);
    layout->addLayout(buttons);
    dlg.setMinimumWidth(440);
    platform::activate();
    dlg.exec();
}

void App::firstRunIfNeeded()
{
    QSettings settings;
    if (settings.value(QStringLiteral("firstRunDone")).toBool())
        return;
    QDialog dlg;
    dlg.setWindowTitle(L("welcome.windowTitle"));
    Theme::get().apply(&dlg);
    auto* title = new QLabel(L("welcome.title"), &dlg);
    QFont f = title->font();
    f.setBold(true);
    f.setPointSizeF(f.pointSizeF() + 3);
    title->setFont(f);
    auto* byDefault = new QCheckBox(L("welcome.default"), &dlg);
    auto* atLogin = new QCheckBox(L("welcome.login"), &dlg);
    byDefault->setChecked(true);
    atLogin->setChecked(true);
    auto* ok = new QPushButton(L("common.ok"), &dlg);
    ok->setDefault(true);
    ok->setStyleSheet(Theme::get().accentButtonStyle());
    connect(ok, &QPushButton::clicked, &dlg, &QDialog::accept);
    auto* texts = new QVBoxLayout;
    texts->addWidget(title);
    texts->addSpacing(6);
    texts->addWidget(byDefault);
    texts->addWidget(atLogin);
    auto* head = new QHBoxLayout;
    head->setSpacing(16);
    head->addWidget(logoLabel(64, &dlg), 0, Qt::AlignTop);
    head->addLayout(texts, 1);
    auto* buttons = new QHBoxLayout;
    buttons->addStretch();
    buttons->addWidget(ok);
    auto* layout = new QVBoxLayout(&dlg);
    layout->setContentsMargins(20, 20, 20, 16);
    layout->addLayout(head);
    layout->addLayout(buttons);
    platform::activate();
    dlg.exec();

    if (byDefault->isChecked())
        platform::makeDefaultArchiver();
    platform::setLoginItem(atLogin->isChecked());
    settings.setValue(QStringLiteral("firstRunDone"), true);
}

} // namespace packix::ui
