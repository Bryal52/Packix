#include "Dialogs.h"

#include "Effects.h"
#include "Formats.h"
#include "Icons.h"
#include "Lang.h"
#include "Log.h"
#include "Platform.h"
#include "Theme.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QEventLoop>
#include <QFileInfo>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QThread>
#include <QVBoxLayout>
#include <QtConcurrent>

#include <optional>

namespace packix::ui::Dialogs {

namespace {

void prepare(QMessageBox& box, const QString& title, const QString& text)
{
    box.setWindowModality(Qt::WindowModal);
    box.setText(title);
    box.setInformativeText(text);
    Theme::get().apply(&box);
}

QLabel* boldLabel(const QString& text, QWidget* parent)
{
    auto* l = new QLabel(text, parent);
    QFont f = l->font();
    f.setBold(true);
    f.setPointSizeF(f.pointSizeF() + 1);
    l->setFont(f);
    l->setWordWrap(true);
    return l;
}

QLabel* hintLabel(QWidget* parent, bool warning = false)
{
    auto* l = new QLabel(parent);
    l->setWordWrap(true);
    QFont f = l->font();
    f.setPointSizeF(f.pointSizeF() - 1);
    l->setFont(f);
    QPalette pal = l->palette();
    pal.setColor(QPalette::WindowText, warning ? Theme::get().color("warning") : pal.color(QPalette::PlaceholderText));
    l->setPalette(pal);
    return l;
}

// ---------------------------------------------------------------------------

class PasswordDialog : public QDialog {
public:
    PasswordDialog(QWidget* parent, const QString& archiveName, std::function<Status(const QString&)> check)
        : QDialog(parent), m_check(std::move(check))
    {
        setWindowTitle(L("password.windowTitle"));
        setWindowModality(Qt::WindowModal);
        Theme::get().apply(this);

        m_book = new BookLockWidget(56, this);
        auto* title = boldLabel(L("password.title", {{"name", archiveName}}), this);
        auto* sub = hintLabel(this);
        sub->setText(L("password.subtitle"));

        m_edit = new QLineEdit(this);
        m_edit->setEchoMode(QLineEdit::Password);
        m_edit->setPlaceholderText(L("password.placeholder"));
        m_edit->setMinimumWidth(300);
        m_error = hintLabel(this, true);
        m_error->hide();
        m_remember = new QCheckBox(L("password.remember"), this);

        m_cancel = new QPushButton(L("common.cancel"), this);
        m_ok = new QPushButton(L("password.open"), this);
        m_ok->setDefault(true);
        m_ok->setStyleSheet(Theme::get().accentButtonStyle());
        connect(m_cancel, &QPushButton::clicked, this, &QDialog::reject);
        connect(m_ok, &QPushButton::clicked, this, &PasswordDialog::tryPassword);
        connect(m_edit, &QLineEdit::textChanged, this, [this] {
            m_ok->setEnabled(!m_edit->text().isEmpty());
            m_error->hide();
        });
        m_ok->setEnabled(false);

        auto* head = new QHBoxLayout;
        head->setSpacing(14);
        head->addWidget(m_book, 0, Qt::AlignTop);
        auto* texts = new QVBoxLayout;
        texts->addWidget(title);
        texts->addWidget(sub);
        head->addLayout(texts, 1);

        auto* buttons = new QHBoxLayout;
        buttons->addStretch();
        buttons->addWidget(m_cancel);
        buttons->addWidget(m_ok);

        auto* layout = new QVBoxLayout(this);
        const int m = Theme::get().metric("dialogMargin", 20);
        layout->setContentsMargins(m, m, m, m - 4);
        layout->setSpacing(10);
        layout->addLayout(head);
        layout->addWidget(m_edit);
        layout->addWidget(m_error);
        layout->addWidget(m_remember);
        layout->addLayout(buttons);
    }

