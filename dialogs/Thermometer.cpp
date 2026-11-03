#include "Thermometer.h"

#include "Theme.h"

#include <QPainter>
#include <QPainterPath>
#include <QTimer>

#include <cmath>
#include <tuple>

namespace packix::ui {

Thermometer::Thermometer(QWidget* parent) : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_timer = new QTimer(this);
    m_timer->setTimerType(Qt::PreciseTimer);
    m_timer->setInterval(16);
    connect(m_timer, &QTimer::timeout, this, &Thermometer::tick);
    m_clock.start();
}

QSize Thermometer::sizeHint() const
{
    return {360, Theme::get().metric("thermometerHeight", 7) + 4};
}

QSize Thermometer::minimumSizeHint() const
{
    return {120, sizeHint().height()};
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

// `done` callbacks run after the current frame: they may close the window this widget lives in.
void Thermometer::tick()
{
    const Theme& th = Theme::get();
    const qint64 now = m_clock.elapsed();
    const double dt = double(qBound(qint64(1), now - m_last, qint64(100)));
    m_last = now;

    if (!m_failed) {
        const double target = m_target < 0 ? 0.04 + 0.025 * std::sin(now / 420.0) : m_target;
        if (m_mode == Mode::Draining) {
            m_shown = qMax(0.0, m_shown - dt / qMax(1, th.motion("drainMs", 200)) * qMax(m_shown, 0.15) * 1.6);
            if (m_shown <= 0.001 && m_done)
                QTimer::singleShot(0, std::exchange(m_done, {}));
        } else {
            // A critically damped spring: the liquid keeps flowing between progress reports
            // instead of moving in steps, and never overshoots.
            const double omega = 2000.0 / qMax(1, th.motion("liquidCatchUpMs", 600));   // per second
            const double h = dt / 1000.0;
            const double x = m_shown - target;
            m_velocity += (-omega * omega * x - 2.0 * omega * m_velocity) * h;
            m_shown = qBound(0.0, m_shown + m_velocity * h, 1.0);
            if (m_mode == Mode::Finishing && m_shown > 0.995) {
                m_shown = 1.0;
                m_velocity = 0;
                if (m_shine < 0)
                    m_shine = 0;
            }
        }
        if (m_shine >= 0) {
            m_shine += dt / qMax(1, th.motion("finishShineMs", 200));
            if (m_shine >= 1.0) {
                m_shine = -1;
                if (m_done)
                    QTimer::singleShot(0, std::exchange(m_done, {}));
            }
        }
    }
    update();
}

void Thermometer::paintEvent(QPaintEvent*)
{
    const Theme& th = Theme::get();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const qreal T = th.metric("thermometerHeight", 7);
    const QRectF tube(1, (height() - T) / 2.0, width() - 2, T);
    QPainterPath glass;
    glass.addRoundedRect(tube, T / 2, T / 2);
    p.setPen(QPen(th.color("glassEdge"), 1.0));
    p.setBrush(th.color("glass"));
    p.drawPath(glass);

    const QRectF inner = tube.adjusted(1, 1, -1, -1);
    const qreal r = inner.height() / 2;
    // A round drop is always there, so the bar never looks empty while working.
    const qreal len = inner.height() + (inner.width() - inner.height()) * m_shown;
    QPainterPath liquid;
    liquid.addRoundedRect(QRectF(inner.left(), inner.top(), len, inner.height()), r, r);

    const QColor top = m_failed ? th.color("liquidDim") : th.color("liquidTop");
    const QColor bottom = m_failed ? th.color("liquidDim").darker(115) : th.color("liquidBottom");
    QLinearGradient grad(0, inner.top(), 0, inner.bottom());
    grad.setColorAt(0, top);
    grad.setColorAt(1, bottom);
    p.setPen(Qt::NoPen);
    p.setBrush(grad);
    p.drawPath(liquid);

    p.save();
    p.setClipPath(liquid);
    if (!Theme::reduceMotion() && !m_failed) {
        // Water-like shimmer: two soft light bands at different speeds drift along the liquid
        // and overlap, so the glow keeps changing instead of repeating one stripe.
        const double t = m_clock.elapsed() / 1000.0;
        const double speed = th.motionF("shimmerSpeed", 90);      // px per second
        const double period = th.motionF("shimmerPeriod", 140);   // px
        for (const auto& [mul, alpha, phase] : {std::tuple{1.0, 70, 0.0}, std::tuple{0.62, 45, 0.37}}) {
            const double span = period / mul;
            const double offset = std::fmod(t * speed * mul + phase * span, span);
            QLinearGradient band(offset, 0, offset + span, 0);
            band.setSpread(QGradient::RepeatSpread);
            band.setColorAt(0.0, QColor(255, 255, 255, 0));
            band.setColorAt(0.45, QColor(255, 255, 255, alpha));
            band.setColorAt(0.55, QColor(255, 255, 255, alpha));
            band.setColorAt(1.0, QColor(255, 255, 255, 0));
            p.fillRect(liquid.boundingRect(), band);
        }
    }
    // Glassy highlight along the top of the liquid.
    if (!m_failed) {
        QLinearGradient hl(0, inner.top(), 0, inner.center().y());
        hl.setColorAt(0, th.color("liquidHighlight"));
        hl.setColorAt(1, QColor(255, 255, 255, 0));
        p.fillRect(QRectF(inner.left(), inner.top(), len, inner.height() / 2), hl);
    }
    p.restore();

    // Finish: a bright band sweeps once over the glass.
    if (m_shine >= 0) {
        const qreal x = tube.left() + (tube.width() + 80) * m_shine - 40;
        QLinearGradient sweep(x - 40, 0, x + 40, 0);
        sweep.setColorAt(0, QColor(255, 255, 255, 0));
        sweep.setColorAt(0.5, QColor(255, 255, 255, 170));
        sweep.setColorAt(1, QColor(255, 255, 255, 0));
        p.setClipPath(glass);
        p.fillRect(rect(), sweep);
    }
}

} // namespace packix::ui
