#include "Archiver.h"

#include "Names.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QStorageInfo>
#include <QTemporaryDir>

#include <filesystem>

namespace packix::Archiver {

namespace {

bool replaceFile(const QString& from, const QString& to)
{
    std::error_code ec;
    std::filesystem::rename(std::filesystem::path(from.toStdU16String()), std::filesystem::path(to.toStdU16String()), ec);
    return !ec;
}

bool removePath(const QString& path)
{
    const QFileInfo fi(path);
    if (fi.isDir() && !fi.isSymLink())
        return QDir(path).removeRecursively();
    return QFile::remove(path);
}

bool copyRecursively(const QString& from, const QString& to)
{
    const QFileInfo fi(from);
    if (fi.isSymLink())
        return QFile::link(fi.symLinkTarget(), to);
    if (!fi.isDir())
        return QFile::copy(from, to);
    if (!QDir().mkpath(to))
        return false;
    const QDir dir(from);
    for (const QString& name : dir.entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System)) {
        if (!copyRecursively(dir.filePath(name), QDir(to).filePath(name)))
            return false;
    }
    return true;
}

QString parentPath(const QString& insidePath)
{
    const qsizetype slash = insidePath.lastIndexOf(QLatin1Char('/'));
    return slash < 0 ? QString() : insidePath.left(slash);
}

QString lastName(const QString& insidePath)
{
    return insidePath.mid(insidePath.lastIndexOf(QLatin1Char('/')) + 1);
}

// Size of the selection inside the archive, folders with their contents.
qint64 selectedSize(const ArchiveInfo& info, const QStringList& items)
{
    if (items.isEmpty())
        return info.totalSize();
    qint64 sum = 0;
    for (const Item& i : info.items) {
        if (i.isDir)
            continue;
        for (const QString& sel : items) {
            if (i.path == sel || i.path.startsWith(sel + QLatin1Char('/'))) {
                sum += i.size;
                break;
            }
        }
    }
    return sum;
}

// Links that point outside the unpacked tree could make later writes land anywhere: drop them.
// Absolute links go too: 7-Zip rewrites them into the hidden stage folder, which is deleted afterwards.
void dropEscapingLinks(const QString& root, QStringList& warnings)
{
    const QString base = QDir::cleanPath(root) + QLatin1Char('/');
    QDirIterator it(root, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                    QDirIterator::Subdirectories);
    QStringList bad;
    while (it.hasNext()) {
        it.next();
        const QFileInfo fi = it.fileInfo();
        if (!fi.isSymLink())
            continue;
        std::error_code ec;
        const auto raw = std::filesystem::read_symlink(std::filesystem::path(fi.filePath().toStdU16String()), ec);
        const QString target = QString::fromStdU16String(raw.u16string());
        const bool escapes = ec || raw.is_absolute()
            || !QDir::cleanPath(fi.absolutePath() + QLatin1Char('/') + target).startsWith(base);
        if (escapes)
            bad << fi.filePath();
    }
    for (const QString& b : std::as_const(bad)) {
        QFile::remove(b);
        warnings << QStringLiteral("link:") + b.mid(base.size());
    }
}

// Moves `from` to `dir/name`, merging folders and asking about files that already exist.
Status place(const QString& from, const QString& dir, QString name, const ConflictHandler& ask,
             QStringList& created, QStringList& warnings)
{
    const QString safe = Names::forThisSystem(name);
    if (safe != name) {
        warnings << QStringLiteral("renamed:") + name + QStringLiteral(" -> ") + safe;
        name = safe;
    }
    QString target = QDir(dir).filePath(name);
    const QFileInfo src(from);
    const QFileInfo dst(target);

    if (dst.exists() || dst.isSymLink()) {
        const bool bothDirs = src.isDir() && !src.isSymLink() && dst.isDir() && !dst.isSymLink();
        if (bothDirs) {
            const QDir d(from);
            for (const QString& child : d.entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System)) {
                const Status st = place(d.filePath(child), target, child, ask, created, warnings);
                if (!st.ok())
                    return st;
            }
            return {};
        }
        const Conflict c = ask ? ask(target, dst.isDir()) : Conflict::KeepBoth;
        switch (c) {
        case Conflict::Cancel:
            return Status::fail(Error::Cancelled);
        case Conflict::Skip:
            return {};
        case Conflict::KeepBoth:
            target = QDir(dir).filePath(Names::unique(dir, name));
            break;
        case Conflict::Replace:
            if (!removePath(target))
                return Status::fail(Error::NoAccess, target);
            break;
        }
    }
    if (!QDir().rename(from, target))
        return Status::fail(Error::NoAccess, target);
    created << target;
    return {};
}

