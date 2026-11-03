#include "Preview.h"

#include "Archiver.h"
#include "Icons.h"
#include "Lang.h"
#include "Temp.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QGuiApplication>
#include <QImageReader>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QProcess>
#include <QScreen>
#include <QStandardPaths>
#include <QThreadPool>
#include <QVBoxLayout>
#include <QtConcurrent>

namespace packix::ui {

namespace {

const QSize kMaxPreview(360, 270);
constexpr qint64 kMaxImageBytes = 64LL << 20;
constexpr qint64 kMaxVideoBytes = 500LL << 20;   // bigger ones take too long to unpack for a hover

QString findTool(const QString& name)
{
    for (const QString& dir : {QStringLiteral("/opt/homebrew/bin"), QStringLiteral("/usr/local/bin")}) {
        const QFileInfo fi(dir + QLatin1Char('/') + name);
        if (fi.isExecutable())
            return fi.absoluteFilePath();
    }
    return QStandardPaths::findExecutable(name);
}

QByteArray run(const QString& program, const QStringList& args, int timeoutMs)
{
    if (program.isEmpty())
        return {};
    QProcess p;
    p.start(program, args);
    if (!p.waitForFinished(timeoutMs)) {
        p.kill();
        p.waitForFinished(2000);
    }
    return p.readAllStandardOutput();
}

QImage loadImage(const QString& path, QStringList* lines)
{
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QSize full = reader.size();
    if (full.isValid()) {
        lines->append(QStringLiteral("%1 × %2").arg(full.width()).arg(full.height()));
        if (full.width() > kMaxPreview.width() * 2 || full.height() > kMaxPreview.height() * 2)
            reader.setScaledSize(full.scaled(kMaxPreview * 2, Qt::KeepAspectRatio));
    }
    return reader.read();
}

QString formatDuration(double secs)
{
    const int s = int(secs + 0.5);
    if (s >= 3600)
        return QStringLiteral("%1:%2:%3").arg(s / 3600).arg((s / 60) % 60, 2, 10, QLatin1Char('0')).arg(s % 60, 2, 10, QLatin1Char('0'));
    return QStringLiteral("%1:%2").arg(s / 60, 2, 10, QLatin1Char('0')).arg(s % 60, 2, 10, QLatin1Char('0'));
}

QStringList frameArgs(double at, const QString& input)
{
    QStringList args{QStringLiteral("-v"), QStringLiteral("error")};
    if (at > 0)
        args << QStringLiteral("-ss") << QString::number(at, 'f', 2);
    args << QStringLiteral("-i") << input << QStringLiteral("-an") << QStringLiteral("-sn")
         << QStringLiteral("-frames:v") << QStringLiteral("1")
         << QStringLiteral("-vf") << QStringLiteral("scale='min(720,iw)':-2")
         << QStringLiteral("-f") << QStringLiteral("image2pipe") << QStringLiteral("-vcodec") << QStringLiteral("mjpeg")
         << QStringLiteral("-q:v") << QStringLiteral("4") << QStringLiteral("pipe:1");
    return args;
}

PreviewData buildVideo(const QString& path)
{
    PreviewData d;
    const QString ffmpeg = findTool(QStringLiteral("ffmpeg"));
    if (ffmpeg.isEmpty()) {
        d.lines << L("preview.needFfmpeg");
        return d;
    }
    double duration = 0;
    int w = 0, h = 0;
    const QByteArray probe = run(findTool(QStringLiteral("ffprobe")),
        {QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-select_streams"), QStringLiteral("v:0"),
         QStringLiteral("-show_entries"), QStringLiteral("stream=width,height:format=duration"),
         QStringLiteral("-of"), QStringLiteral("default=nw=1"), path},
        20000);
    for (const QByteArray& line : probe.split('\n')) {
        const QByteArray l = line.trimmed();
        if (l.startsWith("width="))
            w = l.mid(6).toInt();
        else if (l.startsWith("height="))
            h = l.mid(7).toInt();
        else if (l.startsWith("duration="))
            duration = l.mid(9).toDouble();
    }
    if (w > 0 && h > 0)
        d.lines << QStringLiteral("%1 × %2").arg(w).arg(h);
    if (duration > 0)
        d.lines << formatDuration(duration);
    d.image = QImage::fromData(run(ffmpeg, frameArgs(duration > 0 ? qMin(duration * 0.2, 90.0) : 1.0, path), 25000));
    if (d.image.isNull())
        d.image = QImage::fromData(run(ffmpeg, frameArgs(0, path), 25000));
    return d;
}

PreviewData build(const ArchiveInfo& info, const QString& password, const Item& item, Progress* progress)
{
    PreviewData d;
    const FileCat cat = classify(item.path.section(QLatin1Char('/'), -1), false).cat;
    if (item.encrypted && password.isEmpty()) {
        d.lines << L("preview.locked");
        return d;
    }
    if ((cat == FileCat::Image && item.size > kMaxImageBytes) || (cat == FileCat::Video && item.size > kMaxVideoBytes)) {
        d.lines << L("preview.tooBig");
        return d;
    }
    const QString dir = Temp::newDir(QStringLiteral("preview"));
    const Status st = engine()->extract(info.path, Archiver::formatOf(info), dir, {item.path}, password, progress);
    const QString file = QDir(dir).filePath(item.path);
    if (st.ok()) {
        if (cat == FileCat::Video)
            d = buildVideo(file);
        else
            d.image = loadImage(file, &d.lines);
    }
    QDir(dir).removeRecursively();
    return d;
}

} // namespace

bool PreviewPopup::wants(const QString& name, bool isDir)
{
    if (isDir)
        return false;
    const FileCat cat = classify(name, false).cat;
    return cat == FileCat::Image || cat == FileCat::Video;
}

PreviewPopup* PreviewPopup::instance()
{
    static PreviewPopup* popup = new PreviewPopup;
    return popup;
}

PreviewPopup::PreviewPopup() : QFrame(nullptr, Qt::ToolTip | Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);

