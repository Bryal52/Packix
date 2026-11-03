#include "App.h"
#include "Lang.h"
#include "Log.h"
#include "Platform.h"
#include "Temp.h"

#include <QApplication>
#include <QFileInfo>
#include <QIcon>
#include <QTimer>

#include <cstdio>
#include <cstdlib>

using namespace packix;

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Packix"));
    QApplication::setOrganizationName(QStringLiteral("Packix"));
    QApplication::setApplicationVersion(QStringLiteral(PACKIX_VERSION));
    QApplication::setQuitOnLastWindowClosed(false);   // lives in the menu bar
#ifndef Q_OS_MACOS
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icon.png")));
#endif
    ui::Log::install();
    Temp::cleanupStale();
    QObject::connect(&app, &QApplication::aboutToQuit, [] { Temp::cleanupSession(); });
    ui::Lang::instance();

    const QStringList args = QApplication::arguments();
    // --background: started at login, stays quiet.
    const bool background = args.contains(QStringLiteral("--background"));
    ui::App controller(background);
    platform::installServices([&controller](const QStringList& files) { controller.archiveFiles(files); });

    for (const QString& a : args.mid(1)) {
        if (!a.startsWith(QLatin1String("-")) && QFileInfo::exists(a))
            QTimer::singleShot(0, &controller, [&controller, a] { controller.openArchive(a); });
    }
    if (!background) {
        QTimer::singleShot(0, &controller, [&controller] { controller.firstRunIfNeeded(); });
        // Finder delivers the file a moment after the start; only then is "idle" known.
        QTimer::singleShot(700, &controller, [&controller] { controller.showLauncherIfIdle(); });
    }
    if (args.contains(QStringLiteral("--smoke-test"))) {
        QTimer::singleShot(3000, &app, [] {
            std::printf("Packix %s started fine\n", PACKIX_VERSION);
            std::fflush(stdout);
            std::_Exit(0);
        });
    }
    return app.exec();
}
