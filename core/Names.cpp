#include "Names.h"

#include "Formats.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

namespace packix::Names {

QString unique(const QString& dir, const QString& fileName)
{
    const QDir d(dir);
    if (!QFileInfo::exists(d.filePath(fileName)))
        return fileName;
    auto [base, ext] = Formats::instance().splitName(fileName);
    // "Фото (3).zip" continues from 3 instead of making "Фото (3) (1).zip".
    static const QRegularExpression counted(QStringLiteral("^(.*) \\((\\d+)\\)$"));
    int n = 1;
    if (const auto m = counted.match(base); m.hasMatch()) {
        base = m.captured(1);
        n = m.captured(2).toInt() + 1;
    }
    for (;; ++n) {
        const QString candidate = QStringLiteral("%1 (%2)").arg(base).arg(n)
            + (ext.isEmpty() ? QString() : QLatin1Char('.') + ext);
        if (!QFileInfo::exists(d.filePath(candidate)))
            return candidate;
    }
}

QString windowsSafe(const QString& name)
{
    QString out;
    out.reserve(name.size());
    for (const QChar c : name) {
        if (c.unicode() < 32 || QStringLiteral("<>:\"/\\|?*").contains(c))
            out += QLatin1Char('_');
        else
            out += c;
    }
    // Windows drops trailing dots and spaces silently, which would merge names.
    while (out.endsWith(QLatin1Char('.')) || out.endsWith(QLatin1Char(' ')))
        out.chop(1);
    if (out.isEmpty())
        out = QStringLiteral("_");
    static const QRegularExpression reserved(QStringLiteral("^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(\\..*)?$"),
                                             QRegularExpression::CaseInsensitiveOption);
    if (reserved.match(out).hasMatch())
        out.prepend(QLatin1Char('_'));
    return out;
}

QString forThisSystem(const QString& name)
{
#ifdef Q_OS_WIN
    return windowsSafe(name);
#else
    QString out = name;
    out.replace(QLatin1Char('/'), QLatin1Char('_'));
    out.remove(QChar(0));
    return out.isEmpty() ? QStringLiteral("_") : out;
#endif
}

} // namespace packix::Names
