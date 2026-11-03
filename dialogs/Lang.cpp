#include "Lang.h"

#include "Shared.h"

#include <QDir>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QJsonArray>
#include <QJsonObject>
#include <QLocale>
#include <QSettings>
#include <QStandardPaths>

namespace packix::ui {

namespace {

const QString kFallback = QStringLiteral("ru");

// "a": {"b": "x"} -> "a.b" = "x"; arrays stay lists (plural forms).
void flatten(const QJsonObject& obj, const QString& prefix, QHash<QString, QVariant>& out)
{
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        const QString key = prefix.isEmpty() ? it.key() : prefix + QLatin1Char('.') + it.key();
        if (it->isObject())
            flatten(it->toObject(), key, out);
        else if (it->isArray())
            out.insert(key, it->toArray().toVariantList());
        else
            out.insert(key, it->toString());
    }
}

QString fill(QString text, Args args)
{
    for (const auto& [name, value] : args)
        text.replace(QLatin1Char('{') + QLatin1String(name) + QLatin1Char('}'), value);
    return text;
}

} // namespace

Lang& Lang::instance()
{
    static Lang* lang = new Lang;
    return *lang;
}

Lang::Lang()
{
    QDir().mkpath(userDir());
    scan();
    m_fallback = loadStrings(Shared::path(QStringLiteral("lang/") + kFallback + QStringLiteral(".json")));
    QString system = kFallback;
    for (const QString& ui : QLocale::system().uiLanguages()) {
        const QString code = ui.left(2).toLower();
        bool known = false;
        for (const Info& i : std::as_const(m_available))
            known = known || (i.code == code && i.error.isEmpty());
        if (known) {
            system = code;
            break;
        }
    }
    const QString saved = QSettings().value(QStringLiteral("language"), system).toString();
    if (!setLanguage(saved))
        setLanguage(kFallback);

    auto* watcher = new QFileSystemWatcher(this);
    watcher->addPath(userDir());
    connect(watcher, &QFileSystemWatcher::directoryChanged, this, [this] {
        scan();
        setLanguage(m_code);   // the current file may have been edited
    });
}

void Lang::rescan()
{
    scan();
}

QString Lang::userDir() const
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/Packix/lang");
}

void Lang::scan()
{
    m_available.clear();
    QHash<QString, int> byCode;
    const QList<std::pair<QString, bool>> dirs = {{Shared::path(QStringLiteral("lang")), false}, {userDir(), true}};
    for (const auto& [dir, user] : dirs) {
        for (const QFileInfo& fi : QDir(dir).entryInfoList({QStringLiteral("*.json")}, QDir::Files, QDir::Name)) {
            Info info;
            info.code = fi.completeBaseName();
            info.path = fi.absoluteFilePath();
            info.user = user;
            const QJsonObject root = Shared::loadFile(info.path, &info.error, &info.errorLine);
            info.name = root.value(QStringLiteral("_meta")).toObject().value(QStringLiteral("name")).toString(info.code);
            // The user's file replaces a built-in one with the same code.
            if (const auto it = byCode.constFind(info.code); it != byCode.constEnd())
                m_available[*it] = info;
            else {
                byCode.insert(info.code, int(m_available.size()));
                m_available.append(info);
            }
        }
    }
}

QHash<QString, QVariant> Lang::loadStrings(const QString& path) const
{
    QHash<QString, QVariant> out;
    flatten(Shared::loadFile(path), QString(), out);
    return out;
}

bool Lang::setLanguage(const QString& code)
{
    for (const Info& info : std::as_const(m_available)) {
        if (info.code != code || !info.error.isEmpty())
            continue;
        m_strings = loadStrings(info.path);
        m_code = code;
        m_rule = m_strings.value(QStringLiteral("_meta.plural"), QStringLiteral("ru")).toString();
        QSettings().setValue(QStringLiteral("language"), code);
        emit changed();
        return true;
    }
    return false;
}

QString Lang::text(const QString& key) const
{
    QVariant v = m_strings.value(key);
    if (!v.isValid())
        v = m_fallback.value(key);
    if (v.typeId() == QMetaType::QVariantList)
        return v.toList().value(0).toString();
    return v.isValid() ? v.toString() : key;
}

int Lang::pluralIndex(qint64 n) const
{
    n = qAbs(n);
    if (m_rule == QLatin1String("ru")) {   // 1 файл, 2 файла, 5 файлов, 21 файл, 11 файлов
        if (n % 10 == 1 && n % 100 != 11)
            return 0;
        if (n % 10 >= 2 && n % 10 <= 4 && (n % 100 < 12 || n % 100 > 14))
            return 1;
        return 2;
    }
    if (m_rule == QLatin1String("one"))   // English and most others: 1 file, 2 files
        return n == 1 ? 0 : 1;
    return 0;                              // "none": one form for everything
}

QString Lang::plural(const QString& key, qint64 n) const
{
    QVariant v = m_strings.value(key);
    int idx = pluralIndex(n);
    if (!v.isValid()) {
        v = m_fallback.value(key);
        // Russian forms for a key the current language lacks.
        n = qAbs(n);
        idx = (n % 10 == 1 && n % 100 != 11) ? 0 : (n % 10 >= 2 && n % 10 <= 4 && (n % 100 < 12 || n % 100 > 14)) ? 1 : 2;
    }
    const QVariantList forms = v.toList();
    if (forms.isEmpty())
        return v.isValid() ? v.toString() : key;
    return forms.value(qMin(idx, int(forms.size()) - 1)).toString();
}

QString L(const char* key, Args args)
{
    return fill(Lang::instance().text(QString::fromLatin1(key)), args);
}

QString Ln(const char* key, qint64 n, Args args)
{
    QString t = Lang::instance().plural(QString::fromLatin1(key), n);
    t.replace(QStringLiteral("{n}"), QLocale(Lang::instance().current()).toString(n));
    return fill(t, args);
}

QString humanSize(qint64 bytes)
{
    if (bytes < 0)
        return {};
    const QLocale locale(Lang::instance().current());
    if (bytes < 1024)
        return locale.toString(bytes) + QLatin1Char(' ') + L("units.b");
    static const char* units[] = {"units.kb", "units.mb", "units.gb", "units.tb"};
    double v = double(bytes);
    int u = -1;
    while (v >= 1024.0 && u < 3) {
        v /= 1024.0;
        ++u;
    }
    return locale.toString(v, 'f', v < 10 ? 1 : 0) + QLatin1Char(' ') + L(units[u]);
}

} // namespace packix::ui