    m_image = new QLabel(this);
    m_image->setAlignment(Qt::AlignCenter);
    m_name = new QLabel(this);
    m_name->setTextFormat(Qt::PlainText);
    m_name->setWordWrap(true);
    m_name->setMaximumWidth(kMaxPreview.width());
    QFont f = m_name->font();
    f.setBold(true);
    m_name->setFont(f);
    m_info = new QLabel(this);
    m_info->setTextFormat(Qt::PlainText);
    m_info->setWordWrap(true);
    m_info->setMaximumWidth(kMaxPreview.width());
    m_info->setForegroundRole(QPalette::PlaceholderText);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 10);
    layout->setSpacing(6);
    layout->addWidget(m_image, 0, Qt::AlignHCenter);
    layout->addWidget(m_name);
    layout->addWidget(m_info);
}

void PreviewPopup::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QColor bg = palette().color(QPalette::Window);
    bg.setAlpha(246);
    QPainterPath path;
    path.addRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 12, 12);
    p.fillPath(path, bg);
    p.setPen(QPen(palette().color(QPalette::Mid), 1));
    p.drawPath(path);
}

void PreviewPopup::showFor(const ArchiveInfo& info, const QString& password, const Item& item, const QPoint& globalPos)
{
    const quint64 gen = ++m_gen;
    m_anchor = globalPos;
    if (m_running)
        m_running->cancel = true;   // the previous file is no longer wanted

    const QString key = info.path + QLatin1Char('|') + item.path + QLatin1Char('|') + QString::number(item.size)
        + QLatin1Char('|') + QString::number(item.modified.toSecsSinceEpoch());
    if (const PreviewData* cached = m_cache.object(key)) {
        fill(*cached, item);
        return;
    }

    PreviewData loading;
    loading.lines << L("preview.loading");
    fill(loading, item);

    static QThreadPool* pool = [] {
        auto* p = new QThreadPool;
        p->setMaxThreadCount(2);
        return p;
    }();
    auto progress = std::make_shared<Progress>();
    m_running = progress;
    auto* watcher = new QFutureWatcher<PreviewData>(this);
    connect(watcher, &QFutureWatcher<PreviewData>::finished, this, [this, watcher, gen, key, item, progress] {
        const PreviewData d = watcher->result();
        watcher->deleteLater();
        if (progress->cancel)
            return;
        m_cache.insert(key, new PreviewData(d));
        if (gen == m_gen && isVisible())
            fill(d, item);
    });
    watcher->setFuture(QtConcurrent::run(pool, [info, password, item, progress] {
        return build(info, password, item, progress.get());
    }));
}

void PreviewPopup::fill(const PreviewData& d, const Item& item)
{
    const QString name = item.path.section(QLatin1Char('/'), -1);
    const TypeInfo t = classify(name, false);
    if (!d.image.isNull()) {
        const qreal dpr = devicePixelRatioF();
        QPixmap pm = QPixmap::fromImage(d.image.scaled(kMaxPreview * dpr, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        pm.setDevicePixelRatio(dpr);
        m_image->setPixmap(pm);
    } else {
        m_image->setPixmap(fileIcon(t.cat).pixmap(96, 96));
    }
    m_name->setText(name);
    QStringList info{t.description, humanSize(item.size)};
    info += d.lines;
    m_info->setText(info.join(QStringLiteral(" · ")));
    adjustSize();
    place();
    show();
    raise();
}

void PreviewPopup::place()
{
    QPoint pos = m_anchor + QPoint(18, 18);
    if (const QScreen* screen = QGuiApplication::screenAt(m_anchor)) {
        const QRect avail = screen->availableGeometry();
        if (pos.x() + width() > avail.right())
            pos.setX(m_anchor.x() - width() - 12);
        if (pos.y() + height() > avail.bottom())
            pos.setY(qMax(avail.top(), avail.bottom() - height()));
    }
    move(pos);
}

void PreviewPopup::hidePopup()
{
    ++m_gen;
    if (m_running)
        m_running->cancel = true;
    hide();
}

} // namespace packix::ui
