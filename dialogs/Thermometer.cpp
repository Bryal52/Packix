#include "Thermometer.h"

#include "Theme.h"

#include <QPainter>
#include <QPainterPath>
#include <QRandomGenerator>
#include <QTimer>

#include <cmath>

namespace packix::ui {

Thermometer::Thermometer(QWidget* parent) : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setMinimumWidth(200);
    m_timer = new QTimer(this);
    m_timer->setTimerType(Qt::PreciseTimer);
    m_timer->setInterval(16);
    connect(m_timer, &QTimer::timeout, this, &Thermometer::tick);
    m_clock.start();
}

QSize Thermometer::sizeHint() const
{
    const int h = qRound(Theme::get().metric("thermometerHeight", 22) * 1.45);
    return {360, h};
}

void Thermometer::setValue(double value)
{
    if (m_mode != Mode::Normal)
        return;
    m_target = value < 0 ? -1 : qBound(0.0, value, 1.0);
}

void Thermometer::finish(std::function<void()> done)
{
    m_mode = Mode::Finishing;
    m_target = 1.0;
    m_done = std::move(done);
    if (Theme::reduceMotion() || !isVisible()) {
        m_shown = 1.0;
        update();
        if (m_done)
            QTimer::singleShot(0, this, std::exchange(m_done, {}));
    }
}

void Thermometer::drain(std::function<void()> done)
{
    m_mode = Mode::Draining;
    m_target = 0;
    m_done = std::move(done);
    if (Theme::reduceMotion() || !isVisible()) {
        m_shown = 0;
        update();
        if (m_done)
            QTimer::singleShot(0, this, std::exchange(m_done, {}));
    }
}

void Thermometer::setFailed(bool failed)
{
    m_failed = failed;
    update();
}

void Thermometer::showEvent(QShowEvent*)
{
    m_last = m_clock.elapsed();
    m_timer->start();
}

void Thermometer::hideEvent(QHideEvent*)
{
    m_timer->stop();
}

void Thermometer::tick()
{
    const Theme& th = Theme::get();
    const qint64 now = m_clock.elapsed();
    const double dt = double(qMax<qint64>(1, now - m_last));
    m_last = now;

    if (m_failed) {
        update();
        return;   // frozen
    }
    const double target = m_target < 0 ? 0.03 + 0.02 * std::sin(now / 380.0) : m_target;
    if (m_mode == Mode::Draining) {
        m_shown = qMax(0.0, m_shown - dt / qMax(1, th.motion("drainMs", 200)) * qMax(m_shown, 0.15) * 1.6);
        if (m_shown <= 0.001 && m_done)
            std::exchange(m_done, {})();
    } else {
        // Exponential catch-up: fast for big steps, gentle at the end.
        const double tau = qMax(1, th.motion("liquidCatchUpMs", 220)) / 3.0;
        m_shown += (target - m_shown) * (1.0 - std::exp(-dt / tau));
        if (m_mode == Mode::Finishing && m_shown > 0.995) {
            m_shown = 1.0;
            if (m_shine < 0)
                m_shine = 0;
        }
    }
    if (m_shine >= 0) {
        m_shine += dt / qMax(1, th.motion("finishShineMs", 200));
        if (m_shine >= 1.0) {
            m_shine = -1;
            if (m_done)
                std::exchange(m_done, {})();
        }
    }

    // Bubbles rise in the bulb now and then.
    if (!Theme::reduceMotion()) {
        if (now >= m_nextBubble) {
            auto* rng = QRandomGenerator::global();
            m_bubbles.append({QPointF(rng->bounded(1.0) - 0.5, 0.75), 0.06 + rng->bounded(0.05), 0.0011 + rng->bounded(0.0008)});
            const int every = th.motion("bubbleEveryMs", 1400);
            m_nextBubble = now + every / 2 + rng->bounded(qMax(1, every));
        }
        for (Bubble& b : m_bubbles)
            b.pos.ry() -= b.speed * dt;
        m_bubbles.removeIf([](const Bubble& b) { return b.pos.y() < -0.8; });
    }
    update();
}

