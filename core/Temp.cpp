#include "Temp.h"

#include <QDir>
#include <QFile>
#include <QLockFile>
#include <QMutex>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <memory>

namespace packix::Temp {

namespace {

const QString kLock = QStringLiteral(".alive.lock");

QMutex g_mutex;
std::unique_ptr<QTemporaryDir> g_session;
std::unique_ptr<QLockFile> g_lock;

QString rootDir()
{
    QString user = qEnvironmentVariable("USER", qEnvironmentVariable("USERNAME", QStringLiteral("user")));
    const QString dir = QDir::tempPath() + QStringLiteral("/Packix-") + user;
    QDir().mkpath(dir);
    QFile::setPermissions(dir, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    return dir;
}

} // namespace

QString sessionDir()
{
    QMutexLocker l(&g_mutex);
    if (!g_session) {
        g_session = std::make_unique<QTemporaryDir>(rootDir() + QStringLiteral("/run-XXXXXX"));
        g_session->setAutoRemove(true);
        QFile::setPermissions(g_session->path(), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        g_lock = std::make_unique<QLockFile>(g_session->filePath(kLock));
        g_lock->setStaleLockTime(0);
        g_lock->tryLock(0);
    }
    return g_session->path();
}

QString newDir(const QString& prefix)
{
    QTemporaryDir d(sessionDir() + QLatin1Char('/') + prefix + QStringLiteral("-XXXXXX"));
    d.setAutoRemove(false);
    return d.path();
}

void cleanupStale()
{
    const QDir root(rootDir());
    for (const QString& name : root.entryList({QStringLiteral("run-*")}, QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString dir = root.filePath(name);
        {
            QMutexLocker l(&g_mutex);
            if (g_session && g_session->path() == dir)
                continue;
        }
        QLockFile lock(dir + QLatin1Char('/') + kLock);
        lock.setStaleLockTime(0);
        // Taking the lock works only if the run that owned the folder is gone.
        if (lock.tryLock(0)) {
            lock.unlock();
            QDir(dir).removeRecursively();
        }
    }
}

void cleanupSession()
{
    QMutexLocker l(&g_mutex);
    if (g_lock)
        g_lock->unlock();
    g_lock.reset();
    g_session.reset();   // auto-removes the folder
}

} // namespace packix::Temp
