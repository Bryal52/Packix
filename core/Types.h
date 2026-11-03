#pragma once

#include <QDateTime>
#include <QList>
#include <QMutex>
#include <QString>
#include <QStringList>

#include <atomic>
#include <functional>

namespace packix {

// What went wrong, without any wording: the interface turns it into text from lang/*.json.
enum class Error {
    None,
    NeedPassword,     // the archive is encrypted and no password was given
    WrongPassword,
    PasswordCharset,  // the format cannot store this password (zip: only Latin letters, digits, symbols)
    Corrupt,
    Unsupported,      // not an archive, or a format 7-Zip cannot open
    MissingVolume,    // a part of a multi-volume archive is missing
    NoSpace,
    NoAccess,
    NotFound,
    ReadOnlyFormat,   // rar, iso, dmg... cannot be changed
    Cancelled,
    EngineMissing,    // 7zz is not installed / not bundled
    Failed,           // anything else; see Status::detail
};

struct Status {
    Error error = Error::None;
    QString detail;        // engine output or a file name, for "Подробнее" and the log
    QStringList warnings;  // things that were fixed on the way (renamed files, dropped links...)
    qint64 needed = 0;     // NoSpace: bytes required / available
    qint64 available = 0;
    QStringList missing;   // MissingVolume: file names of the missing parts

    bool ok() const { return error == Error::None; }
    static Status fail(Error e, const QString& detail = {})
    {
        Status s;
        s.error = e;
        s.detail = detail;
        return s;
    }
};

struct Item {
    QString path;          // inside the archive, always with '/'
    bool isDir = false;
    bool isLink = false;
    bool implied = false;  // folder not stored in the archive, made up from its children
    bool encrypted = false;
    qint64 size = 0;
    qint64 packed = -1;    // -1 when unknown (solid blocks, compressed tar)
    QDateTime modified;
    QString method;
};

struct ArchiveInfo {
    QString path;          // the file that was opened (first volume for split archives)
    QString formatId;      // id from formats.json
    QString engineType;    // what 7-Zip reported: zip, 7z, Rar5, tar...
    QStringList volumes;   // every part, in order; one entry for a normal archive
    bool encryptedNames = false;
    bool anyEncrypted = false;
    bool solid = false;
    qint64 fileSize = 0;   // all volumes together
    QList<Item> items;

    qint64 totalSize() const;
    int fileCount() const;
};

// Shared between a worker thread and whoever shows progress.
class Progress {
public:
    std::atomic_bool cancel{false};

    void setPercent(int percent);
    void setCurrent(const QString& file);
    void setTotalBytes(qint64 bytes);

    struct Snapshot {
        int percent = -1;      // -1 until the engine reports anything
        qint64 totalBytes = 0;
        QString current;
    };
    Snapshot snapshot() const;

private:
    mutable QMutex m_mutex;
    int m_percent = -1;
    qint64 m_total = 0;
    QString m_current;
};

enum class Conflict { Replace, KeepBoth, Skip, Cancel };
// Asked for every name that already exists at the destination. Called on the worker thread.
using ConflictHandler = std::function<Conflict(const QString& existingPath, bool isDir)>;

} // namespace packix
