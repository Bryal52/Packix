#include "Formats.h"

#include "Shared.h"

#include <QJsonArray>
#include <QJsonObject>

namespace packix {

namespace {

QStringList strings(const QJsonValue& v)
{
    QStringList out;
    for (const QJsonValue& s : v.toArray())
        out << s.toString();
    return out;
}

} // namespace

const Formats& Formats::instance()
{
    static const Formats f;
    return f;
}

Formats::Formats()
{
    QString err;
    const QJsonObject root = Shared::load(QStringLiteral("formats.json"), &err);
    if (!err.isEmpty())
        m_error = QStringLiteral("formats.json: ") + err;

    for (const QJsonValue& v : root.value(QStringLiteral("formats")).toArray()) {
        const QJsonObject o = v.toObject();
        Format f;
        f.id = o.value(QStringLiteral("id")).toString();
        f.name = o.value(QStringLiteral("name")).toString(f.id.toUpper());
        f.extensions = strings(o.value(QStringLiteral("extensions")));
        f.canOpen = o.value(QStringLiteral("open")).toBool();
        f.canCreate = o.value(QStringLiteral("create")).toBool();
        f.canModify = o.value(QStringLiteral("modify")).toBool();
        f.canPassword = o.value(QStringLiteral("password")).toBool();
        f.defaultHandler = o.value(QStringLiteral("defaultHandler")).toBool();
        f.wrapsTar = o.value(QStringLiteral("wrapsTar")).toBool();
        f.passwordAsciiOnly = o.value(QStringLiteral("passwordAsciiOnly")).toBool();
        f.createArgs = strings(o.value(QStringLiteral("createArgs")));
        f.passwordArgs = strings(o.value(QStringLiteral("passwordArgs")));
        f.encryptNamesArgs = strings(o.value(QStringLiteral("encryptNamesArgs")));
        f.compressArgs = strings(o.value(QStringLiteral("compressArgs")));
        if (f.isValid())
            m_formats.append(f);
    }
    m_createOrder = strings(root.value(QStringLiteral("createOrder")));
    m_defaultCreate = root.value(QStringLiteral("defaultCreate")).toString();

    for (const QJsonValue& v : root.value(QStringLiteral("volumes")).toArray()) {
        const QJsonObject o = v.toObject();
        VolumePattern p;
        p.kind = o.value(QStringLiteral("kind")).toString();
        p.re = QRegularExpression(o.value(QStringLiteral("pattern")).toString(),
                                  QRegularExpression::CaseInsensitiveOption);
        if (p.re.isValid())
            m_volumes.append(p);
    }

    m_junk = strings(root.value(QStringLiteral("junk")));
    for (const QString& j : std::as_const(m_junk))
        m_junkRe.append(QRegularExpression(QRegularExpression::wildcardToRegularExpression(j),
                                           QRegularExpression::CaseInsensitiveOption));

    const QJsonObject comp = Shared::load(QStringLiteral("compression.json"), &err);
    m_defaultLevel = comp.value(QStringLiteral("default")).toString();
    for (const QJsonValue& v : comp.value(QStringLiteral("levels")).toArray()) {
        const QJsonObject o = v.toObject();
        m_levels.append({o.value(QStringLiteral("id")).toString(), strings(o.value(QStringLiteral("args")))});
    }
}

bool Format::acceptsPassword(const QString& password) const
{
    if (!passwordAsciiOnly)
        return true;
    for (const QChar c : password) {
        if (c.unicode() < 0x20 || c.unicode() > 0x7E)
            return false;
    }
    return true;
}

Format Formats::byId(const QString& id) const
{
    for (const Format& f : m_formats) {
        if (f.id == id)
            return f;
    }
    return {};
}

Format Formats::detect(const QString& fileName) const
{
    const QString lower = fileName.toLower();
    Format best;
    qsizetype bestLen = 0;
    for (const Format& f : m_formats) {
        for (const QString& ext : f.extensions) {
            if (ext.size() > bestLen && lower.endsWith(QLatin1Char('.') + ext) && lower.size() > ext.size() + 1) {
                best = f;
                bestLen = ext.size();
            }
        }
    }
    return best;
}

std::pair<QString, QString> Formats::splitName(const QString& fileName) const
{
    const QString lower = fileName.toLower();
    qsizetype len = 0;
    for (const QString& ext : detect(fileName).extensions) {
        if (lower.endsWith(QLatin1Char('.') + ext))
            len = qMax(len, ext.size());
    }
    if (len > 0)
        return {fileName.left(fileName.size() - len - 1), fileName.right(len)};
    const qsizetype dot = fileName.lastIndexOf(QLatin1Char('.'));
    if (dot <= 0)
        return {fileName, QString()};
    return {fileName.left(dot), fileName.mid(dot + 1)};
}

QList<Format> Formats::creatable() const
{
    QList<Format> out;
    for (const QString& id : m_createOrder) {
        const Format f = byId(id);
        if (f.isValid() && f.canCreate)
            out.append(f);
    }
    return out;
}

CompressionLevel Formats::level(const QString& id) const
{
    for (const CompressionLevel& l : m_levels) {
        if (l.id == id)
            return l;
    }
    return {};
}

bool Formats::isJunk(const QString& name) const
{
    for (const QRegularExpression& re : m_junkRe) {
        if (re.match(name).hasMatch())
            return true;
    }
    return false;
}

} // namespace packix