    QString password() const { return m_password; }
    Status lastStatus() const { return m_status; }

private:
    void tryPassword()
    {
        const QString pw = m_edit->text();
        m_ok->setEnabled(false);
        m_edit->setEnabled(false);
        m_ok->setText(L("password.checking"));
        m_status = runAsync([this, pw] { return m_check(pw); });
        m_ok->setText(L("password.open"));
        m_edit->setEnabled(true);

        if (m_status.error == Error::WrongPassword || m_status.error == Error::NeedPassword) {
            m_edit->clear();
            m_edit->setFocus();
            m_error->setText(L("password.wrong"));
            m_error->show();
            shake(this);
            return;
        }
        if (!m_status.ok()) {
            reject();
            return;
        }
        m_password = pw;
        if (m_remember->isChecked())
            platform::savePassword(pw);
        // The clasp snaps open, then the dialog goes.
        m_book->animateOpen([this] { accept(); });
    }

    std::function<Status(const QString&)> m_check;
    BookLockWidget* m_book;
    QLineEdit* m_edit;
    QLabel* m_error;
    QCheckBox* m_remember;
    QPushButton* m_cancel;
    QPushButton* m_ok;
    QString m_password;
    Status m_status = Status::fail(Error::Cancelled);
};

} // namespace

// ---------------------------------------------------------------------------

Status runAsync(const std::function<Status()>& work)
{
    QFutureWatcher<Status> watcher;
    QEventLoop loop;
    QObject::connect(&watcher, &QFutureWatcher<Status>::finished, &loop, &QEventLoop::quit);
    watcher.setFuture(QtConcurrent::run(work));
    if (!watcher.isFinished())
        loop.exec(QEventLoop::ExcludeUserInputEvents);
    return watcher.result();
}

bool confirm(QWidget* parent, const QString& title, const QString& text, const QString& okText, bool destructive)
{
    QMessageBox box(parent);
    prepare(box, title, text);
    box.setIcon(destructive ? QMessageBox::Warning : QMessageBox::Question);
    QPushButton* ok = box.addButton(okText, destructive ? QMessageBox::DestructiveRole : QMessageBox::AcceptRole);
    QPushButton* cancel = box.addButton(L("common.cancel"), QMessageBox::RejectRole);
    box.setDefaultButton(destructive ? cancel : ok);
    box.setEscapeButton(cancel);
    box.exec();
    return box.clickedButton() == ok;
}

void info(QWidget* parent, const QString& title, const QString& text)
{
    QMessageBox box(parent);
    prepare(box, title, text);
    box.setIcon(QMessageBox::Information);
    box.addButton(L("common.ok"), QMessageBox::AcceptRole);
    box.exec();
}

QString errorTitle(const Status& st)
{
    switch (st.error) {
    case Error::None: return {};
    case Error::NeedPassword: return L("error.needPassword.title");
    case Error::WrongPassword: return L("error.wrongPassword.title");
    case Error::PasswordCharset: return L("error.passwordCharset.title");
    case Error::Corrupt: return L("error.corrupt.title");
    case Error::Unsupported: return L("error.unsupported.title");
    case Error::MissingVolume: return L("error.missingVolume.title");
    case Error::NoSpace: return L("error.noSpace.title");
    case Error::NoAccess: return L("error.noAccess.title");
    case Error::NotFound: return L("error.notFound.title");
    case Error::ReadOnlyFormat: return L("error.readOnly.title");
    case Error::Cancelled: return L("error.cancelled.title");
    case Error::EngineMissing: return L("error.engineMissing.title");
    case Error::Failed: return L("error.failed.title");
    }
    return {};
}

QString errorText(const Status& st, const QString& name)
{
    switch (st.error) {
    case Error::None: return {};
    case Error::NeedPassword: return L("error.needPassword.text", {{"name", name}});
    case Error::WrongPassword: return L("error.wrongPassword.text", {{"name", name}});
    case Error::PasswordCharset: return L("error.passwordCharset.text");
    case Error::Corrupt: return L("error.corrupt.text", {{"name", name}});
    case Error::Unsupported: return L("error.unsupported.text", {{"name", name}});
    case Error::MissingVolume: return L("error.missingVolume.text", {{"parts", st.missing.join(QStringLiteral(", "))}});
    case Error::NoSpace: return L("error.noSpace.text", {{"need", humanSize(st.needed)}, {"free", humanSize(st.available)}});
    case Error::NoAccess: return L("error.noAccess.text", {{"path", st.detail}});
    case Error::NotFound: return L("error.notFound.text", {{"name", name}});
    case Error::ReadOnlyFormat: return L("error.readOnly.text");
    case Error::Cancelled: return {};
    case Error::EngineMissing: return L("error.engineMissing.text");
    case Error::Failed: return L("error.failed.text");
    }
    return {};
}

void showError(QWidget* parent, const Status& st, const QString& archiveName)
{
    if (st.ok() || st.error == Error::Cancelled)
        return;
    Log::error(archiveName, errorTitle(st) + (st.detail.isEmpty() ? QString() : QStringLiteral("\n") + st.detail));
    QMessageBox box(parent);
    prepare(box, errorTitle(st), errorText(st, archiveName));
    box.setIcon(QMessageBox::Warning);
    QPushButton* more = nullptr;
    if (!st.detail.isEmpty())
        more = box.addButton(L("common.details"), QMessageBox::ActionRole);
    box.setDefaultButton(box.addButton(L("common.ok"), QMessageBox::AcceptRole));
    box.exec();
    if (more && box.clickedButton() == more) {
        QMessageBox details(parent);
        prepare(details, errorTitle(st), st.detail + QStringLiteral("\n\n") + L("log.fileAt", {{"path", Log::filePath()}}));
        details.setTextInteractionFlags(Qt::TextSelectableByMouse);
        QPushButton* reveal = details.addButton(L("common.revealIn", {{"app", platform::fileManagerName()}}), QMessageBox::ActionRole);
        details.addButton(L("common.ok"), QMessageBox::AcceptRole);
        details.exec();
        if (details.clickedButton() == reveal)
            platform::reveal(Log::filePath());
    }
}

ConflictAnswer askConflict(QWidget* parent, const QString& existingPath, bool forExtract)
{
    QMessageBox box(parent);
    const QString name = QFileInfo(existingPath).fileName();
    prepare(box, L("conflict.title"), L("conflict.text", {{"name", name}}));
    box.setIcon(QMessageBox::Warning);
    QPushButton* cancel = box.addButton(L("common.cancel"), QMessageBox::RejectRole);
    QPushButton* skip = forExtract ? box.addButton(L("conflict.skip"), QMessageBox::ActionRole) : nullptr;
    QPushButton* both = box.addButton(L("conflict.keepBoth"), QMessageBox::ActionRole);
    QPushButton* replace = box.addButton(L("conflict.replace"), QMessageBox::DestructiveRole);
    QCheckBox* all = nullptr;
    if (forExtract) {
        all = new QCheckBox(L("conflict.applyToAll"), &box);
        box.setCheckBox(all);
    }
    box.setDefaultButton(cancel);   // a stray Enter must not destroy anything
    box.setEscapeButton(cancel);
    box.exec();

    ConflictAnswer a;
    a.applyToAll = all && all->isChecked();
    const auto* clicked = box.clickedButton();
    a.choice = clicked == replace ? Conflict::Replace
        : clicked == both         ? Conflict::KeepBoth
        : (skip && clicked == skip) ? Conflict::Skip
                                    : Conflict::Cancel;
    return a;
}

ConflictHandler conflictHandler(QWidget* parent, bool forExtract)
{
    auto remembered = std::make_shared<std::optional<Conflict>>();
    QPointer<QWidget> owner(parent);
    return [remembered, owner, forExtract](const QString& path, bool) {
        if (*remembered)
            return **remembered;
        ConflictAnswer a;
        auto ask = [&] { a = askConflict(owner, path, forExtract); };
        if (QThread::currentThread() == qApp->thread())
            ask();
        else
            QMetaObject::invokeMethod(qApp, ask, Qt::BlockingQueuedConnection);
        if (a.applyToAll && a.choice != Conflict::Cancel)
            *remembered = a.choice;
        return a.choice;
    };
}

Status askPassword(QWidget* parent, const QString& archiveName, const std::function<Status(const QString&)>& check,
                   QString* password, bool trySaved)
{
    if (trySaved) {
        for (const QString& pw : platform::savedPasswords()) {
            const Status st = runAsync([&] { return check(pw); });
            if (st.ok()) {
                if (password)
                    *password = pw;
                return st;
            }
            if (st.error != Error::WrongPassword && st.error != Error::NeedPassword)
                return st;
        }
    }
    PasswordDialog dlg(parent, archiveName, check);
    if (dlg.exec() == QDialog::Accepted) {
        if (password)
            *password = dlg.password();
        return {};
    }
    return dlg.lastStatus().ok() ? Status::fail(Error::Cancelled) : dlg.lastStatus();
}

bool askNewPassword(QWidget* parent, const QString& archiveName, const QString& formatId, bool change, NewPassword* out)
{
    const Format own = Formats::instance().byId(formatId);
    const bool needsNewFormat = !own.canPassword || !own.canModify;

    QDialog dlg(parent);
    dlg.setWindowModality(Qt::WindowModal);
    dlg.setWindowTitle(change ? L("setPassword.changeWindowTitle") : L("setPassword.windowTitle"));
    Theme::get().apply(&dlg);

    auto* title = boldLabel(change ? L("setPassword.changeTitle", {{"name", archiveName}})
                                   : L("setPassword.title", {{"name", archiveName}}), &dlg);
    QComboBox* format = nullptr;
    QLabel* formatNote = nullptr;
    if (needsNewFormat) {
        formatNote = hintLabel(&dlg);
        formatNote->setText(L("setPassword.newFormatNote", {{"format", own.isValid() ? own.name : archiveName}}));
        format = new QComboBox(&dlg);
        for (const Format& f : Formats::instance().creatable()) {
            if (f.canPassword)
                format->addItem(f.name, f.id);
        }
    }
    auto* pass = new QLineEdit(&dlg);
    auto* again = new QLineEdit(&dlg);
    for (QLineEdit* e : {pass, again}) {
        e->setEchoMode(QLineEdit::Password);
        e->setMinimumWidth(260);
    }
    auto* hint = hintLabel(&dlg);
    auto* mismatch = hintLabel(&dlg, true);

    auto* cancel = new QPushButton(L("common.cancel"), &dlg);
    auto* ok = new QPushButton(change ? L("setPassword.changeButton") : L("setPassword.button"), &dlg);
    ok->setDefault(true);
    ok->setStyleSheet(Theme::get().accentButtonStyle());
    QObject::connect(cancel, &QPushButton::clicked, &dlg, &QDialog::reject);
    QObject::connect(ok, &QPushButton::clicked, &dlg, &QDialog::accept);

    auto target = [&] {
        return format ? Formats::instance().byId(format->currentData().toString()) : own;
    };
    auto validate = [&] {
        const Format f = target();
        hint->setText(f.passwordAsciiOnly ? L("password.asciiHint") : QString());
        hint->setVisible(f.passwordAsciiOnly);
        QString problem;
        if (!pass->text().isEmpty() && !f.acceptsPassword(pass->text()))
            problem = L("password.asciiOnly");
        else if (!again->text().isEmpty() && again->text() != pass->text())
            problem = L("password.mismatch");
        mismatch->setText(problem);
        mismatch->setVisible(!problem.isEmpty());
        ok->setEnabled(!pass->text().isEmpty() && pass->text() == again->text() && f.acceptsPassword(pass->text()));
    };
    QObject::connect(pass, &QLineEdit::textChanged, &dlg, validate);
    QObject::connect(again, &QLineEdit::textChanged, &dlg, validate);
    if (format)
        QObject::connect(format, &QComboBox::currentIndexChanged, &dlg, validate);

    auto* form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    if (format)
        form->addRow(L("setPassword.newFormat"), format);
    form->addRow(L("create.password"), pass);
    form->addRow(L("create.passwordAgain"), again);

    auto* buttons = new QHBoxLayout;
    buttons->addStretch();
    buttons->addWidget(cancel);
    buttons->addWidget(ok);

    auto* layout = new QVBoxLayout(&dlg);
    const int m = Theme::get().metric("dialogMargin", 20);
    layout->setContentsMargins(m, m, m, m - 4);
    layout->setSpacing(10);
    layout->addWidget(title);
    if (formatNote)
        layout->addWidget(formatNote);
    layout->addLayout(form);
    layout->addWidget(hint);
    layout->addWidget(mismatch);
    layout->addLayout(buttons);
    validate();

    if (dlg.exec() != QDialog::Accepted)
        return false;
    out->password = pass->text();
    out->targetFormatId = format ? format->currentData().toString() : QString();
    return true;
}

} // namespace packix::ui::Dialogs
