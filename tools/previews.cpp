// Development helper: renders Packix windows to PNG files for a quick look at the design.
// Usage: previews <out-dir>

#include "ArchiveWindow.h"
#include "Archiver.h"
#include "CreateDialog.h"
#include "Effects.h"
#include "ExtractDialog.h"
#include "Icons.h"
#include "Lang.h"
#include "Theme.h"
#include "Thermometer.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QPainter>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>

using namespace packix;
using namespace packix::ui;

namespace {

void write(const QString& path, const QByteArray& data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return;
    f.write(data);
}

void save(QWidget* w, const QString& file)
{
    w->adjustSize();
    w->grab().save(file);
}

} // namespace

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Packix"));
    // Own settings, so a preview never changes the language of the real Packix.
    QApplication::setOrganizationName(QStringLiteral("PackixPreviews"));
    const QString out = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral(".");
    QDir().mkpath(out);
    if (argc > 2)
        Lang::instance().setLanguage(QString::fromLocal8Bit(argv[2]));

    QTemporaryDir tmp;
    const QString src = tmp.filePath(QStringLiteral("Отпуск 2025"));
    write(src + QStringLiteral("/IMG_0012.jpg"), QByteArray(420000, 'j'));
    write(src + QStringLiteral("/IMG_0013.jpg"), QByteArray(380000, 'k'));
    write(src + QStringLiteral("/Видео/море.mp4"), QByteArray(900000, 'v'));
    write(src + QStringLiteral("/Документы/план поездки.pdf"), QByteArray(120000, 'p'));
    write(src + QStringLiteral("/Документы/билеты.txt"), "x");
    write(src + QStringLiteral("/Игры/game.nsp"), QByteArray(300000, 'n'));
    write(src + QStringLiteral("/readme.md"), "# hi");

    auto make = [&](const QString& name, const QString& fmt, const QString& pw) {
        Archiver::CreateRequest r;
        r.sources = {src};
        r.destinationDir = tmp.path();
        r.fileName = name;
        r.formatId = fmt;
        r.levelId = QStringLiteral("normal");
        r.password = pw;
        QString result;
        Archiver::create(r, nullptr, &result);
        return result;
    };
    const QString zip = make(QStringLiteral("Отпуск.zip"), QStringLiteral("zip"), QStringLiteral("abc"));
    const QString sevenZ = make(QStringLiteral("Секрет.7z"), QStringLiteral("7z"), {});
    ArchiveInfo info;
    Archiver::open(sevenZ, {}, info);
    Archiver::PasswordRequest pr;
    pr.newPassword = QStringLiteral("x");
    Archiver::changePassword(info, pr);

    {
        CreateDialog d({src});
        save(&d, out + QStringLiteral("/1-create-zip.png"));
    }
    {
        ExtractDialog d(zip, 0);
        save(&d, out + QStringLiteral("/2-extract.png"));
    }
    {
        auto* w = new ArchiveWindow(zip);
        w->start();
        QApplication::processEvents();
        w->grab().save(out + QStringLiteral("/3-archive.png"));
        w->close();
    }
    {
        auto* w = new ArchiveWindow(sevenZ);
        w->start();
        QApplication::processEvents();
        w->grab().save(out + QStringLiteral("/4-locked.png"));
        w->close();
    }
    {
        QWidget box;
        auto* layout = new QVBoxLayout(&box);
        for (double v : {0.0, 0.35, 0.8}) {
            auto* t = new Thermometer(&box);
            t->setValue(v);
            layout->addWidget(t);
        }
        box.resize(460, 140);
        box.show();
        for (int i = 0; i < 40; ++i) {   // let the liquid flow in
            QApplication::processEvents();
            QThread::msleep(16);
        }
        box.grab().save(out + QStringLiteral("/5-thermometer.png"));
        // Close-up of one, four times bigger.
        const QPixmap one = box.layout()->itemAt(1)->widget()->grab();
        one.scaled(one.size() * 4).save(out + QStringLiteral("/5b-thermometer-zoom.png"));
    }
    {
        QImage img(360, 120, QImage::Format_ARGB32_Premultiplied);
        img.fill(QApplication::palette().color(QPalette::Window));
        QPainter p(&img);
        paintBookLock(p, QRectF(10, 10, 96, 96), Theme::get().color("accent"), 0);
        paintBookLock(p, QRectF(120, 10, 96, 96), Theme::get().color("accent"), 1);
        p.drawPixmap(240, 30, trayIcon().pixmap(48, 48));
        p.drawPixmap(300, 30, globeIcon().pixmap(48, 48));
        p.end();
        img.save(out + QStringLiteral("/6-icons.png"));
    }
    return 0;
}
