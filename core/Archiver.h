#pragma once

#include "Engine.h"
#include "Volumes.h"

namespace packix::Archiver {

// Finds all volumes, picks the format and lists the contents. Encrypted names need the password.
Status open(const QString& path, const QString& password, ArchiveInfo& out, Progress* progress = nullptr);

// Format of an opened archive; unknown extensions (apk, vpk...) still open by content, read-only.
Format formatOf(const ArchiveInfo& info);
bool canModify(const ArchiveInfo& info);

struct ExtractRequest {
    QString archive;
    QString destination;        // created if missing
    QStringList items;          // empty = everything; selected items land directly in destination
    QString password;
    ConflictHandler onConflict; // not set = keep both
    std::function<void(const QString& createdPath)> onCreated;   // e.g. copy the quarantine flag
};
// Unpacks into a hidden folder inside the destination first, checks it (links pointing outside are
// dropped, names are made valid for this system), then moves everything in place asking about conflicts.
Status extract(const ExtractRequest& request, Progress* progress = nullptr, QStringList* created = nullptr);

struct CreateRequest {
    QStringList sources;        // absolute paths
    QString destinationDir;
    QString fileName;           // with extension
    QString formatId;
    QString levelId;
    QString password;
    bool encryptNames = false;
    bool replaceExisting = false;   // false: an existing file gets " (1)" instead
};
// Writes into a hidden temporary folder and moves the result in only when it is complete,
// so a failed or cancelled run never touches an existing archive.
Status create(const CreateRequest& request, Progress* progress = nullptr, QString* resultPath = nullptr);

Status remove(const ArchiveInfo& info, const QStringList& items, const QString& password, Progress* progress = nullptr);
// Adds files from disk into `insideDir` of the archive ("" = top level).
Status add(const ArchiveInfo& info, const QStringList& sources, const QString& insideDir, const QString& password,
           Progress* progress = nullptr);
// Puts back files whose paths relative to workDir are their paths in the archive (edited previews).
Status update(const ArchiveInfo& info, const QString& workDir, const QStringList& relativePaths,
              const QString& password, Progress* progress = nullptr);

struct PasswordRequest {
    QString oldPassword;
    QString newPassword;        // empty = remove the password
    QString targetFormatId;     // empty = keep the format (must be modifiable and support passwords)
    bool encryptNames = true;   // 7z only
    bool removeOriginal = true; // when the format changes and the file gets a new name
};
// Repacks: unpack to a temporary folder, pack with the new password, test, then replace.
Status changePassword(const ArchiveInfo& info, const PasswordRequest& request, Progress* progress = nullptr,
                      QString* resultPath = nullptr);

Status test(const ArchiveInfo& info, const QString& password, Progress* progress = nullptr);

// Size of files and folders on disk (for progress and free-space checks).
qint64 diskSize(const QStringList& paths);

} // namespace packix::Archiver
