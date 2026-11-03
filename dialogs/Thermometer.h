#pragma once

#include <QElapsedTimer>
#include <QList>
#include <QPointF>
#include <QWidget>

#include <functional>

class QTimer;

namespace packix::ui {

// Progress bar as a mercury thermometer: a glass tube with a bulb, red liquid with a moving
// front edge, a few bubbles. It flows to new values instead of jumping.
class Thermometer : public QWidget {
    Q_OBJECT
public:
    explicit Thermometer(QWidget* parent = nullptr);

    // 0..1; below 0 = still counting (liquid sways in the bulb).
    void setValue(double value);
    // Fills up, a shine runs over the glass, then `done`.
    void finish(std::function<void()> done = {});
    // Cancel: the liquid runs back into the bulb, then `done`.
    void drain(std::function<void()> done = {});
    void setFailed(bool failed);

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent*) override;
    void showEvent(QShowEvent*) override;
    void hideEvent(QHideEvent*) override;

private:
    enum class Mode { Normal, Finishing, Draining };
    struct Bubble {
        QPointF pos;   // in bulb units: -1..1
        qreal radius;
        qreal speed;
    };

    void tick();

    QTimer* m_timer;
    QElapsedTimer m_clock;
    qint64 m_last = 0;
    double m_target = -1;
    double m_shown = 0;
    double m_shine = -1;   // 0..1 while the finish shine runs
    qint64 m_nextBubble = 0;
    QList<Bubble> m_bubbles;
    Mode m_mode = Mode::Normal;
    bool m_failed = false;
    std::function<void()> m_done;
};

} // namespace packix::ui