Status checkSpace(const QString& dir, qint64 needed)
{
    const QStorageInfo storage(dir);
    if (storage.isValid() && storage.bytesAvailable() >= 0 && needed > storage.bytesAvailable()) {
        Status st = Status::fail(Error::NoSpace, dir);
        st.needed = needed;
        st.available = storage.bytesAvailable();
        return st;
    }
    return {};
}

QStringList junk()
{
    return Formats::instance().junkPatterns();
}

} // namespace

// ---------------------------------------------------------------------------

Format formatOf(const ArchiveInfo& info)
{
    Format f = Formats::instance().byId(info.formatId);
    if (!f.isValid()) {
        f.id = QString();
        f.name = info.engineType.toUpper();
        f.canOpen = true;
    }
    return f;
}

bool canModify(const ArchiveInfo& info)
{
    return formatOf(info).canModify && info.volumes.size() <= 1;
}

Status open(const QString& path, const QString& password, ArchiveInfo& out, Progress* progress)
{
    const auto eng = engine();
    if (!eng->available())
        return Status::fail(Error::EngineMissing);
    if (!QFileInfo::exists(path))
        return Status::fail(Error::NotFound, path);

    const VolumeSet vol = resolveVolumes(path);
    if (!vol.missing.isEmpty()) {
        Status st = Status::fail(Error::MissingVolume, vol.missing.join(QLatin1Char('\n')));
        st.missing = vol.missing;
        return st;
    }

    // "Игра.7z.001" -> 7z, "Игра.part1.rar" -> rar; anything unknown is left to 7-Zip.
    const QString name = QFileInfo(vol.openPath).fileName();
    Format f = Formats::instance().detect(name);
    if (!f.isValid())
        f = Formats::instance().detect(QFileInfo(name).completeBaseName());

    Status st = eng->list(vol.openPath, f, password, out, progress);
    if (st.error == Error::Corrupt && vol.multi)
        st.error = Error::MissingVolume;   // usually the last part is not there yet
    if (!st.ok())
        return st;

    out.volumes = vol.parts;
    out.fileSize = 0;
    for (const QString& p : vol.parts)
        out.fileSize += QFileInfo(p).size();
    return st;
}

Status extract(const ExtractRequest& req, Progress* progress, QStringList* created)
{
    ArchiveInfo info;
    Status st = open(req.archive, req.password, info, nullptr);
    if (!st.ok())
        return st;

    if (!QDir().mkpath(req.destination))
        return Status::fail(Error::NoAccess, req.destination);
    const qint64 needed = selectedSize(info, req.items);
    st = checkSpace(req.destination, needed);
    if (!st.ok())
        return st;
    if (progress)
        progress->setTotalBytes(needed);

    QTemporaryDir stage(QDir(req.destination).filePath(QStringLiteral(".packix-XXXXXX")));
    if (!stage.isValid())
        return Status::fail(Error::NoAccess, req.destination);

    st = engine()->extract(info.path, formatOf(info), stage.path(), req.items, req.password, progress);
    if (!st.ok())
        return st;
    QStringList warnings = st.warnings;
    dropEscapingLinks(stage.path(), warnings);

    // What goes into the destination: the selected items themselves, or the top level of everything.
    QStringList tops;
    if (req.items.isEmpty()) {
        for (const QString& n : QDir(stage.path()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System))
            tops << n;
    } else {
        // A folder and something inside it both selected: the folder already brings it.
        QSet<QString> chosen(req.items.begin(), req.items.end());
        for (const QString& i : req.items) {
            bool covered = false;
            for (QString p = parentPath(i); !p.isEmpty(); p = parentPath(p))
                covered = covered || chosen.contains(p);
            if (!covered)
                tops << i;
        }
    }

    QStringList made;
    for (const QString& t : std::as_const(tops)) {
        const QString from = QDir(stage.path()).filePath(t);
        if (!QFileInfo::exists(from) && !QFileInfo(from).isSymLink())
            continue;
        st = place(from, req.destination, lastName(t), req.onConflict, made, warnings);
        if (!st.ok())
            break;
    }
    if (req.onCreated) {
        for (const QString& m : std::as_const(made))
            req.onCreated(m);
    }
    if (created)
        *created = made;
    st.warnings = warnings;
    return st;
}

