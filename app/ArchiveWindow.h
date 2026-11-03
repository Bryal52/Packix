#pragma once

#include "Types.h"

#include <QHash>
#include <QPersistentModelIndex>
#include <QPointer>
#include <QSet>
#include <QWidget>

class QCheckBox;
class QFileSystemWatcher;
class QLabel;
class QLineEdit;
class QPushButton;
class QStackedWidget;
class QTimer;

namespace packix::ui {

class ArchiveModel;
class ArchiveView;
class BookLockWidget;
class DropZone;

// One archive: its contents as a tree and what can be done with it.
class ArchiveWindow : public QWidget {
    Q_OBJECT
public:
    // A nested archive knows where it came from, to offer saving changes back.
    struct Origin {
        QPointer<ArchiveWindow> window;
        QString workDir;     // temporary folder it was unpacked into
        QString relPath;     // its path inside the outer archive
    };

    explicit ArchiveWindow(const QString& path, Origin origin = {});
    ~ArchiveWindow() override;

    QString archivePath() const { return m_path; }
    // Opens the archive and shows the window. False (and the window is gone) if it cannot be opened.
    bool start();

    // Called by the view.
    void dragOut();
    void dropIn(const QStringList& paths, const QString& folder);
    void openItem(const QModelIndex& index);
    void quickLook();
    void deleteSelected();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void buildUi();
    void retranslate();
    void showContents();
    void showLocked();
    void tryLockedPassword();
    bool reload();
    void updateState();
    void updateStatus();

    QStringList selectedPaths() const;
    // Unpacks items into a new temporary folder, keeping their paths. Empty on failure.
    QString unpackToTemp(const QStringList& items, const QString& prefix);

    void extractSelected();
    void setPassword();
    void changePassword();
    void removePassword();
    void runPasswordChange(const QString& newPassword, const QString& targetFormat);
    void afterChange();   // reload and tell the outer archive
    void childChanged(const QString& workDir, const QString& relPath);
    void fileEdited(const QString& file);

    QString m_path;
    Origin m_origin;
    ArchiveInfo m_info;
    QString m_password;

    ArchiveModel* m_model = nullptr;
    ArchiveView* m_view = nullptr;
    QStackedWidget* m_stack = nullptr;
    QPushButton* m_extract = nullptr;
    QPushButton* m_delete = nullptr;
    QPushButton* m_passwordBtn = nullptr;
    QLabel* m_lockIcon = nullptr;
    QLabel* m_lockText = nullptr;
    QLineEdit* m_search = nullptr;
    QLabel* m_statusLeft = nullptr;
    QLabel* m_statusRight = nullptr;
    DropZone* m_empty = nullptr;

    // locked page (encrypted names)
    BookLockWidget* m_lockedBook = nullptr;
    QLabel* m_lockedTitle = nullptr;
    QLabel* m_lockedText = nullptr;
    QLineEdit* m_lockedEdit = nullptr;
    QLabel* m_lockedError = nullptr;
    QCheckBox* m_lockedRemember = nullptr;
    QPushButton* m_lockedOpen = nullptr;

    QStringList m_tempDirs;
    QFileSystemWatcher* m_watcher = nullptr;
    QHash<QString, std::pair<QString, QString>> m_opened;   // file -> (workDir, relPath)
    QSet<QString> m_pendingEdits;
    bool m_busy = false;
    QTimer* m_hoverTimer = nullptr;
    QPersistentModelIndex m_hoverIndex;
};

} // namespace packix::ui
