// packix — command line front end of the Packix core, for scripts and testing.
//   packix list <archive>
//   packix x <archive> <destination> [items...]
//   packix a <archive> <sources...> [--format zip] [--level normal]
//   packix d <archive> <items...>
//   packix t <archive>
// The password comes from $PACKIX_PASSWORD so it never shows up in the process list.

#include "Archiver.h"
#include "Formats.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QTextStream>

using namespace packix;

namespace {

QTextStream& out()
{
    static QTextStream s(stdout);
    return s;
}

QTextStream& err()
{
    static QTextStream s(stderr);
    return s;
}

const char* errorName(Error e)
{
    switch (e) {
    case Error::None: return "ok";
    case Error::NeedPassword: return "need-password";
    case Error::WrongPassword: return "wrong-password";
    case Error::PasswordCharset: return "password-charset";
    case Error::Corrupt: return "corrupt";
    case Error::Unsupported: return "unsupported";
    case Error::MissingVolume: return "missing-volume";
    case Error::NoSpace: return "no-space";
    case Error::NoAccess: return "no-access";
    case Error::NotFound: return "not-found";
    case Error::ReadOnlyFormat: return "read-only-format";
    case Error::Cancelled: return "cancelled";
    case Error::EngineMissing: return "engine-missing";
    case Error::Failed: return "failed";
    }
    return "?";
}

int report(const Status& st)
{
    for (const QString& w : st.warnings)
        err() << "warning: " << w << Qt::endl;
    if (st.ok())
        return 0;
    err() << "error: " << errorName(st.error);
    if (!st.detail.isEmpty())
        err() << "\n" << st.detail;
    err() << Qt::endl;
    return 2;
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("packix"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Packix archiver"));
    parser.addHelpOption();
    QCommandLineOption formatOpt(QStringLiteral("format"), QStringLiteral("zip, 7z, tar.gz, tar.xz"), QStringLiteral("id"));
    QCommandLineOption levelOpt(QStringLiteral("level"), QStringLiteral("store, fast, normal, max"), QStringLiteral("id"));
    parser.addOption(formatOpt);
    parser.addOption(levelOpt);
    parser.addPositionalArgument(QStringLiteral("command"), QStringLiteral("list | x | a | d | t"));
    parser.process(app);

    const QStringList args = parser.positionalArguments();
    if (args.size() < 2)
        parser.showHelp(1);
    const QString cmd = args.at(0);
    const QString archive = QFileInfo(args.at(1)).absoluteFilePath();
    const QString password = qEnvironmentVariable("PACKIX_PASSWORD");

    if (cmd == QLatin1String("list")) {
        ArchiveInfo info;
        const Status st = Archiver::open(archive, password, info);
        if (!st.ok())
            return report(st);
        out() << "format: " << info.formatId << " (" << info.engineType << ")"
              << (info.encryptedNames ? ", encrypted names" : "") << (info.anyEncrypted ? ", encrypted" : "")
              << ", volumes: " << info.volumes.size() << Qt::endl;
        for (const Item& i : std::as_const(info.items)) {
            out() << (i.isDir ? "D " : "  ") << (i.encrypted ? "* " : "  ") << i.size << "\t" << i.packed << "\t"
                  << i.path << (i.implied ? " (implied)" : "") << Qt::endl;
        }
        return report(st);
    }
    if (cmd == QLatin1String("x")) {
        if (args.size() < 3)
            parser.showHelp(1);
        Archiver::ExtractRequest req;
        req.archive = archive;
        req.destination = QFileInfo(args.at(2)).absoluteFilePath();
        req.items = args.mid(3);
        req.password = password;
        QStringList created;
        const Status st = Archiver::extract(req, nullptr, &created);
        for (const QString& c : std::as_const(created))
            out() << c << Qt::endl;
        return report(st);
    }
    if (cmd == QLatin1String("a")) {
        if (args.size() < 3)
            parser.showHelp(1);
        Archiver::CreateRequest req;
        for (const QString& s : args.mid(2))
            req.sources << QFileInfo(s).absoluteFilePath();
        req.destinationDir = QFileInfo(archive).absolutePath();
        req.fileName = QFileInfo(archive).fileName();
        req.formatId = parser.isSet(formatOpt) ? parser.value(formatOpt)
                                               : Formats::instance().detect(req.fileName).id;
        req.levelId = parser.isSet(levelOpt) ? parser.value(levelOpt) : Formats::instance().defaultLevel();
        req.password = password;
        QString result;
        const Status st = Archiver::create(req, nullptr, &result);
        if (st.ok())
            out() << result << Qt::endl;
        return report(st);
    }
    if (cmd == QLatin1String("d") || cmd == QLatin1String("t")) {
        ArchiveInfo info;
        Status st = Archiver::open(archive, password, info);
        if (!st.ok())
            return report(st);
        st = cmd == QLatin1String("d") ? Archiver::remove(info, args.mid(2), password)
                                       : Archiver::test(info, password);
        return report(st);
    }
    parser.showHelp(1);
}
