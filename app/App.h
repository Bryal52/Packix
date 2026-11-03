#pragma once

#include <QObject>
#include <QPointer>
#include <QStringList>

class QMenu;
class QSystemTrayIcon;
class QTimer;
class QWidget;

namespace packix::ui {

// Lives in the background with a menu-bar icon; windows appear only when there is something to do.
class App : public QObject {
    Q_OBJECT
public:
    explicit App(bool background);

    void openArchive(const QString& path);
    // From the command line or another start of Packix: "open" archives or "archive" files.
    // Explorer starts Packix once per selected file; those arrive here and become one archive.
    void request(const QString& mode, const QStringList& files);
    void archiveFiles(const QStringList& files);
    // Packix started by hand without a file: a small "drop an archive here" window.
    void showLauncherIfIdle();
    void showLauncher();
    void showSettings();
    void showAbout();
    void firstRunIfNeeded();
    void chooseAndOpen();
    void chooseAndCreate();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void rebuildMenu();
    void updateForeground();
    QString archiveFilter() const;

    QSystemTrayIcon* m_tray;
    QMenu* m_menu;
    QPointer<QWidget> m_launcher;
    QPointer<QWidget> m_settings;
    QStringList m_pendingArchive;
    QTimer* m_archiveBatch = nullptr;
    bool m_sawFile = false;
    bool m_foreground = false;
};

} // namespace packix::ui
