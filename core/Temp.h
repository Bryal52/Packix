#pragma once

#include <QString>

namespace packix::Temp {

// Private folder of this run (owner-only permissions): <temp>/Packix-<user>/run-XXXXXX.
// A lock file marks it as alive; folders of runs that are gone are removed by cleanupStale().
QString sessionDir();
// A new empty folder inside the session folder.
QString newDir(const QString& prefix = QStringLiteral("item"));
void cleanupStale();
// Removes this run's folder (at quit).
void cleanupSession();

} // namespace packix::Temp
