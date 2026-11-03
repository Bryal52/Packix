#include "Archiver.h"
#include "Formats.h"
#include "Names.h"
#include "SevenZipEngine.h"
#include "Temp.h"
#include "Volumes.h"

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>

using namespace packix;

namespace {

void write(const QString& path, const QByteArray& data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(data);
}

QByteArray read(const QString& path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray("<missing>");
}

QStringList paths(const ArchiveInfo& info)
{
    QStringList out;
    for (const Item& i : info.items)
        out << i.path;
    out.sort();
    return out;
}

// A small tree: Фото/a.txt, Фото/Отпуск/b.txt, Фото/big.bin, Фото/.DS_Store
QString makeTree(const QString& root)
{
    const QString dir = root + QStringLiteral("/Фото");
    write(dir + QStringLiteral("/a.txt"), "hello");
    write(dir + QStringLiteral("/Отпуск/b.txt"), "мир");
    QByteArray big(200000, 'x');
    for (int i = 0; i < big.size(); i += 7)
        big[i] = char('a' + i % 26);
    write(dir + QStringLiteral("/big.bin"), big);
    write(dir + QStringLiteral("/.DS_Store"), "junk");
    return dir;
}

Status makeArchive(const QString& dir, const QString& name, const QString& format, const QStringList& sources,
                   const QString& password = {}, QString* result = nullptr)
{
    Archiver::CreateRequest req;
    req.sources = sources;
    req.destinationDir = dir;
    req.fileName = name;
    req.formatId = format;
    req.levelId = QStringLiteral("normal");
    req.password = password;
    return Archiver::create(req, nullptr, result);
}

} // namespace

