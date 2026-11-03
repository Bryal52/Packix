#include "Actions.h"

#include "CreateDialog.h"
#include "Dialogs.h"
#include "ExtractDialog.h"
#include "Jobs.h"
#include "Lang.h"
#include "Platform.h"

#include <QFileInfo>
#include <QPointer>

namespace packix::ui::Actions {

void createArchive(const QStringList& sources, QWidget* parent, std::function<void(const QString&)> done)
{
    if (sources.isEmpty())
        return;
    platform::activate();
    CreateDialog dlg(sources, parent);
    dlg.raise();
    dlg.activateWindow();
    if (dlg.exec() != QDialog::Accepted)
        return;
    const Archiver::CreateRequest req = dlg.request();

    JobSpec spec;
    spec.title = L("job.creating", {{"name", req.fileName}});
    spec.archiveName = req.fileName;
    spec.packAnimation = true;
    spec.notifyTitle = L("notify.created");
    spec.work = [req](Progress& progress, QString& result) { return Archiver::create(req, &progress, &result); };
    spec.done = [done](const Status& st, const QString& result) {
        if (st.ok() && done)
            done(result);
    };
    Jobs::instance().start(spec, parent);
}

bool ensurePassword(QWidget* parent, const ArchiveInfo& info, QString* password)
{
    if (!info.anyEncrypted || !password->isEmpty())
        return true;
    const QString name = QFileInfo(info.path).fileName();
    const Status st = Dialogs::askPassword(parent, name, [info](const QString& pw) { return Archiver::checkPassword(info, pw); },
                                           password);
    if (!st.ok()) {
        Dialogs::showError(parent, st, name);
        return false;
    }
    return true;
}

void extract(QWidget* parent, const ArchiveInfo& info, const QStringList& items, QString* password,
             const QString& destination, std::function<void(const Status&, const QStringList&)> done)
{
    if (!ensurePassword(parent, info, password))
        return;
    QString dest = destination;
    if (dest.isEmpty()) {
        ExtractDialog dlg(info.path, int(items.size()), parent);
        if (dlg.exec() != QDialog::Accepted)
            return;
        dest = dlg.destination();
    }

    Archiver::ExtractRequest req;
    req.archive = info.path;
    req.destination = dest;
    req.items = items;
    req.password = *password;
    req.onConflict = Dialogs::conflictHandler(parent, true);
    const QString archive = info.path;
    req.onCreated = [archive](const QString& created) { platform::inheritQuarantine(archive, created); };

    auto created = std::make_shared<QStringList>();
    JobSpec spec;
    const QString name = QFileInfo(info.path).fileName();
    spec.title = L("job.extracting", {{"name", name}});
    spec.archiveName = name;
    spec.notifyTitle = L("notify.extracted");
    spec.work = [req, created](Progress& progress, QString& result) {
        const Status st = Archiver::extract(req, &progress, created.get());
        result = created->size() == 1 ? created->first() : req.destination;
        return st;
    };
    spec.done = [done, created](const Status& st, const QString&) {
        if (done)
            done(st, *created);
    };
    Jobs::instance().start(spec, parent);
}

bool open(QWidget* parent, const QString& path, ArchiveInfo* info, QString* password)
{
    const QString name = QFileInfo(path).fileName();
    Status st = Archiver::open(path, *password, *info);
    if (st.error == Error::NeedPassword || (st.error == Error::WrongPassword && !password->isEmpty())) {
        // The names themselves are encrypted: the password is needed just to look inside.
        ArchiveInfo* out = info;
        st = Dialogs::askPassword(parent, name, [path, out](const QString& pw) { return Archiver::open(path, pw, *out); },
                                  password);
    }
    if (!st.ok()) {
        Dialogs::showError(parent, st, name);
        return false;
    }
    return true;
}

} // namespace packix::ui::Actions
