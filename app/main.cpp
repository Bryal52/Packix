#include "App.h"
#include "Lang.h"
#include "Log.h"
#include "Platform.h"
#include "Temp.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>
#include <QThread>
#include <QTimer>

#include <cstdio>
#include <cstdlib>

using namespace packix;

namespace {

QString instanceName()
{
    // Not "Packix-<user>": on macOS the socket lives in the temp folder, where Temp keeps a folder of that name.
    return QStringLiteral("packix-instance-") + qEnvironmentVariable("USER", qEnvironmentVariable("USERNAME"));
}

// Hands the request to the Packix that is already running. False if there is none.
bool sendToRunning(const QJsonObject& request)
{
    QLocalSocket socket;
    socket.connectToServer(instanceName());
    if (!socket.waitForConnected(300))
        return false;
    socket.write(QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n');
    socket.waitForBytesWritten(2000);
    socket.disconnectFromServer();
    return true;
}

} // namespace

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Packix"));
    QApplication::setOrganizationName(QStringLiteral("Packix"));
    QApplication::setApplicationVersion(QStringLiteral(PACKIX_VERSION));
    QApplication::setQuitOnLastWindowClosed(false);   // lives in the menu bar / tray
#ifndef Q_OS_MACOS
    // The Mac takes it from the bundle; elsewhere it lies next to the program.
    QApplication::setWindowIcon(QIcon(QApplication::applicationDirPath() + QStringLiteral("/packix.ico")));
#endif

    const QStringList args = QApplication::arguments();
    // --background: started at login, stays quiet. --archive: files to pack (Explorer's menu).
    const bool background = args.contains(QStringLiteral("--background"));
    const bool smokeTest = args.contains(QStringLiteral("--smoke-test"));
    QJsonObject request;
    request.insert(QStringLiteral("mode"), args.contains(QStringLiteral("--archive")) ? QStringLiteral("archive") : QStringLiteral("open"));
    QStringList files;
    for (const QString& a : args.mid(1)) {
        if (!a.startsWith(QLatin1String("-")) && QFileInfo::exists(a))
            files << QFileInfo(a).absoluteFilePath();
    }
    request.insert(QStringLiteral("files"), QJsonArray::fromStringList(files));

    // One Packix at a time: a later start passes its files on and quits. The lock decides who is
    // first when Explorer starts several copies at once.
    QLockFile lock(QDir::tempPath() + QLatin1Char('/') + instanceName() + QStringLiteral(".lock"));
    lock.setStaleLockTime(0);
    if (!smokeTest && !lock.tryLock(0)) {
        for (int attempt = 0; attempt < 30; ++attempt) {
            if (background || sendToRunning(request))
                return 0;
            QThread::msleep(100);
        }
        return 0;
    }

    ui::Log::install();
    Temp::cleanupStale();
    QObject::connect(&app, &QApplication::aboutToQuit, [] { Temp::cleanupSession(); });
    ui::Lang::instance();

    ui::App controller(background);
    platform::installServices([&controller](const QStringList& paths) { controller.archiveFiles(paths); });

    QLocalServer server;
    QLocalServer::removeServer(instanceName());
    server.listen(instanceName());
    QObject::connect(&server, &QLocalServer::newConnection, &controller, [&server, &controller] {
        while (QLocalSocket* socket = server.nextPendingConnection()) {
            QObject::connect(socket, &QLocalSocket::readyRead, socket, [socket, &controller] {
                while (socket->canReadLine()) {
                    const QJsonObject r = QJsonDocument::fromJson(socket->readLine()).object();
                    QStringList paths;
                    for (const QJsonValue& v : r.value(QStringLiteral("files")).toArray())
                        paths << v.toString();
                    controller.request(r.value(QStringLiteral("mode")).toString(), paths);
                }
            });
            QObject::connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
        }
    });

    if (!files.isEmpty()) {
        const QString mode = request.value(QStringLiteral("mode")).toString();
        QTimer::singleShot(0, &controller, [&controller, mode, files] { controller.request(mode, files); });
    }
    if (!background && !smokeTest) {
        QTimer::singleShot(0, &controller, [&controller] { controller.firstRunIfNeeded(); });
        // Finder delivers the file a moment after the start; only then is "idle" known.
        QTimer::singleShot(700, &controller, [&controller] { controller.showLauncherIfIdle(); });
    }
    if (smokeTest) {
        QTimer::singleShot(3000, &app, [] {
            std::printf("Packix %s started fine\n", PACKIX_VERSION);
            std::fflush(stdout);
            std::_Exit(0);
        });
    }
    return app.exec();
}
