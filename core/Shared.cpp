#include "Shared.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QMutex>

namespace packix::Shared {

QString dir()
{
    static const QString found = [] {
        QStringList candidates;
        if (const QString env = qEnvironmentVariable("PACKIX_SHARED"); !env.isEmpty())
            candidates << env;
        const QString app = QCoreApplication::applicationDirPath();
        candidates << app + QStringLiteral("/../Resources/shared") << app + QStringLiteral("/shared");
#ifdef PACKIX_SOURCE_SHARED
        candidates << QStringLiteral(PACKIX_SOURCE_SHARED);
#endif
        for (const QString& c : std::as_const(candidates)) {
            if (QFileInfo::exists(c + QStringLiteral("/formats.json")))
                return QDir(c).absolutePath();
        }
        return QString();
    }();
    return found;
}

QString path(const QString& relative)
{
    return dir() + QLatin1Char('/') + relative;
}

QJsonObject loadFile(const QString& absolutePath, QString* error, int* errorLine)
{
    QFile f(absolutePath);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = f.errorString();
        return {};
    }
    QJsonParseError pe;
    const QByteArray data = f.readAll();
    const QJsonDocument doc = QJsonDocument::fromJson(data, &pe);
    if (pe.error != QJsonParseError::NoError) {
        if (error)
            *error = pe.errorString();
        if (errorLine)
            *errorLine = int(data.left(pe.offset).count('\n')) + 1;
        return {};
    }
    return doc.object();
}

QJsonObject load(const QString& relative, QString* error)
{
    static QMutex mutex;
    static QHash<QString, QJsonObject> cache;
    QMutexLocker lock(&mutex);
    if (const auto it = cache.constFind(relative); it != cache.constEnd())
        return *it;
    QString err;
    const QJsonObject obj = loadFile(path(relative), &err);
    if (!err.isEmpty()) {
        if (error)
            *error = err;
        return obj;
    }
    cache.insert(relative, obj);
    return obj;
}

} // namespace packix::Shared
