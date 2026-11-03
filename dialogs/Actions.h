#pragma once

#include "Archiver.h"

#include <functional>

class QWidget;

// Complete user actions built from the dialogs; Packix and Folix call these.
namespace packix::ui::Actions {

// "Архивировать": the dialog, then the job. done() gets the new archive's path.
void createArchive(const QStringList& sources, QWidget* parent = nullptr,
                   std::function<void(const QString& archive)> done = {});

// Makes sure the password is known when something inside is encrypted (asks, tries saved ones).
bool ensurePassword(QWidget* parent, const ArchiveInfo& info, QString* password);

// "Распаковать": asks where (unless destination is given), then unpacks with conflict questions.
void extract(QWidget* parent, const ArchiveInfo& info, const QStringList& items, QString* password,
             const QString& destination = {}, std::function<void(const Status&, const QStringList& created)> done = {});

// Opens any archive file (volumes, passwords for hidden names). False if the user gave up or it failed.
bool open(QWidget* parent, const QString& path, ArchiveInfo* info, QString* password);

} // namespace packix::ui::Actions