qint64 diskSize(const QStringList& paths)
{
    qint64 sum = 0;
    for (const QString& p : paths) {
        const QFileInfo fi(p);
        if (fi.isSymLink() || !fi.isDir()) {
            sum += fi.size();
            continue;
        }
        QDirIterator it(p, QDir::Files | QDir::Hidden | QDir::System | QDir::NoSymLinks, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            it.next();
            sum += it.fileInfo().size();
        }
    }
    return sum;
}

Status create(const CreateRequest& req, Progress* progress, QString* resultPath)
{
    if (req.sources.isEmpty())
        return Status::fail(Error::NotFound);
    if (!req.password.isEmpty() && !Formats::instance().byId(req.formatId).acceptsPassword(req.password))
        return Status::fail(Error::PasswordCharset);
    if (!QDir().mkpath(req.destinationDir))
        return Status::fail(Error::NoAccess, req.destinationDir);

    // Paths inside the archive are relative to the deepest folder all sources share.
    QString common = QFileInfo(req.sources.first()).absolutePath();
    for (const QString& s : req.sources) {
        const QString dir = QFileInfo(s).absolutePath();
        while (!(dir == common || dir.startsWith(common + QLatin1Char('/'))) && common.contains(QLatin1Char('/')))
            common = QFileInfo(common).absolutePath();
    }
    QStringList inputs;
    for (const QString& s : req.sources)
        inputs << QDir(common).relativeFilePath(QFileInfo(s).absoluteFilePath());

    if (progress)
        progress->setTotalBytes(diskSize(req.sources));

    QTemporaryDir tmp(QDir(req.destinationDir).filePath(QStringLiteral(".packix-XXXXXX")));
    if (!tmp.isValid())
        return Status::fail(Error::NoAccess, req.destinationDir);

    packix::CreateRequest raw;
    raw.output = tmp.filePath(req.fileName);
    raw.workDir = common;
    raw.inputs = inputs;
    raw.formatId = req.formatId;
    raw.levelId = req.levelId;
    raw.password = req.password;
    raw.encryptNames = req.encryptNames;
    raw.exclude = junk();
    Status st = engine()->create(raw, progress);
    if (!st.ok())
        return st;

    QString target = QDir(req.destinationDir).filePath(req.fileName);
    if (QFileInfo::exists(target) && !req.replaceExisting)
        target = QDir(req.destinationDir).filePath(Names::unique(req.destinationDir, req.fileName));
    if (!replaceFile(raw.output, target))
        return Status::fail(Error::NoAccess, target);
    if (resultPath)
        *resultPath = target;
    return st;
}

Status remove(const ArchiveInfo& info, const QStringList& items, const QString& password, Progress* progress)
{
    if (!canModify(info))
        return Status::fail(Error::ReadOnlyFormat, info.formatId);
    return engine()->remove(info.path, formatOf(info), items, password, progress);
}

Status update(const ArchiveInfo& info, const QString& workDir, const QStringList& relativePaths,
              const QString& password, Progress* progress)
{
    if (!canModify(info))
        return Status::fail(Error::ReadOnlyFormat, info.formatId);
    return engine()->add(info.path, formatOf(info), workDir, relativePaths, password, progress);
}

