#include "SevenZipEngine.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QMutex>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTemporaryFile>

#include <filesystem>

namespace packix {

namespace {

const QStringList kCommon = {QStringLiteral("-y"), QStringLiteral("-sccUTF-8"), QStringLiteral("-scsUTF-8")};

// 7-Zip redraws progress with backspaces and carriage returns.
QStringList outputLines(QString text)
{
    text.replace(QLatin1Char('\b'), QLatin1Char('\n'));
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    return text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
}

void parseProgress(const QString& chunk, Progress* progress)
{
    // "  17% 3 + folder/file.txt", " 17%", "  3% - file"
    static const QRegularExpression re(QStringLiteral("^\\s*(\\d{1,3})%(?:\\s+\\d+)?(?:\\s+[-+=.UDTRA]\\s+(.+?))?\\s*$"));
    for (const QString& line : outputLines(chunk)) {
        const auto m = re.match(line);
        if (!m.hasMatch())
            continue;
        progress->setPercent(m.captured(1).toInt());
        if (m.hasCaptured(2) && !m.captured(2).isEmpty())
            progress->setCurrent(m.captured(2));
    }
}

QString errorText(const QString& output)
{
    QStringList keep;
    bool inErrors = false;
    for (const QString& line : outputLines(output)) {
        const QString t = line.trimmed();
        if (t.startsWith(QLatin1String("ERRORS:")) || t.startsWith(QLatin1String("WARNINGS:")))
            inErrors = true;
        if (inErrors || t.startsWith(QLatin1String("ERROR")) || t.startsWith(QLatin1String("WARNING"))
            || t.startsWith(QLatin1String("Open ERROR")))
            keep << t;
    }
    if (keep.isEmpty())
        keep = outputLines(output).mid(qMax(0, int(outputLines(output).size()) - 20));
    return keep.join(QLatin1Char('\n')).trimmed();
}

Status classify(const SevenZipEngine::Run& r, bool hadPassword)
{
    const QString all = r.stageError + QLatin1Char('\n') + r.output;
    if (r.cancelled)
        return Status::fail(Error::Cancelled);
    if (!r.started)
        return Status::fail(Error::EngineMissing, r.output);

    Status st;
    if (r.code == 0 || r.code == 1) {   // 1 = warnings, the job itself is done
        if (r.code == 1 || all.contains(QLatin1String("WARNING")))
            st.warnings << errorText(all);
        return st;
    }
    auto has = [&](const char* s) { return all.contains(QLatin1String(s), Qt::CaseInsensitive); };
    st.detail = errorText(all);
    if (has("Wrong password"))
        st.error = hadPassword ? Error::WrongPassword : Error::NeedPassword;
    else if (has("Enter password") && !hadPassword)
        st.error = Error::NeedPassword;
    else if (has("not enough space") || has("No space left"))
        st.error = Error::NoSpace;
    else if (has("Permission denied") || has("Access is denied") || has("Operation not permitted")
             || has("Can not open output file"))
        st.error = Error::NoAccess;
    else if (has("Unexpected end of archive") || has("Missing volume"))
        st.error = Error::Corrupt;
    else if (has("Not implemented"))
        st.error = Error::ReadOnlyFormat;
    else if (has("Cannot open the file as archive") || has("Can not open the file as archive")
             || has("Is not archive") || has("Unsupported archive type") || has("Cannot open the file as ["))
        st.error = Error::Unsupported;
    else if (has("Data Error") || has("CRC Failed") || has("Headers Error") || has("Unsupported Method"))
        st.error = Error::Corrupt;
    else if (has("No such file") || has("cannot find"))
        st.error = Error::NotFound;
    else
        st.error = Error::Failed;
    return st;
}

QString normalizePath(QString p)
{
#ifdef Q_OS_WIN
    p.replace(QLatin1Char('\\'), QLatin1Char('/'));
#endif
    while (p.startsWith(QLatin1String("./")))
        p.remove(0, 2);
    while (p.endsWith(QLatin1Char('/')))
        p.chop(1);
    return p;
}

void addItem(const QHash<QString, QString>& block, ArchiveInfo& out)
{
    const QString path = normalizePath(block.value(QStringLiteral("Path")));
    if (path.isEmpty())
        return;
    Item it;
    it.path = path;
    const QString attrs = block.value(QStringLiteral("Attributes")).trimmed();
    const QString unixPart = attrs.section(QLatin1Char(' '), -1);
    it.isDir = block.value(QStringLiteral("Folder")) == QLatin1String("+") || attrs.startsWith(QLatin1Char('D'))
        || (unixPart.size() == 10 && unixPart.startsWith(QLatin1Char('d')));
    it.isLink = unixPart.size() == 10 && unixPart.startsWith(QLatin1Char('l'));
    it.size = block.value(QStringLiteral("Size")).toLongLong();
    const QString packed = block.value(QStringLiteral("Packed Size"));
    it.packed = packed.isEmpty() ? -1 : packed.toLongLong();
    it.modified = QDateTime::fromString(block.value(QStringLiteral("Modified")).left(19), QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    it.encrypted = block.value(QStringLiteral("Encrypted")) == QLatin1String("+");
    it.method = block.value(QStringLiteral("Method"));
    out.items.append(it);
}

// `7zz l -slt` output: archive properties, a "----------" line, then one block per item.
void parseListing(const QString& text, ArchiveInfo& out)
{
    bool inItems = false;
    QHash<QString, QString> block;
    for (QString line : text.split(QLatin1Char('\n'))) {
        if (line.endsWith(QLatin1Char('\r')))
            line.chop(1);
        if (!inItems) {
            if (line == QLatin1String("----------")) {
                inItems = true;
                continue;
            }
            const qsizetype eq = line.indexOf(QLatin1String(" = "));
            if (eq <= 0)
                continue;
            const QString key = line.left(eq);
            const QString value = line.mid(eq + 3);
            if (key == QLatin1String("Type"))
                out.engineType = value;   // the last one wins: Split -> 7z
            else if (key == QLatin1String("Solid"))
                out.solid = value == QLatin1String("+");
            continue;
        }
        if (line.isEmpty()) {
            addItem(block, out);
            block.clear();
            continue;
        }
        const qsizetype eq = line.indexOf(QLatin1String(" = "));
        if (eq > 0)
            block.insert(line.left(eq), line.mid(eq + 3));
        else if (line.endsWith(QLatin1String(" =")))
            block.insert(line.chopped(2), QString());
    }
    addItem(block, out);

    // Folders that only exist as parts of paths.
    QSet<QString> dirs;
    for (const Item& i : std::as_const(out.items)) {
        if (i.isDir)
            dirs.insert(i.path);
    }
    const qsizetype n = out.items.size();
    for (qsizetype k = 0; k < n; ++k) {
        QString p = out.items[k].path;
        for (qsizetype slash = p.lastIndexOf(QLatin1Char('/')); slash > 0; slash = p.lastIndexOf(QLatin1Char('/'))) {
            p.truncate(slash);
            if (dirs.contains(p))
                break;
            dirs.insert(p);
            Item d;
            d.path = p;
            d.isDir = true;
            d.implied = true;
            d.size = 0;
            out.items.append(d);
        }
    }
    for (const Item& i : std::as_const(out.items)) {
        if (i.encrypted)
            out.anyEncrypted = true;
    }
}

// Names go through a list file so that nothing in them can look like a switch.
class ListFile {
public:
    explicit ListFile(const QStringList& names)
    {
        if (names.isEmpty() || !m_file.open())
            return;
        for (const QString& n : names)
            m_file.write(n.toUtf8() + '\n');
        m_file.flush();
        m_ok = true;
    }
    // Empty list = no argument at all (= everything).
    QStringList args() const { return m_ok ? QStringList{QLatin1Char('@') + m_file.fileName()} : QStringList(); }

private:
    QTemporaryFile m_file{QDir::tempPath() + QStringLiteral("/packix-list-XXXXXX.txt")};
    bool m_ok = false;
};

// A folder means the folder and everything in it.
QStringList withContents(const QStringList& items)
{
    QStringList out;
    for (const QString& i : items)
        out << i << i + QStringLiteral("/*");
    return out;
}

QByteArray passwordInput(const QString& password, int times)
{
    if (password.isEmpty())
        return {};
    QByteArray b;
    for (int i = 0; i < times; ++i)
        b += password.toUtf8() + '\n';
    return b;
}

bool replaceFile(const QString& from, const QString& to)
{
    std::error_code ec;
    std::filesystem::rename(std::filesystem::path(from.toStdU16String()), std::filesystem::path(to.toStdU16String()), ec);
    return !ec;
}

} // namespace

SevenZipEngine::SevenZipEngine(QString program) : m_program(program.isEmpty() ? findProgram() : std::move(program)) {}

QString SevenZipEngine::findProgram()
{
    const QString env = qEnvironmentVariable("PACKIX_7ZZ");
    if (!env.isEmpty() && QFileInfo(env).isExecutable())
        return env;
    const QString app = QCoreApplication::applicationDirPath();
#ifdef Q_OS_WIN
    const QStringList names = {QStringLiteral("7z.exe")};
    QStringList dirs = {app, QDir::fromNativeSeparators(qEnvironmentVariable("ProgramFiles")) + QStringLiteral("/7-Zip")};
#else
    const QStringList names = {QStringLiteral("7zz")};
    QStringList dirs = {app, app + QStringLiteral("/../Resources"), QStringLiteral("/opt/homebrew/bin"),
                        QStringLiteral("/usr/local/bin")};
#endif
    for (const QString& d : std::as_const(dirs)) {
        for (const QString& n : names) {
            const QFileInfo fi(QDir(d).filePath(n));
            if (fi.isFile() && fi.isExecutable())
                return fi.absoluteFilePath();
        }
    }
    for (const QString& n : names) {
        const QString found = QStandardPaths::findExecutable(n);
        if (!found.isEmpty())
            return found;
    }
    return {};
}

bool SevenZipEngine::available() const
{
    return !m_program.isEmpty() && QFileInfo(m_program).isExecutable();
}

QString SevenZipEngine::version() const
{
    static QMutex mutex;
    static QHash<QString, QString> cache;
    QMutexLocker lock(&mutex);
    if (const auto it = cache.constFind(m_program); it != cache.constEnd())
        return *it;
    const Run r = run({}, {}, {}, nullptr);
    static const QRegularExpression re(QStringLiteral("7-Zip[^:\\n]*?(\\d+\\.\\d+)"));
    const QString v = re.match(r.output).captured(1);
    cache.insert(m_program, v);
    return v;
}

SevenZipEngine::Run SevenZipEngine::run(const QStringList& args, const QString& workDir, const QByteArray& input,
                                        Progress* progress) const
{
    Run r;
    if (!available()) {
        r.output = QStringLiteral("7-Zip not found");
        return r;
    }
    QProcess p;
    p.setProcessChannelMode(QProcess::MergedChannels);
    if (!workDir.isEmpty())
        p.setWorkingDirectory(workDir);
    p.start(m_program, args);
    if (!p.waitForStarted(10000)) {
        r.output = p.errorString();
        return r;
    }
    r.started = true;
    // No password: stdin closes at once, so a password prompt ends with "Break signaled" instead of hanging.
    if (!input.isEmpty()) {
        p.write(input);
        p.waitForBytesWritten(2000);
    }
    p.closeWriteChannel();

    QByteArray out;
    for (;;) {
        const bool finished = p.waitForFinished(100);
        const QByteArray chunk = p.readAll();
        if (!chunk.isEmpty()) {
            out += chunk;
            if (progress)
                parseProgress(QString::fromUtf8(chunk), progress);
        }
        if (finished || p.state() == QProcess::NotRunning)
            break;
        if (progress && progress->cancel) {
            p.kill();
            p.waitForFinished(3000);
            r.cancelled = true;
            break;
        }
    }
    out += p.readAll();
    r.output = QString::fromUtf8(out);
    r.code = p.exitStatus() == QProcess::NormalExit ? p.exitCode() : -1;
    return r;
}

SevenZipEngine::Run SevenZipEngine::pipe(const QStringList& first, const QStringList& second, const QString& workDir,
                                         Progress* progress) const
{
    Run r;
    if (!available()) {
        r.output = QStringLiteral("7-Zip not found");
        return r;
    }
    QProcess a;
    QProcess b;
    a.setStandardOutputProcess(&b);
    b.setProcessChannelMode(QProcess::MergedChannels);
    if (!workDir.isEmpty()) {
        a.setWorkingDirectory(workDir);
        b.setWorkingDirectory(workDir);
    }
    a.start(m_program, first);
    b.start(m_program, second);
    if (!a.waitForStarted(10000) || !b.waitForStarted(10000)) {
        r.output = a.errorString() + QLatin1Char('\n') + b.errorString();
        a.kill();
        b.kill();
        return r;
    }
    r.started = true;
    a.closeWriteChannel();

    QByteArray out;
    QByteArray err;
    for (;;) {
        const bool finished = b.waitForFinished(100);
        const QByteArray e = a.readAllStandardError();
        if (!e.isEmpty()) {
            err += e;
            if (progress)
                parseProgress(QString::fromUtf8(e), progress);
        }
        out += b.readAll();
        if (finished || b.state() == QProcess::NotRunning)
            break;
        if (progress && progress->cancel) {
            a.kill();
            b.kill();
            a.waitForFinished(3000);
            b.waitForFinished(3000);
            r.cancelled = true;
            break;
        }
    }
    a.waitForFinished(5000);
    err += a.readAllStandardError();
    out += b.readAll();
    r.output = QString::fromUtf8(out);
    r.stageError = QString::fromUtf8(err);
    const int codeA = a.exitStatus() == QProcess::NormalExit ? a.exitCode() : -1;
    const int codeB = b.exitStatus() == QProcess::NormalExit ? b.exitCode() : -1;
    r.code = codeA > 1 || codeA < 0 ? codeA : codeB;
    return r;
}

Status SevenZipEngine::list(const QString& archive, const Format& format, const QString& password, ArchiveInfo& out,
                            Progress* progress)
{
    const QString abs = QFileInfo(archive).absoluteFilePath();
    Run r;
    if (format.wrapsTar) {
        r = pipe(QStringList{QStringLiteral("x"), QStringLiteral("-so"), QStringLiteral("-bsp0"), QStringLiteral("-bse2")}
                     + kCommon + QStringList{abs},
                 QStringList{QStringLiteral("l"), QStringLiteral("-slt"), QStringLiteral("-si"), QStringLiteral("-ttar")} + kCommon,
                 {}, progress);
    } else {
        r = run(QStringList{QStringLiteral("l"), QStringLiteral("-slt")} + kCommon + QStringList{abs}, {},
                passwordInput(password, 1), progress);
    }
    Status st = classify(r, !password.isEmpty());
    if (!st.ok())
        return st;

    out = ArchiveInfo();
    out.path = abs;
    out.formatId = format.id;
    parseListing(r.output, out);
    if (format.wrapsTar) {
        out.engineType = format.name;
        for (Item& i : out.items)
            i.packed = -1;
    }
    // A prompt while listing means even the names were hidden behind the password.
    out.encryptedNames = !password.isEmpty() && r.output.contains(QLatin1String("Enter password"));
    if (out.encryptedNames)
        out.anyEncrypted = true;
    return st;
}

Status SevenZipEngine::extract(const QString& archive, const Format& format, const QString& destDir,
                               const QStringList& items, const QString& password, Progress* progress)
{
    const QString abs = QFileInfo(archive).absoluteFilePath();
    const ListFile list(withContents(items));
    const QString out = QStringLiteral("-o") + QDir(destDir).absolutePath();
    Run r;
    if (format.wrapsTar) {
        r = pipe(QStringList{QStringLiteral("x"), QStringLiteral("-so"), QStringLiteral("-bsp2"), QStringLiteral("-bse2")}
                     + kCommon + QStringList{abs},
                 QStringList{QStringLiteral("x"), QStringLiteral("-si"), QStringLiteral("-ttar"), QStringLiteral("-aoa"), out,
                             QStringLiteral("-bsp0"), QStringLiteral("-bse1")}
                     + kCommon + list.args(),
                 {}, progress);
    } else {
        r = run(QStringList{QStringLiteral("x"), QStringLiteral("-aoa"), out, QStringLiteral("-bsp1"), QStringLiteral("-bse1")}
                    + kCommon + QStringList{abs} + list.args(),
                {}, passwordInput(password, 1), progress);
    }
    return classify(r, !password.isEmpty());
}

Status SevenZipEngine::create(const CreateRequest& req, Progress* progress)
{
    const Format f = Formats::instance().byId(req.formatId);
    if (!f.isValid() || !f.canCreate)
        return Status::fail(Error::Unsupported, req.formatId);
    const CompressionLevel level = Formats::instance().level(req.levelId);
    QStringList exclude;
    for (const QString& x : req.exclude)
        exclude << QStringLiteral("-xr!") + x;
    const ListFile list(req.inputs);
    const QString output = QFileInfo(req.output).absoluteFilePath();

    Run r;
    if (f.wrapsTar) {
        const QString tarName = Formats::instance().splitName(QFileInfo(output).fileName()).first + QStringLiteral(".tar");
        r = pipe(QStringList{QStringLiteral("a"), QStringLiteral("-ttar"), QStringLiteral("-so"), QStringLiteral("-an"),
                             QStringLiteral("-snl"), QStringLiteral("-bsp2"), QStringLiteral("-bse2")}
                     + kCommon + exclude + list.args(),
                 QStringList{QStringLiteral("a"), QStringLiteral("-si") + tarName} + f.compressArgs + level.args
                     + QStringList{QStringLiteral("-bso0"), QStringLiteral("-bsp0"), QStringLiteral("-bse1")} + kCommon
                     + QStringList{output},
                 req.workDir, progress);
    } else {
        QStringList args = QStringList{QStringLiteral("a")} + f.createArgs + level.args
            + QStringList{QStringLiteral("-snl"), QStringLiteral("-bsp1"), QStringLiteral("-bse1")} + kCommon + exclude;
        if (!req.password.isEmpty()) {
            args << QStringLiteral("-p") << f.passwordArgs;
            if (req.encryptNames)
                args << f.encryptNamesArgs;
        }
        args << output << list.args();
        r = run(args, req.workDir, passwordInput(req.password, 2), progress);
    }
    return classify(r, !req.password.isEmpty());
}

Status SevenZipEngine::add(const QString& archive, const Format& format, const QString& workDir,
                           const QStringList& inputs, const QString& password, Progress* progress)
{
    if (!format.canModify)
        return Status::fail(Error::ReadOnlyFormat, format.id);
    const ListFile list(inputs);
    auto addTo = [&](const QString& target, const QString& pw) {
        QStringList args = QStringList{QStringLiteral("a"), QStringLiteral("-snl"), QStringLiteral("-bsp1"), QStringLiteral("-bse1")}
            + kCommon;
        if (!pw.isEmpty())
            args << QStringLiteral("-p") << format.passwordArgs;
        args << QFileInfo(target).absoluteFilePath() << list.args();
        return classify(run(args, workDir, passwordInput(pw, 2), progress), !pw.isEmpty());
    };
    if (format.wrapsTar)
        return rewrapTar(archive, format, progress, [&](const QString& tar) { return addTo(tar, {}); });
    return addTo(archive, password);
}

Status SevenZipEngine::remove(const QString& archive, const Format& format, const QStringList& items,
                              const QString& password, Progress* progress)
{
    if (!format.canModify)
        return Status::fail(Error::ReadOnlyFormat, format.id);
    const ListFile list(withContents(items));
    auto removeFrom = [&](const QString& target, const QString& pw) {
        const QStringList args = QStringList{QStringLiteral("d"), QStringLiteral("-bsp1"), QStringLiteral("-bse1")} + kCommon
            + QStringList{QFileInfo(target).absoluteFilePath()} + list.args();
        return classify(run(args, {}, passwordInput(pw, 1), progress), !pw.isEmpty());
    };
    if (format.wrapsTar)
        return rewrapTar(archive, format, progress, [&](const QString& tar) { return removeFrom(tar, {}); });
    return removeFrom(archive, password);
}

Status SevenZipEngine::test(const QString& archive, const Format& format, const QString& password, Progress* progress)
{
    Q_UNUSED(format);
    const Run r = run(QStringList{QStringLiteral("t"), QStringLiteral("-bsp1"), QStringLiteral("-bse1")} + kCommon
                          + QStringList{QFileInfo(archive).absoluteFilePath()},
                      {}, passwordInput(password, 1), progress);
    return classify(r, !password.isEmpty());
}

Status SevenZipEngine::rewrapTar(const QString& archive, const Format& format, Progress* progress,
                                 const std::function<Status(const QString&)>& change)
{
    const QFileInfo fi(archive);
    QTemporaryDir tmp(fi.absolutePath() + QStringLiteral("/.packix-XXXXXX"));
    if (!tmp.isValid())
        return Status::fail(Error::NoAccess, fi.absolutePath());

    Status st = classify(run(QStringList{QStringLiteral("x"), QStringLiteral("-o") + tmp.path()} + kCommon
                                 + QStringList{fi.absoluteFilePath()},
                             {}, {}, progress),
                         false);
    if (!st.ok())
        return st;
    const QStringList tars = QDir(tmp.path()).entryList(QDir::Files);
    if (tars.size() != 1)
        return Status::fail(Error::Corrupt, archive);
    const QString tar = tmp.filePath(tars.first());

    st = change(tar);
    if (!st.ok())
        return st;

    const QString packed = tmp.filePath(fi.fileName());
    st = classify(run(QStringList{QStringLiteral("a")} + format.compressArgs
                          + Formats::instance().level(Formats::instance().defaultLevel()).args + kCommon
                          + QStringList{packed, tar},
                      {}, {}, progress),
                  false);
    if (!st.ok())
        return st;
    if (!replaceFile(packed, fi.absoluteFilePath()))
        return Status::fail(Error::NoAccess, fi.absoluteFilePath());
    return st;
}

// ---------------------------------------------------------------------------

namespace {
QMutex g_engineMutex;
std::shared_ptr<Engine> g_engine;
} // namespace

std::shared_ptr<Engine> engine()
{
    QMutexLocker lock(&g_engineMutex);
    if (!g_engine)
        g_engine = std::make_shared<SevenZipEngine>();
    return g_engine;
}

void setEngine(std::shared_ptr<Engine> e)
{
    QMutexLocker lock(&g_engineMutex);
    g_engine = std::move(e);
}

} // namespace packix
