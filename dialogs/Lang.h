#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QVariant>

#include <initializer_list>
#include <utility>

namespace packix::ui {

// Interface texts from lang/<code>.json: built-in ones in shared/lang, the user's own in
// ~/Library/Application Support/Packix/lang (picked up without a restart).
// A missing key falls back to Russian, then to the key itself.
class Lang : public QObject {
    Q_OBJECT
public:
    struct Info {
        QString code;        // file name without .json
        QString name;        // "_meta.name": "Русский"
        QString path;
        bool user = false;   // from the user's folder
        QString error;       // broken JSON: message and line
        int errorLine = 0;
    };

    static Lang& instance();

    QList<Info> available() const { return m_available; }
    QString current() const { return m_code; }
    bool setLanguage(const QString& code);
    QString userDir() const;

    QString text(const QString& key) const;
    // Plural forms are an array in the file: ["{n} файл", "{n} файла", "{n} файлов"].
    QString plural(const QString& key, qint64 n) const;

signals:
    void changed();

private:
    Lang();
    void scan();
    QHash<QString, QVariant> loadStrings(const QString& path) const;
    int pluralIndex(qint64 n) const;

    QList<Info> m_available;
    QString m_code;
    QString m_rule;
    QHash<QString, QVariant> m_strings;
    QHash<QString, QVariant> m_fallback;
};

using Args = std::initializer_list<std::pair<const char*, QString>>;

// L("create.title"), L("conflict.text", {{"name", file}}) — "{name}" in the text is replaced.
QString L(const char* key, Args args = {});
// Ln("files.count", 5) -> "5 файлов"; {n} is filled in automatically.
QString Ln(const char* key, qint64 n, Args args = {});
// "4 Б", "410 КБ", "2,0 МБ" — like Folix; unit names come from the language file.
QString humanSize(qint64 bytes);

} // namespace packix::ui
