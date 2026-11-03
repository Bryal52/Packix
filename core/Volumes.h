#pragma once

#include <QString>
#include <QStringList>

namespace packix {

// Parts of a multi-volume archive found next to the one the user opened.
struct VolumeSet {
    QString openPath;      // what 7-Zip has to be given (first part; X.zip for split zips)
    QStringList parts;     // existing parts in order, full paths
    QStringList missing;   // file names of gaps (part 3 of 5 is gone)
    bool multi = false;
};

// Works from any part: "Игра.part3.rar", "Игра.7z.002", "Игра.r01", "Игра.z02", or the archive itself.
VolumeSet resolveVolumes(const QString& path);

} // namespace packix
