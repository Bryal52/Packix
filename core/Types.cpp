#include "Types.h"

namespace packix {

qint64 ArchiveInfo::totalSize() const
{
    qint64 sum = 0;
    for (const Item& i : items) {
        if (!i.isDir)
            sum += i.size;
    }
    return sum;
}

int ArchiveInfo::fileCount() const
{
    int n = 0;
    for (const Item& i : items) {
        if (!i.isDir)
            ++n;
    }
    return n;
}

void Progress::setPercent(int percent)
{
    QMutexLocker l(&m_mutex);
    m_percent = qBound(0, percent, 100);
}

void Progress::setCurrent(const QString& file)
{
    QMutexLocker l(&m_mutex);
    m_current = file;
}

void Progress::setTotalBytes(qint64 bytes)
{
    QMutexLocker l(&m_mutex);
    m_total = bytes;
}

Progress::Snapshot Progress::snapshot() const
{
    QMutexLocker l(&m_mutex);
    return {m_percent, m_total, m_current};
}

} // namespace packix
