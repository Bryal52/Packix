#pragma once

#include <QString>
#include <QStringList>

#include <functional>

// Everything that depends on the operating system. macOS: platform/macos; Windows and Android later.
namespace packix::platform {

QString fileManagerName();                 // "Finder"
void reveal(const QString& path);          // select the file in Finder
void quickLook(const QStringList& paths);
bool reduceMotion();
// Mac shortcuts are written ⌘/⌥/⇧/⌫ in texts; other systems get Ctrl+, Alt+...
QString keys(QString text);

// No Dock icon normally; while windows are open the app shows up in ⌘Tab (which needs the Dock icon).
void setForeground(bool hasWindows);
void activate();

// Passwords the user asked to remember; tried automatically on encrypted archives.
QStringList savedPasswords();
void savePassword(const QString& password);
void forgetPassword(const QString& password);

// "Downloaded from the internet" flag of the archive goes onto everything unpacked from it.
void inheritQuarantine(const QString& archive, const QString& createdPath);

bool loginItemEnabled();
bool setLoginItem(bool enabled);

bool isDefaultArchiver();
void makeDefaultArchiver();                // may show a system confirmation

void notify(const QString& title, const QString& text, const QString& revealPath);

// Finder: right-click → Быстрые действия → Архивировать.
void installServices(std::function<void(const QStringList& files)> onArchive);

} // namespace packix::platform
