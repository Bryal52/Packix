#pragma once

#include <QColor>
#include <QJsonObject>
#include <QString>

class QWidget;

namespace packix::ui {

// Colors, sizes and animation timings from theme.json. Colors follow the system light/dark mode.
class Theme {
public:
    static const Theme& get();

    bool isDark() const;
    QColor color(const char* name) const;
    int metric(const char* name, int fallback) const;
    int motion(const char* name, int fallback) const;     // milliseconds (or a count)
    double motionF(const char* name, double fallback) const;
    // Animations are skipped when the system asks for less motion.
    static bool reduceMotion();

    // Soft red selection and accent for item views, buttons, focus — on top of the system style.
    void apply(QWidget* w) const;
    QString accentButtonStyle() const;

private:
    Theme();
    QJsonObject m_light, m_dark, m_metrics, m_motion;
};

} // namespace packix::ui
