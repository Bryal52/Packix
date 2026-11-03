#pragma once

#include <QJsonObject>
#include <QString>

namespace packix::Shared {

// Folder with formats.json, theme.json, lang/... Looked up in this order:
// $PACKIX_SHARED, Packix.app/Contents/Resources/shared, "shared" next to the program, the source tree.
QString dir();
QString path(const QString& relative);

// Parsed JSON file from the shared folder. Cached; an unreadable file gives an empty object
// and a message in *error (if given); *errorLine is the line of a JSON syntax error.
QJsonObject load(const QString& relative, QString* error = nullptr);
QJsonObject loadFile(const QString& absolutePath, QString* error = nullptr, int* errorLine = nullptr);

} // namespace packix::Shared