void Thermometer::paintEvent(QPaintEvent*)
{
    const Theme& th = Theme::get();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const qreal H = height();
    const qreal D = H - 2;                // bulb diameter
    const qreal T = H * 0.52;             // tube thickness
    const QPointF bulbC(1 + D / 2, H / 2);
    const QRectF tube(bulbC.x(), (H - T) / 2, width() - bulbC.x() - 2, T);

    QPainterPath glass;
    glass.addEllipse(bulbC, D / 2, D / 2);
    glass.addRoundedRect(tube, T / 2, T / 2);
    glass.setFillRule(Qt::WindingFill);
    glass = glass.simplified();

    p.setPen(QPen(th.color("glassEdge"), 1.0));
    p.setBrush(th.color("glass"));
    p.drawPath(glass);

    // Liquid, inset from the glass.
    const qreal inset = qMax<qreal>(2.0, T * 0.12);
    const qreal tubeTop = tube.top() + inset;
    const qreal tubeBottom = tube.bottom() - inset;
    const qreal startX = bulbC.x();
    const qreal endX = tube.right() - inset;
    const qreal front = startX + (endX - startX - (tubeBottom - tubeTop) / 2) * m_shown;
    const double t = m_clock.elapsed();
    const bool calm = Theme::reduceMotion() || m_failed;

    QPainterPath liquid;
    liquid.addEllipse(bulbC, D / 2 - inset, D / 2 - inset);
    QPainterPath column;
    column.moveTo(startX, tubeTop);
    // The front edge ripples a little.
    const qreal wave = calm ? 0 : th.motionF("waveHeight", 1.6);
    const double period = qMax(1, th.motion("wavePeriodMs", 1800));
    // Rounded front (a meniscus) that ripples a little.
    const qreal r = (tubeBottom - tubeTop) / 2;
    const int steps = 18;
    for (int i = 0; i <= steps; ++i) {
        const qreal u = qreal(i) / steps;
        const qreal y = tubeTop + (tubeBottom - tubeTop) * u;
        const qreal bulge = r * 0.8 * std::sqrt(qMax<qreal>(0, 1 - (2 * u - 1) * (2 * u - 1)));
        const qreal ripple = wave * std::sin(u * 4.0 + t / period * 2 * M_PI) * std::sin(M_PI * u);
        column.lineTo(front + bulge + ripple, y);
    }
    column.lineTo(startX, tubeBottom);
    column.closeSubpath();
    liquid.addPath(column);
    liquid.setFillRule(Qt::WindingFill);

    // Saturated, slightly see-through red: lighter at the top, deep at the bottom, like mercury-red spirit.
    QLinearGradient grad(0, bulbC.y() - D / 2, 0, bulbC.y() + D / 2);
    const QColor top = m_failed ? th.color("liquidDim") : th.color("liquidTop");
    const QColor bottom = m_failed ? th.color("liquidDim").darker(115) : th.color("liquidBottom");
    grad.setColorAt(0, top);
    grad.setColorAt(0.45, top);
    grad.setColorAt(1, bottom);
    const QPainterPath liquidShape = liquid.simplified();
    p.setPen(Qt::NoPen);
    p.setBrush(grad);
    p.drawPath(liquidShape);
    // A glassy highlight inside the liquid along its upper side.
    if (!m_failed) {
        p.save();
        p.setClipPath(liquidShape);
        QLinearGradient hl(0, tubeTop, 0, tubeTop + (tubeBottom - tubeTop) * 0.5);
        hl.setColorAt(0, th.color("liquidHighlight"));
        hl.setColorAt(1, QColor(255, 255, 255, 0));
        p.setBrush(hl);
        p.drawRect(QRectF(0, tubeTop, width(), (tubeBottom - tubeTop) * 0.5));
        p.restore();
    }

    // Bubbles inside the bulb.
    p.setBrush(QColor(255, 255, 255, 120));
    const qreal br = D / 2 - inset;
    for (const Bubble& b : std::as_const(m_bubbles))
        p.drawEllipse(bulbC + QPointF(b.pos.x() * br, b.pos.y() * br), b.radius * br, b.radius * br);

    // Shine along the top of the glass, and a highlight on the bulb.
    p.setPen(QPen(th.color("glassShine"), qMax<qreal>(1.2, T * 0.12), Qt::SolidLine, Qt::RoundCap));
    p.drawLine(QPointF(tube.left() + D * 0.45, tube.top() + T * 0.24), QPointF(tube.right() - T * 0.6, tube.top() + T * 0.24));
    p.setBrush(Qt::NoBrush);
    p.drawArc(QRectF(bulbC.x() - D * 0.3, bulbC.y() - D * 0.3, D * 0.6, D * 0.6), 100 * 16, 60 * 16);

    // Finish: a bright band sweeps once over the glass.
    if (m_shine >= 0) {
        const qreal x = tube.left() + (tube.width() + 60) * m_shine - 30;
        QLinearGradient sweep(x - 30, 0, x + 30, 0);
        sweep.setColorAt(0, QColor(255, 255, 255, 0));
        sweep.setColorAt(0.5, QColor(255, 255, 255, 150));
        sweep.setColorAt(1, QColor(255, 255, 255, 0));
        p.setPen(Qt::NoPen);
        p.setBrush(sweep);
        p.setClipPath(glass);
        p.drawRect(rect());
    }
}

} // namespace packix::ui
