#pragma once

#include <QList>
#include <QRegularExpression>
#include <QString>
#include <QStringList>

namespace packix {

// One entry of formats.json.
struct Format {
    QString id;
    QString name;              // short label: ZIP, 7Z, TAR.GZ
    QStringList extensions;    // without the dot, may contain dots: "tar.gz"
    bool canOpen = false;
    bool canCreate = false;
    bool canModify = false;    // delete / add / set a password in place
    bool canPassword = false;
    bool defaultHandler = false;
    bool wrapsTar = false;     // .tar inside a single-stream compressor
    bool passwordAsciiOnly = false;   // 7-Zip rejects other characters in zip passwords
    QStringList createArgs;
    QStringList passwordArgs;
    QStringList encryptNamesArgs;
    QStringList compressArgs;  // wrapsTar: second stage of the pipeline

    bool isValid() const { return !id.isEmpty(); }
    QString primaryExtension() const { return extensions.value(0); }
    bool acceptsPassword(const QString& password) const;
};

struct CompressionLevel {
    QString id;                // store, fast, normal, max
    QStringList args;
};

struct VolumePattern {
    QString kind;              // numbered, rarPart, rarOld, zipSplit
    QRegularExpression re;
};

class Formats {
public:
    static const Formats& instance();

    const QList<Format>& all() const { return m_formats; }
    Format byId(const QString& id) const;
    // By the file name; the longest matching extension wins ("a.tar.gz" is tar.gz, not gz).
    Format detect(const QString& fileName) const;
    // "Фото.tar.gz" -> {"Фото", "tar.gz"}; unknown extensions split at the last dot.
    std::pair<QString, QString> splitName(const QString& fileName) const;

    QList<Format> creatable() const;   // in the order of the "Тип" list
    QString defaultCreate() const { return m_defaultCreate; }

    const QList<CompressionLevel>& levels() const { return m_levels; }
    CompressionLevel level(const QString& id) const;
    QString defaultLevel() const { return m_defaultLevel; }

    const QList<VolumePattern>& volumePatterns() const { return m_volumes; }
    // True for .DS_Store, __MACOSX, ._name ... that never go into an archive.
    bool isJunk(const QString& name) const;
    const QStringList& junkPatterns() const { return m_junk; }

    QString loadError() const { return m_error; }

private:
    Formats();

    QList<Format> m_formats;
    QStringList m_createOrder;
    QString m_defaultCreate;
    QList<CompressionLevel> m_levels;
    QString m_defaultLevel;
    QList<VolumePattern> m_volumes;
    QStringList m_junk;
    QList<QRegularExpression> m_junkRe;
    QString m_error;
};

} // namespace packix
