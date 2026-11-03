#include "Volumes.h"

#include "Formats.h"

#include <QDir>
#include <QFileInfo>
#include <QMap>

#include <functional>

namespace packix {

namespace {

QString pad(int n, int width)
{
    return QStringLiteral("%1").arg(n, width, 10, QLatin1Char('0'));
}

// Numbered siblings "<prefix><digits><suffix>" in the folder, by number (case-insensitive).
QMap<int, QString> numbered(const QDir& dir, const QString& prefix, const QString& suffix)
{
    QMap<int, QString> out;
    const QRegularExpression re(QLatin1Char('^') + QRegularExpression::escape(prefix) + QStringLiteral("(\\d+)")
                                    + QRegularExpression::escape(suffix) + QLatin1Char('$'),
                                QRegularExpression::CaseInsensitiveOption);
    for (const QString& name : dir.entryList(QDir::Files | QDir::Hidden)) {
        const auto m = re.match(name);
        if (m.hasMatch())
            out.insert(m.captured(1).toInt(), dir.filePath(name));
    }
    return out;
}

// Fills parts and gaps for numbers first..max.
void collect(VolumeSet& set, const QMap<int, QString>& found, int first, const std::function<QString(int)>& nameOf)
{
    if (found.isEmpty())
        return;
    for (int n = first; n <= found.lastKey(); ++n) {
        if (found.contains(n))
            set.parts << found.value(n);
        else
            set.missing << nameOf(n);
    }
}

QString findCaseInsensitive(const QDir& dir, const QString& name)
{
    for (const QString& e : dir.entryList(QDir::Files | QDir::Hidden)) {
        if (e.compare(name, Qt::CaseInsensitive) == 0)
            return dir.filePath(e);
    }
    return {};
}

} // namespace

VolumeSet resolveVolumes(const QString& path)
{
    VolumeSet set;
    set.openPath = path;
    set.parts = {path};

    const QFileInfo fi(path);
    const QDir dir = fi.absoluteDir();
    const QString name = fi.fileName();

    for (const VolumePattern& p : Formats::instance().volumePatterns()) {
        const auto m = p.re.match(name);
        if (!m.hasMatch())
            continue;
        const QString base = m.captured(1);
        const int width = int(m.captured(2).size());
        VolumeSet v;

        if (p.kind == QLatin1String("numbered")) {
            const QMap<int, QString> found = numbered(dir, base + QLatin1Char('.'), QString());
            if (!found.contains(1))
                continue;   // "report.2024" is not a split archive
            collect(v, found, 1, [&](int n) { return base + QLatin1Char('.') + pad(n, width); });
            v.openPath = found.value(1);
        } else if (p.kind == QLatin1String("rarPart")) {
            const QMap<int, QString> found = numbered(dir, base + QStringLiteral(".part"), QStringLiteral(".rar"));
            collect(v, found, 1, [&](int n) { return base + QStringLiteral(".part") + pad(n, width) + QStringLiteral(".rar"); });
            v.openPath = found.value(1, dir.filePath(base + QStringLiteral(".part") + pad(1, width) + QStringLiteral(".rar")));
            if (!found.contains(1))
                v.missing.prepend(QFileInfo(v.openPath).fileName());
        } else if (p.kind == QLatin1String("rarOld") || p.kind == QLatin1String("zipSplit")) {
            const bool rar = p.kind == QLatin1String("rarOld");
            const QString head = findCaseInsensitive(dir, base + (rar ? QStringLiteral(".rar") : QStringLiteral(".zip")));
            const QMap<int, QString> found = numbered(dir, base + (rar ? QStringLiteral(".r") : QStringLiteral(".z")), QString());
            const int first = rar ? 0 : 1;
            const QString letter = rar ? QStringLiteral(".r") : QStringLiteral(".z");
            collect(v, found, first, [&](int n) { return base + letter + pad(n, width); });
            // rar: X.rar is the first part; split zip: X.zip is the last one, but 7-Zip opens it.
            const QString headName = base + (rar ? QStringLiteral(".rar") : QStringLiteral(".zip"));
            if (head.isEmpty())
                v.missing << headName;
            else if (rar)
                v.parts.prepend(head);
            else
                v.parts.append(head);
            v.openPath = head.isEmpty() ? dir.filePath(headName) : head;
        }
        v.multi = v.parts.size() + v.missing.size() > 1;
        if (v.multi)
            return v;
    }

    // The archive itself, with old-style parts next to it: X.rar + X.r00, X.zip + X.z01.
    const QString lower = name.toLower();
    if (lower.endsWith(QLatin1String(".rar")) || lower.endsWith(QLatin1String(".zip"))) {
        const QString base = name.left(name.size() - 4);
        const QString letter = lower.endsWith(QLatin1String(".rar")) ? QStringLiteral(".r") : QStringLiteral(".z");
        const QMap<int, QString> found = numbered(dir, base + letter, QString());
        if (!found.isEmpty() && !QRegularExpression(QStringLiteral("\\.part\\d+\\.rar$"),
                                                    QRegularExpression::CaseInsensitiveOption).match(name).hasMatch()) {
            return resolveVolumes(found.first());
        }
    }
    return set;
}

} // namespace packix
