#pragma once

#include "Types.h"

#include <QString>
#include <QStringList>

#include <functional>

class QWidget;

namespace packix::ui::Dialogs {

// Runs `work` on a pool thread while the interface keeps painting (input is held until it is done).
Status runAsync(const std::function<Status()>& work);

bool confirm(QWidget* parent, const QString& title, const QString& text, const QString& okText,
             bool destructive = false);
void info(QWidget* parent, const QString& title, const QString& text);

// Plain words for a failed operation, with the engine output under "Подробнее". Also logged.
QString errorTitle(const Status& st);
QString errorText(const Status& st, const QString& archiveName);
void showError(QWidget* parent, const Status& st, const QString& archiveName);

struct ConflictAnswer {
    Conflict choice = Conflict::Cancel;
    bool applyToAll = false;
};
// "«Фото.zip» уже есть в этой папке" — Отмена / [Пропустить] / Оставить оба / Заменить.
// Unpacking offers Пропустить and "Применить ко всем".
ConflictAnswer askConflict(QWidget* parent, const QString& existingPath, bool forExtract);
// For worker threads: asks on the GUI thread and remembers "Применить ко всем".
ConflictHandler conflictHandler(QWidget* parent, bool forExtract);

// Asks for the password until `check` accepts it or the user gives up. Saved passwords are tried
// first, silently. Returns the last check's status (Cancelled when closed).
Status askPassword(QWidget* parent, const QString& archiveName, const std::function<Status(const QString&)>& check,
                   QString* password, bool trySaved = true);

struct NewPassword {
    QString password;
    QString targetFormatId;   // set when the archive's own format cannot hold a password
};
// "Поставить пароль" / "Сменить пароль": the password twice; formatId is the archive's format.
bool askNewPassword(QWidget* parent, const QString& archiveName, const QString& formatId, bool change, NewPassword* out);

} // namespace packix::ui::Dialogs