class TestCore : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QVERIFY2(Formats::instance().loadError().isEmpty(), qPrintable(Formats::instance().loadError()));
        QVERIFY2(engine()->available(), "7zz not found");
        qInfo() << "7-Zip" << engine()->version();
    }

    void formats()
    {
        const Formats& f = Formats::instance();
        QCOMPARE(f.detect(QStringLiteral("a.tar.gz")).id, QStringLiteral("tar.gz"));
        QCOMPARE(f.detect(QStringLiteral("a.TGZ")).id, QStringLiteral("tar.gz"));
        QCOMPARE(f.detect(QStringLiteral("a.gz")).id, QStringLiteral("gz"));
        QCOMPARE(f.detect(QStringLiteral("Фото.ZIP")).id, QStringLiteral("zip"));
        QVERIFY(!f.detect(QStringLiteral("zip")).isValid());
        QCOMPARE(f.splitName(QStringLiteral("Фото.tar.gz")), std::make_pair(QStringLiteral("Фото"), QStringLiteral("tar.gz")));
        QCOMPARE(f.splitName(QStringLiteral("a.b.txt")), std::make_pair(QStringLiteral("a.b"), QStringLiteral("txt")));
        QCOMPARE(f.creatable().first().id, QStringLiteral("zip"));
        QCOMPARE(f.creatable().size(), 4);
        QVERIFY(f.isJunk(QStringLiteral(".DS_Store")));
        QVERIFY(f.isJunk(QStringLiteral("._photo.jpg")));
        QVERIFY(!f.isJunk(QStringLiteral("photo.jpg")));
    }

    void uniqueNames()
    {
        QTemporaryDir tmp;
        QCOMPARE(Names::unique(tmp.path(), QStringLiteral("Фото.zip")), QStringLiteral("Фото.zip"));
        write(tmp.filePath(QStringLiteral("Фото.zip")), "1");
        QCOMPARE(Names::unique(tmp.path(), QStringLiteral("Фото.zip")), QStringLiteral("Фото (1).zip"));
        write(tmp.filePath(QStringLiteral("Фото (1).zip")), "1");
        QCOMPARE(Names::unique(tmp.path(), QStringLiteral("Фото.zip")), QStringLiteral("Фото (2).zip"));
        write(tmp.filePath(QStringLiteral("x.tar.gz")), "1");
        QCOMPARE(Names::unique(tmp.path(), QStringLiteral("x.tar.gz")), QStringLiteral("x (1).tar.gz"));
        QCOMPARE(Names::windowsSafe(QStringLiteral("a:b?.txt")), QStringLiteral("a_b_.txt"));
        QCOMPARE(Names::windowsSafe(QStringLiteral("CON.txt")), QStringLiteral("_CON.txt"));
        QCOMPARE(Names::windowsSafe(QStringLiteral("name. ")), QStringLiteral("name"));
    }

    void roundTrip_data()
    {
        QTest::addColumn<QString>("format");
        QTest::addColumn<QString>("name");
        QTest::newRow("zip") << "zip" << "Фото.zip";
        QTest::newRow("7z") << "7z" << "Фото.7z";
        QTest::newRow("tar.gz") << "tar.gz" << "Фото.tar.gz";
        QTest::newRow("tar.xz") << "tar.xz" << "Фото.tar.xz";
    }

    void roundTrip()
    {
        QFETCH(QString, format);
        QFETCH(QString, name);
        QTemporaryDir tmp;
        const QString src = makeTree(tmp.path());
        QString result;
        const Status st = makeArchive(tmp.path(), name, format, {src}, {}, &result);
        QVERIFY2(st.ok(), qPrintable(st.detail));
        QCOMPARE(QFileInfo(result).fileName(), name);
        QVERIFY(QDir(tmp.path()).entryList({QStringLiteral(".packix-*")}, QDir::AllEntries | QDir::Hidden).isEmpty());

        ArchiveInfo info;
        QVERIFY(Archiver::open(result, {}, info).ok());
        QCOMPARE(info.formatId, format);
        QCOMPARE(paths(info), QStringList({QStringLiteral("Фото"), QStringLiteral("Фото/a.txt"), QStringLiteral("Фото/big.bin"),
                                           QStringLiteral("Фото/Отпуск"), QStringLiteral("Фото/Отпуск/b.txt")}));
        QCOMPARE(info.fileCount(), 3);

        Archiver::ExtractRequest req;
        req.archive = result;
        req.destination = tmp.filePath(QStringLiteral("out"));
        QVERIFY(Archiver::extract(req).ok());
        QCOMPARE(read(req.destination + QStringLiteral("/Фото/Отпуск/b.txt")), QByteArray("мир"));
        QCOMPARE(read(req.destination + QStringLiteral("/Фото/big.bin")), read(src + QStringLiteral("/big.bin")));
        QVERIFY(!QFileInfo::exists(req.destination + QStringLiteral("/Фото/.DS_Store")));
        QVERIFY(QDir(req.destination).entryList({QStringLiteral(".packix-*")}, QDir::AllEntries | QDir::Hidden).isEmpty());
    }

    void levels()
    {
        QTemporaryDir tmp;
        const QString src = makeTree(tmp.path());
        qint64 store = 0, max = 0;
        for (const QString& level : {QStringLiteral("store"), QStringLiteral("fast"), QStringLiteral("normal"), QStringLiteral("max")}) {
            Archiver::CreateRequest req;
            req.sources = {src};
            req.destinationDir = tmp.path();
            req.fileName = level + QStringLiteral(".zip");
            req.formatId = QStringLiteral("zip");
            req.levelId = level;
            QString result;
            const Status st = Archiver::create(req, nullptr, &result);
            QVERIFY2(st.ok(), qPrintable(level + QLatin1Char(' ') + st.detail));
            (level == QLatin1String("store") ? store : max) = QFileInfo(result).size();
        }
        QVERIFY(max < store / 2);
    }

    void extractSelected()
    {
        QTemporaryDir tmp;
        const QString src = makeTree(tmp.path());
        QString archive;
        QVERIFY(makeArchive(tmp.path(), QStringLiteral("a.7z"), QStringLiteral("7z"), {src}, {}, &archive).ok());

        Archiver::ExtractRequest req;
        req.archive = archive;
        req.destination = tmp.filePath(QStringLiteral("sel"));
        req.items = {QStringLiteral("Фото/Отпуск"), QStringLiteral("Фото/Отпуск/b.txt"), QStringLiteral("Фото/a.txt")};
        QStringList created;
        QVERIFY(Archiver::extract(req, nullptr, &created).ok());
        QCOMPARE(created.size(), 2);
        QCOMPARE(read(req.destination + QStringLiteral("/Отпуск/b.txt")), QByteArray("мир"));
        QCOMPARE(read(req.destination + QStringLiteral("/a.txt")), QByteArray("hello"));
        QVERIFY(!QFileInfo::exists(req.destination + QStringLiteral("/big.bin")));
    }

    void conflicts()
    {
        QTemporaryDir tmp;
        write(tmp.filePath(QStringLiteral("src/a.txt")), "new");
        QString archive;
        QVERIFY(makeArchive(tmp.path(), QStringLiteral("c.zip"), QStringLiteral("zip"), {tmp.filePath(QStringLiteral("src/a.txt"))}, {}, &archive).ok());
        const QString dest = tmp.filePath(QStringLiteral("dest"));
        write(dest + QStringLiteral("/a.txt"), "old");

        Archiver::ExtractRequest req;
        req.archive = archive;
        req.destination = dest;
        int asked = 0;

        req.onConflict = [&](const QString&, bool) { ++asked; return Conflict::Skip; };
        QVERIFY(Archiver::extract(req).ok());
        QCOMPARE(read(dest + QStringLiteral("/a.txt")), QByteArray("old"));

        req.onConflict = [&](const QString&, bool) { ++asked; return Conflict::KeepBoth; };
        QVERIFY(Archiver::extract(req).ok());
        QCOMPARE(read(dest + QStringLiteral("/a (1).txt")), QByteArray("new"));

        req.onConflict = [&](const QString&, bool) { ++asked; return Conflict::Replace; };
        QVERIFY(Archiver::extract(req).ok());
        QCOMPARE(read(dest + QStringLiteral("/a.txt")), QByteArray("new"));

        req.onConflict = [&](const QString&, bool) { ++asked; return Conflict::Cancel; };
        QCOMPARE(Archiver::extract(req).error, Error::Cancelled);
        QCOMPARE(asked, 4);

        // Creating next to an existing archive keeps both unless told to replace.
        QString second;
        QVERIFY(makeArchive(tmp.path(), QStringLiteral("c.zip"), QStringLiteral("zip"), {tmp.filePath(QStringLiteral("src/a.txt"))}, {}, &second).ok());
        QCOMPARE(QFileInfo(second).fileName(), QStringLiteral("c (1).zip"));
    }

    void passwords()
    {
        QTemporaryDir tmp;
        const QString src = makeTree(tmp.path());
        QString archive;
        QCOMPARE(makeArchive(tmp.path(), QStringLiteral("p.zip"), QStringLiteral("zip"), {src}, QStringLiteral("секрет"), &archive).error,
                 Error::PasswordCharset);
        QVERIFY(makeArchive(tmp.path(), QStringLiteral("p.zip"), QStringLiteral("zip"), {src}, QStringLiteral("Secret 1!"), &archive).ok());

        ArchiveInfo info;
        QVERIFY(Archiver::open(archive, {}, info).ok());   // zip names are not encrypted
        QVERIFY(info.anyEncrypted);
        QVERIFY(!info.encryptedNames);
        QCOMPARE(Archiver::checkPassword(info, {}).error, Error::NeedPassword);
        QCOMPARE(Archiver::checkPassword(info, QStringLiteral("nope")).error, Error::WrongPassword);
        QVERIFY(Archiver::checkPassword(info, QStringLiteral("Secret 1!")).ok());

        Archiver::ExtractRequest req;
        req.archive = archive;
        req.destination = tmp.filePath(QStringLiteral("o"));
        QCOMPARE(Archiver::extract(req).error, Error::NeedPassword);
        req.password = QStringLiteral("wrong");
        QCOMPARE(Archiver::extract(req).error, Error::WrongPassword);
        req.password = QStringLiteral("Secret 1!");
        QVERIFY(Archiver::extract(req).ok());
        QCOMPARE(read(req.destination + QStringLiteral("/Фото/a.txt")), QByteArray("hello"));
    }

    void changePassword()
    {
        QTemporaryDir tmp;
        const QString src = makeTree(tmp.path());
        QString archive;
        QVERIFY(makeArchive(tmp.path(), QStringLiteral("s.7z"), QStringLiteral("7z"), {src}, {}, &archive).ok());
        ArchiveInfo info;
        QVERIFY(Archiver::open(archive, {}, info).ok());

        Archiver::PasswordRequest pr;
        pr.newPassword = QStringLiteral("пароль 7z");   // 7z takes any characters
        QVERIFY(Archiver::changePassword(info, pr).ok());

        ArchiveInfo locked;
        QCOMPARE(Archiver::open(archive, {}, locked).error, Error::NeedPassword);   // 7z hides names too
        QCOMPARE(Archiver::open(archive, QStringLiteral("bad"), locked).error, Error::WrongPassword);
        QVERIFY(Archiver::open(archive, QStringLiteral("пароль 7z"), locked).ok());
        QVERIFY(locked.encryptedNames);

        // And take it off again.
        pr.oldPassword = QStringLiteral("пароль 7z");
        pr.newPassword.clear();
        QVERIFY(Archiver::changePassword(locked, pr).ok());
        QVERIFY(Archiver::open(archive, {}, locked).ok());
        QVERIFY(!locked.anyEncrypted);

        // tar.gz cannot hold a password: becomes zip next to it.
        QString tgz;
        QVERIFY(makeArchive(tmp.path(), QStringLiteral("t.tar.gz"), QStringLiteral("tar.gz"), {src}, {}, &tgz).ok());
        QVERIFY(Archiver::open(tgz, {}, info).ok());
        pr = {};
        pr.newPassword = QStringLiteral("z");
        pr.targetFormatId = QStringLiteral("zip");
        QString result;
        QVERIFY(Archiver::changePassword(info, pr, nullptr, &result).ok());
        QCOMPARE(QFileInfo(result).fileName(), QStringLiteral("t.zip"));
        QVERIFY(!QFileInfo::exists(tgz));
    }

    void removeAndAdd_data()
    {
        QTest::addColumn<QString>("format");
        QTest::newRow("zip") << "zip";
        QTest::newRow("7z") << "7z";
        QTest::newRow("tar.gz") << "tar.gz";
    }

    void removeAndAdd()
    {
        QFETCH(QString, format);
        QTemporaryDir tmp;
        const QString src = makeTree(tmp.path());
        QString archive;
        QVERIFY(makeArchive(tmp.path(), QStringLiteral("r.") + format, format, {src}, {}, &archive).ok());
        ArchiveInfo info;
        QVERIFY(Archiver::open(archive, {}, info).ok());

        Status st = Archiver::remove(info, {QStringLiteral("Фото/Отпуск")}, {});
        QVERIFY2(st.ok(), qPrintable(st.detail));
        QVERIFY(Archiver::open(archive, {}, info).ok());
        QCOMPARE(paths(info), QStringList({QStringLiteral("Фото"), QStringLiteral("Фото/a.txt"), QStringLiteral("Фото/big.bin")}));

        write(tmp.filePath(QStringLiteral("extra/new.txt")), "added");
        st = Archiver::add(info, {tmp.filePath(QStringLiteral("extra/new.txt"))}, QStringLiteral("Фото/Новая"), {});
        QVERIFY2(st.ok(), qPrintable(st.detail));
        st = Archiver::add(info, {tmp.filePath(QStringLiteral("extra/new.txt"))}, {}, {});
        QVERIFY2(st.ok(), qPrintable(st.detail));
        QVERIFY(Archiver::open(archive, {}, info).ok());
        QVERIFY(paths(info).contains(QStringLiteral("Фото/Новая/new.txt")));
        QVERIFY(paths(info).contains(QStringLiteral("new.txt")));
    }

    void readOnly()
    {
        ArchiveInfo info;
        info.formatId = QStringLiteral("rar");
        info.volumes = {QStringLiteral("x.rar")};
        QVERIFY(!Archiver::canModify(info));
        QCOMPARE(Archiver::remove(info, {QStringLiteral("a")}, {}).error, Error::ReadOnlyFormat);
    }

    void volumes()
    {
        QTemporaryDir tmp;
        const QString src = makeTree(tmp.path());
        QProcess p;
        p.setWorkingDirectory(tmp.path());
        p.start(SevenZipEngine::findProgram(), {QStringLiteral("a"), QStringLiteral("-t7z"), QStringLiteral("-mx0"),
                                                QStringLiteral("-v60k"), QStringLiteral("mv.7z"), src});
        QVERIFY(p.waitForFinished(30000));
        QVERIFY(QFileInfo::exists(tmp.filePath(QStringLiteral("mv.7z.004"))));

        const VolumeSet v = resolveVolumes(tmp.filePath(QStringLiteral("mv.7z.003")));
        QVERIFY(v.multi);
        QCOMPARE(QFileInfo(v.openPath).fileName(), QStringLiteral("mv.7z.001"));
        QVERIFY(v.missing.isEmpty());

        ArchiveInfo info;
        QVERIFY(Archiver::open(tmp.filePath(QStringLiteral("mv.7z.002")), {}, info).ok());
        QCOMPARE(info.formatId, QStringLiteral("7z"));
        QVERIFY(info.volumes.size() >= 4);
        QVERIFY(!Archiver::canModify(info));

        QFile::rename(tmp.filePath(QStringLiteral("mv.7z.002")), tmp.filePath(QStringLiteral("hold")));
        const Status st = Archiver::open(tmp.filePath(QStringLiteral("mv.7z.001")), {}, info);
        QCOMPARE(st.error, Error::MissingVolume);
        QCOMPARE(st.missing, QStringList{QStringLiteral("mv.7z.002")});

        // Name patterns of other kinds, with empty files.
        for (const char* n : {"g.part1.rar", "g.part2.rar", "g.part4.rar", "o.rar", "o.r00", "o.r01", "s.z01", "s.zip"})
            write(tmp.filePath(QString::fromLatin1(n)), "x");
        VolumeSet rar = resolveVolumes(tmp.filePath(QStringLiteral("g.part2.rar")));
        QCOMPARE(QFileInfo(rar.openPath).fileName(), QStringLiteral("g.part1.rar"));
        QCOMPARE(rar.missing, QStringList{QStringLiteral("g.part3.rar")});
        VolumeSet old = resolveVolumes(tmp.filePath(QStringLiteral("o.r01")));
        QCOMPARE(QFileInfo(old.openPath).fileName(), QStringLiteral("o.rar"));
        QCOMPARE(old.parts.size(), 3);
        old = resolveVolumes(tmp.filePath(QStringLiteral("o.rar")));
        QCOMPARE(old.parts.size(), 3);
        VolumeSet zs = resolveVolumes(tmp.filePath(QStringLiteral("s.z01")));
        QCOMPARE(QFileInfo(zs.openPath).fileName(), QStringLiteral("s.zip"));
        QCOMPARE(zs.parts.size(), 2);
        QVERIFY(!resolveVolumes(tmp.filePath(QStringLiteral("report.2024"))).multi);
    }

    void escapingLinkDropped()
    {
        QTemporaryDir tmp;
        QDir().mkpath(tmp.filePath(QStringLiteral("src")));
        write(tmp.filePath(QStringLiteral("src/ok.txt")), "ok");
        QVERIFY(QFile::link(QStringLiteral("/etc"), tmp.filePath(QStringLiteral("src/evil"))));
        QVERIFY(QFile::link(QStringLiteral("ok.txt"), tmp.filePath(QStringLiteral("src/fine"))));
        QString archive;
        QVERIFY(makeArchive(tmp.path(), QStringLiteral("l.zip"), QStringLiteral("zip"), {tmp.filePath(QStringLiteral("src"))}, {}, &archive).ok());

        Archiver::ExtractRequest req;
        req.archive = archive;
        req.destination = tmp.filePath(QStringLiteral("out"));
        const Status st = Archiver::extract(req);
        QVERIFY(st.ok());
        QVERIFY(!QFileInfo(req.destination + QStringLiteral("/src/evil")).isSymLink());
        QVERIFY(QFileInfo(req.destination + QStringLiteral("/src/fine")).isSymLink());
        QCOMPARE(read(req.destination + QStringLiteral("/src/ok.txt")), QByteArray("ok"));
    }

    void errors()
    {
        QTemporaryDir tmp;
        write(tmp.filePath(QStringLiteral("not.zip")), "this is not an archive");
        ArchiveInfo info;
        const Error e = Archiver::open(tmp.filePath(QStringLiteral("not.zip")), {}, info).error;
        QVERIFY2(e == Error::Unsupported || e == Error::Corrupt, qPrintable(QString::number(int(e))));
        QCOMPARE(Archiver::open(tmp.filePath(QStringLiteral("none.zip")), {}, info).error, Error::NotFound);
    }

    void cancel()
    {
        QTemporaryDir tmp;
        QByteArray big(64 * 1024 * 1024, '\0');
        for (int i = 0; i < big.size(); i += 4096)
            big[i] = char(i);
        write(tmp.filePath(QStringLiteral("big/huge.bin")), big);
        Progress progress;
        progress.cancel = true;
        Archiver::CreateRequest req;
        req.sources = {tmp.filePath(QStringLiteral("big"))};
        req.destinationDir = tmp.path();
        req.fileName = QStringLiteral("x.7z");
        req.formatId = QStringLiteral("7z");
        req.levelId = QStringLiteral("max");
        QCOMPARE(Archiver::create(req, &progress).error, Error::Cancelled);
        QVERIFY(!QFileInfo::exists(tmp.filePath(QStringLiteral("x.7z"))));
        QVERIFY(QDir(tmp.path()).entryList({QStringLiteral(".packix-*")}, QDir::AllEntries | QDir::Hidden).isEmpty());
    }

    void tempFolders()
    {
        const QString s = Temp::sessionDir();
        QVERIFY(QFileInfo(s).isDir());
        QCOMPARE(QFileInfo(s).permissions() & (QFileDevice::ReadOther | QFileDevice::ReadGroup), QFileDevice::Permissions());
        const QString d = Temp::newDir();
        QVERIFY(d.startsWith(s));
        Temp::cleanupStale();
        QVERIFY(QFileInfo(d).isDir());   // ours is alive
        Temp::cleanupSession();
        QVERIFY(!QFileInfo::exists(s));
    }
};

QTEST_GUILESS_MAIN(TestCore)
#include "tst_core.moc"
