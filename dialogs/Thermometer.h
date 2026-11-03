#pragma once

#include <QElapsedTimer>
#include <QWidget>

#include <functional>

class QTimer;

namespace packix::ui {

// Progress as a thin glass tube with saturated red liquid that shimmers like water.
// It flows to new values instead of jumping.
class Thermometer : public QWidget {
    Q_OBJECT
public:
    explicit Thermometer(QWidget* parent = nullptr);

    // 0..1; below 0 = still counting (the liquid sways at the start).
    void setValue(double value);
    // Fills up, a shine runs over the glass, then `done`.
    void finish(std::function<void()> done = {});
    // Cancel: the liquid runs back, then `done`.
    void drain(std::function<void()> done = {});
    void setFailed(bool failed);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent*) override;
    void showEvent(QShowEvent*) override;
    void hideEvent(QHideEvent*) override;

private:
    enum class Mode { Normal, Finishing, Draining };

    void tick();

    QTimer* m_timer;
    QElapsedTimer m_clock;
    qint64 m_last = 0;
    double m_target = -1;
    double m_shown = 0;
    double m_velocity = 0;
    double m_shine = -1;   // 0..1 while the finish shine runs
    Mode m_mode = Mode::Normal;
    bool m_failed = false;
    std::function<void()> m_done;
};

} // namespace packix::ui