Status add(const ArchiveInfo& info, const QStringList& sources, const QString& insideDir, const QString& password,
           Progress* progress)
{
    if (!canModify(info))
        return Status::fail(Error::ReadOnlyFormat, info.formatId);
    if (insideDir.isEmpty()) {
        // Straight from where the files are, grouped by folder.
        QHash<QString, QStringList> byDir;
        for (const QString& s : sources) {
            const QFileInfo fi(s);
            byDir[fi.absolutePath()] << fi.fileName();
        }
        for (auto it = byDir.cbegin(); it != byDir.cend(); ++it) {
            const Status st = engine()->add(info.path, formatOf(info), it.key(), it.value(), password, progress);
            if (!st.ok())
                return st;
        }
        return {};
    }
    // Into a folder of the archive: rebuild that path in a temporary folder.
    QTemporaryDir tmp;
    if (!tmp.isValid())
        return Status::fail(Error::NoAccess, QDir::tempPath());
    const QString target = tmp.filePath(insideDir);
    QDir().mkpath(target);
    QStringList rel;
    for (const QString& s : sources) {
        const QString name = QFileInfo(s).fileName();
        if (!copyRecursively(s, QDir(target).filePath(name)))
            return Status::fail(Error::NoAccess, s);
        rel << insideDir + QLatin1Char('/') + name;
    }
    return engine()->add(info.path, formatOf(info), tmp.path(), rel, password, progress);
}

Status test(const ArchiveInfo& info, const QString& password, Progress* progress)
{
    return engine()->test(info.path, formatOf(info), password, progress);
}

Status changePassword(const ArchiveInfo& info, const PasswordRequest& req, Progress* progress, QString* resultPath)
{
    if (info.volumes.size() > 1)
        return Status::fail(Error::ReadOnlyFormat, info.path);
    const Format from = formatOf(info);
    const Format to = req.targetFormatId.isEmpty() ? from : Formats::instance().byId(req.targetFormatId);
    if (!to.isValid() || !to.canCreate || (!req.newPassword.isEmpty() && !to.canPassword))
        return Status::fail(Error::ReadOnlyFormat, to.id);
    if (!to.acceptsPassword(req.newPassword))
        return Status::fail(Error::PasswordCharset);

    const QFileInfo fi(info.path);
    Status st = checkSpace(fi.absolutePath(), info.totalSize() + info.fileSize);
    if (!st.ok())
        return st;
    QTemporaryDir tmp(fi.absolutePath() + QStringLiteral("/.packix-XXXXXX"));
    if (!tmp.isValid())
        return Status::fail(Error::NoAccess, fi.absolutePath());
    const QString content = tmp.filePath(QStringLiteral("content"));
    QDir().mkpath(content);

    st = engine()->extract(info.path, from, content, {}, req.oldPassword, progress);
    if (!st.ok())
        return st;

    const QString base = Formats::instance().splitName(fi.fileName()).first;
    const QString newName = to.id == from.id ? fi.fileName() : base + QLatin1Char('.') + to.primaryExtension();
    packix::CreateRequest raw;
    raw.output = tmp.filePath(newName);
    raw.workDir = content;
    raw.inputs = QDir(content).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
    raw.formatId = to.id;
    raw.levelId = Formats::instance().defaultLevel();
    raw.password = req.newPassword;
    raw.encryptNames = req.encryptNames && !to.encryptNamesArgs.isEmpty();
    if (raw.inputs.isEmpty())
        return Status::fail(Error::Corrupt, info.path);
    st = engine()->create(raw, progress);
    if (!st.ok())
        return st;

    // Never replace the original with something that does not open.
    st = engine()->test(raw.output, to, req.newPassword, progress);
    if (!st.ok())
        return st;

    QString target = fi.absoluteFilePath();
    if (to.id != from.id) {
        target = fi.absoluteDir().filePath(Names::unique(fi.absolutePath(), newName));
        if (!replaceFile(raw.output, target))
            return Status::fail(Error::NoAccess, target);
        if (req.removeOriginal)
            QFile::remove(fi.absoluteFilePath());
    } else if (!replaceFile(raw.output, target)) {
        return Status::fail(Error::NoAccess, target);
    }
    if (resultPath)
        *resultPath = target;
    return st;
}

} // namespace packix::Archiver
