#pragma once

#include "Engine.h"

namespace packix {

// Runs the 7-Zip console program (7zz on the Mac, 7z.exe on Windows).
// Passwords go through stdin, never on the command line.
class SevenZipEngine : public Engine {
public:
    explicit SevenZipEngine(QString program = {});

    static QString findProgram();
    QString program() const { return m_program; }

    bool available() const override;
    QString version() const override;

    Status list(const QString& archive, const Format& format, const QString& password, ArchiveInfo& out,
                Progress* progress) override;
    Status extract(const QString& archive, const Format& format, const QString& destDir, const QStringList& items,
                   const QString& password, Progress* progress) override;
    Status create(const CreateRequest& request, Progress* progress) override;
    Status add(const QString& archive, const Format& format, const QString& workDir, const QStringList& inputs,
               const QString& password, Progress* progress) override;
    Status remove(const QString& archive, const Format& format, const QStringList& items, const QString& password,
                  Progress* progress) override;
    Status test(const QString& archive, const Format& format, const QString& password, Progress* progress) override;

    struct Run {
        int code = -1;
        bool cancelled = false;
        bool started = false;
        QString output;      // stdout + stderr of the (last) process
        QString stageError;  // stderr of the first process of a pipeline
    };

private:
    Run run(const QStringList& args, const QString& workDir, const QByteArray& input, Progress* progress) const;
    Run runOnce(const QStringList& args, const QString& workDir, const QByteArray& input, Progress* progress) const;
    Run pipe(const QStringList& first, const QStringList& second, const QString& workDir, Progress* progress) const;
    // wrapsTar formats cannot be changed in place: unpack the .tar next to the archive, change it, pack it again.
    Status rewrapTar(const QString& archive, const Format& format, Progress* progress,
                     const std::function<Status(const QString& tarPath)>& change);

    QString m_program;
};

} // namespace packix
