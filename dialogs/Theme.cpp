#include "Theme.h"

#include "Platform.h"
#include "Shared.h"

#include <QGuiApplication>
#include <QPalette>
#include <QStyleHints>
#include <QWidget>

namespace packix::ui {

const Theme& Theme::get()
{
    static const Theme t;
    return t;
}

Theme::Theme()
{
    const QJsonObject root = Shared::load(QStringLiteral("theme.json"));
    const QJsonObject colors = root.value(QStringLiteral("colors")).toObject();
    m_light = colors.value(QStringLiteral("light")).toObject();
    m_dark = colors.value(QStringLiteral("dark")).toObject();
    m_metrics = root.value(QStringLiteral("metrics")).toObject();
    m_motion = root.value(QStringLiteral("motion")).toObject();
}

bool Theme::isDark() const
{
    if (const auto scheme = QGuiApplication::styleHints()->colorScheme(); scheme != Qt::ColorScheme::Unknown)
        return scheme == Qt::ColorScheme::Dark;
    return QGuiApplication::palette().color(QPalette::Window).lightness() < 128;
}

QColor Theme::color(const char* name) const
{
    const QString key = QLatin1String(name);
    QString value = (isDark() ? m_dark : m_light).value(key).toString();
    if (value.isEmpty())
        value = m_light.value(key).toString();
    // "#RRGGBBAA" in the file; Qt reads 8 digits as #AARRGGBB.
    if (value.size() == 9 && value.startsWith(QLatin1Char('#'))) {
        QColor c(value.left(7));
        c.setAlpha(value.mid(7, 2).toInt(nullptr, 16));
        return c;
    }
    const QColor c(value);
    return c.isValid() ? c : QGuiApplication::palette().color(QPalette::Highlight);
}

int Theme::metric(const char* name, int fallback) const
{
    return m_metrics.value(QLatin1String(name)).toInt(fallback);
}

int Theme::motion(const char* name, int fallback) const
{
    return m_motion.value(QLatin1String(name)).toInt(fallback);
}

double Theme::motionF(const char* name, double fallback) const
{
    return m_motion.value(QLatin1String(name)).toDouble(fallback);
}

bool Theme::reduceMotion()
{
    return platform::reduceMotion();
}

void Theme::apply(QWidget* w) const
{
    QPalette pal = w->palette();
    for (auto group : {QPalette::Active, QPalette::Inactive}) {
        pal.setColor(group, QPalette::Highlight, color("selection"));
        pal.setColor(group, QPalette::HighlightedText, color("selectionText"));
        pal.setColor(group, QPalette::Accent, color("accent"));
        pal.setColor(group, QPalette::Link, color("accent"));
    }
    w->setPalette(pal);
}

QString Theme::accentButtonStyle() const
{
    const QColor a = color("accent");
    return QStringLiteral(
               "QPushButton { background: %1; color: %2; border: none; border-radius: 6px; padding: 5px 16px; }"
               "QPushButton:hover { background: %3; }"
               "QPushButton:pressed { background: %4; }"
               "QPushButton:disabled { background: %5; color: %6; }")
        .arg(a.name(), color("accentText").name(), a.lighter(108).name(), a.darker(112).name(),
             color("liquidDim").name(), color("accentText").name());
}

} // namespace packix::ui
