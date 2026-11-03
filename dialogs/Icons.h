#pragma once

#include <QColor>
#include <QIcon>
#include <QString>

class QPainter;
class QRectF;

namespace packix::ui {

// Same categories and drawings as Folix, so files look alike in both apps.
enum class FileCat {
    Folder, GameFolder, Up,
    Android, Windows, Archive, Video, Image, Audio, Text, Pdf, Code,
    Vita, Switch, Disc, Package, License, Plugin, Save,
    Unknown,
};

struct TypeInfo {
    FileCat cat = FileCat::Unknown;
    QString description;
};

QString extensionOf(const QString& name);
TypeInfo classify(const QString& name, bool isDir);   // from file-types.json

// Painted in code, cached. GUI thread only.
QIcon fileIcon(FileCat cat);
QIcon placeIcon(const QString& place);   // home, desktop, downloads, documents, volume, apps, add
void paintFileIcon(QPainter& p, const QRectF& box, FileCat cat);

// Book with a clasp: open = 0 locked, 1 unlocked; anything between while it snaps open.
void paintBookLock(QPainter& p, const QRectF& box, const QColor& color, qreal open);
QIcon bookLockIcon(bool open = false);
QIcon globeIcon();
QIcon helpIcon();
QIcon trayIcon();   // single-colour template for the menu bar
QIcon coffeeIcon();   // the donate button

} // namespace packix::ui
