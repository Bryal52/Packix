#include "Effects.h"

#include "Icons.h"
#include "Theme.h"

#include <QDragEnterEvent>
#include <QEasingCurve>
#include <QMimeData>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QSequentialAnimationGroup>
#include <QTimer>
#include <QVariantAnimation>

#include <cmath>

namespace packix::ui {

void shake(QWidget* window)
{
    if (!window || Theme::reduceMotion())
        return;
    QWidget* w = window->window();
    const int ms = Theme::get().motion("shakeMs", 250);
    const int count = Theme::get().motion("shakeCount", 3);
    const int amp = Theme::get().motion("shakeAmplitude", 8);
    const QPoint origin = w->pos();
    auto* anim = new QVariantAnimation(w);
    anim->setDuration(ms);
    anim->setStartValue(0.0);
    anim->setEndValue(1.0);
    QObject::connect(anim, &QVariantAnimation::valueChanged, w, [w, origin, count, amp](const QVariant& v) {
        const double t = v.toDouble();
        const double dx = std::sin(t * count * 2 * M_PI) * amp * (1.0 - t);   // dies out
        w->move(origin + QPoint(int(std::lround(dx)), 0));
    });
    QObject::connect(anim, &QVariantAnimation::finished, w, [w, origin] { w->move(origin); });
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

// ---------------------------------------------------------------------------

namespace {

class PackOverlay : public QWidget {
public:
    explicit PackOverlay(const QRect& area)
        : QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::WindowTransparentForInput
                               | Qt::WindowDoesNotAcceptFocus)
    {
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_ShowWithoutActivating);
        setAttribute(Qt::WA_DeleteOnClose);
        const int side = qMin(160, qMin(area.width(), area.height()));
        setGeometry(QRect(area.center() - QPoint(side / 2, side / 2), QSize(side, side)));

        auto* anim = new QVariantAnimation(this);
        anim->setDuration(Theme::get().motion("packMs", 250));
        anim->setStartValue(0.0);
        anim->setEndValue(1.0);
        anim->setEasingCurve(QEasingCurve::OutCubic);
        connect(anim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
            m_t = v.toDouble();
            update();
        });
        connect(anim, &QVariantAnimation::finished, this, &QWidget::close);
        anim->start(QAbstractAnimation::DeleteWhenStopped);
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const qreal s = width() / 100.0;
        p.scale(s, s);
        const QColor accent = Theme::get().color("accent");
        // Fade out over the last fifth.
        p.setOpacity(m_t < 0.8 ? 1.0 : (1.0 - m_t) / 0.2);

        // Box at the bottom; its lid swings shut after the files are in.
        const QRectF box(28, 56, 44, 30);
        // Files: start above and to the sides, slide into the box opening.
        const QPointF starts[] = {{20, 10}, {44, 4}, {68, 12}};
        const qreal fileT = qMin(1.0, m_t / 0.7);
        for (int i = 0; i < 3; ++i) {
            const qreal local = qBound(0.0, fileT * 1.25 - i * 0.12, 1.0);
            const QPointF end(50 - 7 + (i - 1) * 6, 58);
            const QPointF pos = starts[i] * (1 - local) + end * local;
            const qreal scale = 1.0 - 0.35 * local;
            QRectF page(pos, QSizeF(14 * scale, 18 * scale));
            p.setPen(QPen(QColor(0, 0, 0, 50), 0.8));
            p.setBrush(QColor(255, 255, 255, 245));
            p.drawRoundedRect(page, 2, 2);
            p.setPen(QPen(accent, 1.4, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(page.topLeft() + QPointF(3, 5 * scale), page.topRight() + QPointF(-3, 5 * scale));
        }
        p.setPen(Qt::NoPen);
        QLinearGradient g(box.topLeft(), box.bottomLeft());
        g.setColorAt(0, accent.lighter(115));
        g.setColorAt(1, accent.darker(110));
        p.setBrush(g);
        p.drawRoundedRect(box, 5, 5);
        // lid: open (tilted back) until 70 %, then closes
        const qreal lidT = qBound(0.0, (m_t - 0.55) / 0.3, 1.0);
        p.save();
        p.translate(box.left(), box.top());
        p.rotate(-70 * (1 - lidT));
        p.setBrush(accent.darker(118));
        p.drawRoundedRect(QRectF(-2, -6, box.width() + 4, 7), 3, 3);
        p.restore();
    }

private:
    qreal m_t = 0;
};

} // namespace

void playPackAnimation(const QRect& globalArea)
{
    if (Theme::reduceMotion() || globalArea.isEmpty())
        return;
    (new PackOverlay(globalArea))->show();
}

// ---------------------------------------------------------------------------

BookLockWidget::BookLockWidget(int size, QWidget* parent) : QWidget(parent)
{
    setFixedSize(size, size);
}

void BookLockWidget::setOpen(bool open)
{
    m_open = open ? 1.0 : 0.0;
    update();
}

void BookLockWidget::animateOpen(std::function<void()> done)
{
    if (Theme::reduceMotion()) {
        setOpen(true);
        if (done)
            QTimer::singleShot(0, this, done);
        return;
    }
    if (!m_anim) {
        m_anim = new QVariantAnimation(this);
        connect(m_anim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
            m_open = v.toDouble();
            update();
        });
    }
    m_anim->stop();
    m_anim->setDuration(Theme::get().motion("claspMs", 200));
    m_anim->setStartValue(m_open);
    m_anim->setEndValue(1.0);
    m_anim->setEasingCurve(QEasingCurve::OutBack);   // a little snap past the end
    if (done) {
        QPointer<BookLockWidget> self(this);
        connect(m_anim, &QVariantAnimation::finished, this, [self, done] {
            if (self)
                QTimer::singleShot(60, self, done);
        }, Qt::SingleShotConnection);
    }
    m_anim->start();
}

void BookLockWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    paintBookLock(p, QRectF(rect()), Theme::get().color("accent"), m_open);
}

// ---------------------------------------------------------------------------

DropZone::DropZone(QWidget* parent) : QWidget(parent)
{
    setAcceptDrops(true);
    setMinimumHeight(140);
    m_anim = new QVariantAnimation(this);
    m_anim->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_anim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
        m_glow = v.toDouble();
        update();
    });
}

void DropZone::setText(const QString& text)
{
    m_text = text;
    update();
}

void DropZone::setHot(bool hot)
{
    const qreal target = hot ? 1.0 : 0.0;
    if (Theme::reduceMotion()) {
        m_glow = target;
        update();
        return;
    }
    m_anim->stop();
    m_anim->setDuration(Theme::get().motion("dropGlowMs", 150));
    m_anim->setStartValue(m_glow);
    m_anim->setEndValue(target);
    m_anim->start();
}

void DropZone::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QColor zone = Theme::get().color("dropZone");
    const QColor idle = palette().color(QPalette::PlaceholderText);
    auto mix = [](const QColor& a, const QColor& b, qreal t) {
        return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t, a.greenF() + (b.greenF() - a.greenF()) * t,
                                a.blueF() + (b.blueF() - a.blueF()) * t, a.alphaF() + (b.alphaF() - a.alphaF()) * t);
    };
    const QRectF r = QRectF(rect()).adjusted(10, 10, -10, -10);
    const int radius = Theme::get().metric("radius", 8) + 4;

    if (m_glow > 0) {   // soft glow: a few widening, fading outlines
        for (int i = 4; i >= 1; --i) {
            QColor c = zone;
            c.setAlphaF(0.07 * m_glow);
            p.setPen(QPen(c, i * 3.0));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(r, radius, radius);
        }
        QColor fill = zone;
        fill.setAlphaF(0.06 * m_glow);
        p.setPen(Qt::NoPen);
        p.setBrush(fill);
        p.drawRoundedRect(r, radius, radius);
    }
    QPen dash(mix(idle, zone, m_glow), 1.6, Qt::DashLine, Qt::RoundCap);
    dash.setDashPattern({5, 4});
    p.setPen(dash);
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(r, radius, radius);

    p.setPen(mix(idle, zone, m_glow));
    p.drawText(r.adjusted(16, 16, -16, -16), Qt::AlignCenter | Qt::TextWordWrap, m_text);
}

void DropZone::dragEnterEvent(QDragEnterEvent* e)
{
    if (e->mimeData()->hasUrls()) {
        e->acceptProposedAction();
        setHot(true);
    }
}

void DropZone::dragLeaveEvent(QDragLeaveEvent*)
{
    setHot(false);
}

void DropZone::dropEvent(QDropEvent* e)
{
    setHot(false);
    QStringList paths;
    for (const QUrl& u : e->mimeData()->urls()) {
        if (u.isLocalFile())
            paths << u.toLocalFile();
    }
    if (!paths.isEmpty()) {
        e->acceptProposedAction();
        emit filesDropped(paths);
    }
}

} // namespace packix::ui
