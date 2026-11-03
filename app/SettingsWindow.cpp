#include "SettingsWindow.h"

#include "Icons.h"
#include "Lang.h"
#include "LogViewer.h"
#include "Platform.h"
#include "Theme.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDir>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QStandardItemModel>
#include <QTimer>
#include <QVBoxLayout>

namespace packix::ui {

namespace {

QFrame* line(QWidget* parent)
{
    auto* f = new QFrame(parent);
    f->setFrameShape(QFrame::HLine);
    f->setFrameShadow(QFrame::Plain);
    f->setForegroundRole(QPalette::Mid);
    return f;
}

QLabel* sectionTitle(QWidget* parent)
{
    auto* l = new QLabel(parent);
    QFont f = l->font();
    f.setBold(true);
    l->setFont(f);
    return l;
}

} // namespace

SettingsWindow::SettingsWindow() : QWidget(nullptr, Qt::Window)
{
    setAttribute(Qt::WA_DeleteOnClose);
    Theme::get().apply(this);

    // Language: globe, list, and where the files live.
    auto* globe = new QLabel(this);
    globe->setPixmap(globeIcon().pixmap(28, 28));
    m_langTitle = sectionTitle(this);
    m_lang = new QComboBox(this);
    m_lang->setMinimumWidth(200);
    m_langPath = new QLabel(this);
    m_langPath->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_langPath->setForegroundRole(QPalette::PlaceholderText);
    m_langPath->setWordWrap(true);
    m_reveal = new QPushButton(this);
    m_copy = new QPushButton(this);
    connect(m_reveal, &QPushButton::clicked, this, [] {
        const QString dir = Lang::instance().userDir();
        QDir().mkpath(dir);
        platform::reveal(dir);
    });
    connect(m_copy, &QPushButton::clicked, this, [] { QApplication::clipboard()->setText(Lang::instance().userDir()); });
    connect(m_lang, &QComboBox::activated, this, [this](int i) { Lang::instance().setLanguage(m_lang->itemData(i).toString()); });

    auto* langRow = new QHBoxLayout;
    langRow->addWidget(m_langTitle);
    langRow->addWidget(m_lang);
    langRow->addStretch();
    auto* langButtons = new QHBoxLayout;
    langButtons->addWidget(m_reveal);
    langButtons->addWidget(m_copy);
    langButtons->addStretch();
    auto* langTexts = new QVBoxLayout;
    langTexts->addLayout(langRow);
    langTexts->addWidget(m_langPath);
    langTexts->addLayout(langButtons);
    auto* langBlock = new QHBoxLayout;
    langBlock->setSpacing(12);
    langBlock->addWidget(globe, 0, Qt::AlignTop);
    langBlock->addLayout(langTexts, 1);

    m_login = new QCheckBox(this);
    connect(m_login, &QCheckBox::toggled, this, [](bool on) { platform::setLoginItem(on); });

    m_defaultState = new QLabel(this);
    m_makeDefault = new QPushButton(this);
    connect(m_makeDefault, &QPushButton::clicked, this, [this] {
        platform::makeDefaultArchiver();
        QTimer::singleShot(1500, this, &SettingsWindow::refreshState);
    });
    auto* defaultRow = new QHBoxLayout;
    defaultRow->addWidget(m_defaultState, 1);
    defaultRow->addWidget(m_makeDefault);

    m_passwordsTitle = sectionTitle(this);
    m_passwords = new QListWidget(this);
    m_passwords->setMinimumHeight(110);
    m_showPasswords = new QCheckBox(this);
    m_forget = new QPushButton(this);
    connect(m_showPasswords, &QCheckBox::toggled, this, &SettingsWindow::fillPasswords);
    connect(m_forget, &QPushButton::clicked, this, [this] {
        for (QListWidgetItem* item : m_passwords->selectedItems())
            platform::forgetPassword(item->data(Qt::UserRole).toString());
        fillPasswords();
    });
    auto* pwButtons = new QHBoxLayout;
    pwButtons->addWidget(m_showPasswords);
    pwButtons->addStretch();
    pwButtons->addWidget(m_forget);

    auto* bottom = new QHBoxLayout;
    bottom->addStretch();
    bottom->addWidget(new LogButton(this));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 18, 12, 6);
    layout->setSpacing(10);
    layout->addLayout(langBlock);
    layout->addWidget(line(this));
    layout->addWidget(m_login);
    layout->addLayout(defaultRow);
    layout->addWidget(line(this));
    layout->addWidget(m_passwordsTitle);
    layout->addWidget(m_passwords);
    layout->addLayout(pwButtons);
    layout->addLayout(bottom);
    resize(520, 480);

    connect(&Lang::instance(), &Lang::changed, this, &SettingsWindow::retranslate);
    retranslate();
}

void SettingsWindow::showEvent(QShowEvent* e)
{
    QWidget::showEvent(e);
    Lang::instance().rescan();   // a language file may have been added meanwhile
    fillLanguages();
    fillPasswords();
    refreshState();
}

void SettingsWindow::retranslate()
{
    setWindowTitle(L("settings.windowTitle"));
    m_langTitle->setText(L("settings.language"));
    m_langPath->setText(L("settings.languageFiles", {{"path", Lang::instance().userDir()}}));
    m_reveal->setText(L("common.revealIn", {{"app", platform::fileManagerName()}}));
    m_copy->setText(L("settings.copyPath"));
    m_login->setText(L("tray.loginItem"));
    m_makeDefault->setText(L("settings.makeDefault"));
    m_passwordsTitle->setText(L("settings.passwords"));
    m_showPasswords->setText(L("settings.showPasswords"));
    m_forget->setText(L("settings.forget"));
    fillLanguages();
    fillPasswords();
    refreshState();
}

void SettingsWindow::fillLanguages()
{
    m_lang->clear();
    auto* model = qobject_cast<QStandardItemModel*>(m_lang->model());
    for (const Lang::Info& info : Lang::instance().available()) {
        m_lang->addItem(info.name, info.code);
        if (!info.error.isEmpty() && model) {
            // A broken file is listed, greyed out, with the line to fix.
            QStandardItem* item = model->item(m_lang->count() - 1);
            item->setEnabled(false);
            item->setText(info.name + QStringLiteral(" — ") + L("settings.languageBroken", {{"line", QString::number(info.errorLine)}}));
            item->setToolTip(info.path + QStringLiteral("\n") + info.error);
        }
    }
    m_lang->setCurrentIndex(m_lang->findData(Lang::instance().current()));
}

void SettingsWindow::fillPasswords()
{
    m_passwords->clear();
    const QStringList list = platform::savedPasswords();
    for (const QString& pw : list) {
        auto* item = new QListWidgetItem(m_showPasswords->isChecked() ? pw : QString(pw.size(), QChar(0x2022)), m_passwords);
        item->setData(Qt::UserRole, pw);
    }
    if (list.isEmpty()) {
        auto* item = new QListWidgetItem(L("settings.noPasswords"), m_passwords);
        item->setFlags(Qt::NoItemFlags);
    }
    m_forget->setEnabled(!list.isEmpty());
}

void SettingsWindow::refreshState()
{
    const QSignalBlocker block(m_login);
    m_login->setChecked(platform::loginItemEnabled());
    const bool isDefault = platform::isDefaultArchiver();
    m_defaultState->setText(isDefault ? L("settings.isDefault") : L("settings.notDefault"));
    m_makeDefault->setVisible(!isDefault);
}

} // namespace packix::ui
