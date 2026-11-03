// Development helper: renders Packix windows to PNG files for a quick look at the design.
// Usage: previews <out-dir>

#include "ArchiveWindow.h"
#include "Archiver.h"
#include "CreateDialog.h"
#include "Donate.h"
#include "Effects.h"
#include "ExtractDialog.h"
#include "Icons.h"
#include "Lang.h"
#include "PathField.h"
#include "Theme.h"
#include "Thermometer.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
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

// Sharp pictures for slides: drawn at 2× whatever screen the widget is (or is not yet) on.
qreal g_scale = 2.0;

QPixmap shot(QWidget* w)
{
    QPixmap pm(w->size() * g_scale);
    pm.setDevicePixelRatio(g_scale);
    pm.fill(Qt::transparent);
    w->render(&pm);
    return pm;
}

void save(QWidget* w, const QString& file)
{
    w->adjustSize();
    shot(w).save(file);
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
        // Filled in, as someone is about to press "Создать".
        CreateDialog d({src});
        d.show();
        const auto edits = d.findChildren<QLineEdit*>();
        edits.at(0)->setText(QStringLiteral("Отпуск 2025"));
        edits.at(1)->setText(QStringLiteral("Secret1"));
        edits.at(2)->setText(QStringLiteral("Secret1"));
        d.findChild<PathField*>()->setPath(QDir::homePath() + QStringLiteral("/Desktop"));
        // The fade effect of the password block is drawn off place by render() at 2×; not needed here.
        edits.at(1)->parentWidget()->setGraphicsEffect(nullptr);
        for (int i = 0; i < 30; ++i) {
            QApplication::processEvents();
            QThread::msleep(16);
        }
        save(&d, out + QStringLiteral("/1-create-zip.png"));
        d.hide();
    }
    {
        ExtractDialog d(zip, 0);
        save(&d, out + QStringLiteral("/2-extract.png"));
    }
    {
        auto* w = new ArchiveWindow(zip);
        w->start();
        QApplication::processEvents();
        shot(w).save(out + QStringLiteral("/3-archive.png"));
        w->close();
    }
    {
        auto* w = new ArchiveWindow(sevenZ);
        w->start();
        QApplication::processEvents();
        shot(w).save(out + QStringLiteral("/4-locked.png"));
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
        shot(&box).save(out + QStringLiteral("/5-thermometer.png"));
        // Close-up of one, four times bigger.
        const QPixmap one = shot(box.layout()->itemAt(1)->widget());
        one.scaled(one.size() * 4).save(out + QStringLiteral("/5b-thermometer-zoom.png"));
    }
    {
        // What a running job looks like (same layout as the real job window).
        QWidget job;
        job.setWindowTitle(L("job.creating", {{"name", QStringLiteral("Отпуск.zip")}}));
        Theme::get().apply(&job);
        auto* heading = new QLabel(job.windowTitle(), &job);
        QFont f = heading->font();
        f.setBold(true);
        f.setPointSizeF(f.pointSizeF() + 1);
        heading->setFont(f);
        auto* current = new QLabel(QStringLiteral("Отпуск 2025/Видео/море.mp4"), &job);
        auto* bar = new Thermometer(&job);
        bar->setValue(0.62);
        auto* detail = new QLabel(L("job.amount", {{"done", humanSize(1503238553)}, {"total", humanSize(2469606195)}})
                                      + QStringLiteral(" · ") + L("job.speed", {{"speed", humanSize(50331648)}})
                                      + QStringLiteral(" · ") + Ln("job.leftSec", 19),
                                  &job);
        detail->setForegroundRole(QPalette::PlaceholderText);
        auto* cancel = new QPushButton(L("common.cancel"), &job);
        auto* buttons = new QHBoxLayout;
        buttons->addStretch();
        buttons->addWidget(cancel);
        auto* layout = new QVBoxLayout(&job);
        layout->setContentsMargins(20, 18, 20, 16);
        layout->setSpacing(8);
        layout->addWidget(heading);
        layout->addWidget(current);
        layout->addWidget(bar);
        layout->addWidget(detail);
        layout->addLayout(buttons);
        job.resize(480, job.sizeHint().height());
        job.show();
        for (int i = 0; i < 90; ++i) {
            QApplication::processEvents();
            QThread::msleep(16);
        }
        shot(&job).save(out + QStringLiteral("/7-job.png"));
    }
    {
        showDonateWindow();
        for (int i = 0; i < 10; ++i) {
            QApplication::processEvents();
            QThread::msleep(16);
        }
        for (QWidget* w : QApplication::topLevelWidgets()) {
            if (w->isVisible() && w->windowTitle() == L("donate.windowTitle")) {
                save(w, out + QStringLiteral("/8-donate.png"));
                w->close();
            }
        }
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
