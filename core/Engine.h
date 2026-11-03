#pragma once

#include "Formats.h"
#include "Types.h"

#include <memory>

namespace packix {

struct CreateRequest {
    QString output;          // archive to write; must not exist yet
    QString workDir;         // inputs are relative to it and keep that layout inside the archive
    QStringList inputs;
    QString formatId;
    QString levelId;
    QString password;        // empty = no encryption
    bool encryptNames = false;
    QStringList exclude;     // wildcards, matched at any depth
};

// Raw archive operations. Knows nothing about conflicts, free space or where files end up —
// that is Archiver's job. Implementations: SevenZipEngine (7zz process); a library one for Android later.
class Engine {
public:
    virtual ~Engine() = default;

    virtual bool available() const = 0;
    virtual QString version() const = 0;

    virtual Status list(const QString& archive, const Format& format, const QString& password,
                        ArchiveInfo& out, Progress* progress) = 0;
    // Everything (items empty) or the given paths, folders with their contents, into destDir.
    virtual Status extract(const QString& archive, const Format& format, const QString& destDir,
                           const QStringList& items, const QString& password, Progress* progress) = 0;
    virtual Status create(const CreateRequest& request, Progress* progress) = 0;
    // Adds or replaces files; their paths inside the archive are their paths relative to workDir.
    virtual Status add(const QString& archive, const Format& format, const QString& workDir,
                       const QStringList& inputs, const QString& password, Progress* progress) = 0;
    virtual Status remove(const QString& archive, const Format& format, const QStringList& items,
                          const QString& password, Progress* progress) = 0;
    virtual Status test(const QString& archive, const Format& format, const QString& password,
                        Progress* progress) = 0;
};

std::shared_ptr<Engine> engine();
void setEngine(std::shared_ptr<Engine> e);

} // namespace packix
