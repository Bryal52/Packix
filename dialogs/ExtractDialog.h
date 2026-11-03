#pragma once

#include <QDialog>

class QCheckBox;

namespace packix::ui {

class PathField;

// "Распаковать": Куда (address + quick folders + "Рядом с архивом") and
// "Создать папку «Имя архива»".
class ExtractDialog : public QDialog {
    Q_OBJECT
public:
    ExtractDialog(const QString& archivePath, int selectedCount, QWidget* parent = nullptr);
    // Folder to unpack into, the archive-named subfolder included when chosen.
    QString destination() const { return m_result; }

protected:
    void accept() override;

private:
    QString m_archive;
    QString m_folderName;
    PathField* m_dest;
    QCheckBox* m_subfolder;
    QString m_result;
};

} // namespace packix::ui
