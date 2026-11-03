#pragma once

#include <QString>

namespace packix::Names {

// "Фото.zip" -> "Фото (1).zip" -> "Фото (2).zip"... the first name that is free in `dir`.
// Knows double extensions: "Фото.tar.gz" -> "Фото (1).tar.gz".
QString unique(const QString& dir, const QString& fileName);

// Replaces characters and names Windows cannot store (: * ? " < > | CON NUL ...). Same name if fine.
QString windowsSafe(const QString& name);

// The name as it can be written on this system (windowsSafe on Windows, otherwise only '/' and NUL).
QString forThisSystem(const QString& name);

} // namespace packix::Names
