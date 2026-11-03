#include "ExtractDialog.h"

#include "Dialogs.h"
#include "Formats.h"
#include "Lang.h"
#include "PathField.h"
#include "Theme.h"
#include "Types.h"

#include <QCheckBox>
#include <QDir>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QRegularExpression>
#include <QVBoxLayout>

namespace packix::ui {

ExtractDialog::ExtractDialog(const QString& archivePath, int selectedCount, QWidget* parent)
    : QDialog(parent), m_archive(archivePath)
{
    setWindowTitle(L("extract.windowTitle"));
    setWindowModality(Qt::WindowModal);
    Theme::get().apply(this);

    const QFileInfo fi(archivePath);
    m_folderName = Formats::instance().splitName(fi.fileName()).first;
    // "Игра.part1" / "Игра.7z" (from Игра.7z.001) -> "Игра"
    m_folderName.remove(QRegularExpression(QStringLiteral("\\.part\\d+$"), QRegularExpression::CaseInsensitiveOption));
    const QString inner = Formats::instance().splitName(m_folderName).first;
    if (Formats::instance().detect(m_folderName).isValid())
        m_folderName = inner;

    auto* title = new QLabel(selectedCount > 0 ? Ln("extract.titleSome", selectedCount, {{"name", fi.fileName()}})
                                               : L("extract.titleAll", {{"name", fi.fileName()}}),
                             this);
    QFont f = title->font();
    f.setBold(true);
    f.setPointSizeF(f.pointSizeF() + 1);
    title->setFont(f);
    title->setWordWrap(true);

    m_dest = new PathField(this);
    m_dest->addPlace(L("extract.nextToArchive"), fi.absolutePath());
    m_dest->setPath(fi.absolutePath());
    m_subfolder = new QCheckBox(L("extract.subfolder", {{"name", m_folderName}}), this);
    m_subfolder->setChecked(selectedCount == 0);

    auto* cancel = new QPushButton(L("common.cancel"), this);
    auto* ok = new QPushButton(L("extract.button"), this);
    ok->setDefault(true);
    ok->setStyleSheet(Theme::get().accentButtonStyle());
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(ok, &QPushButton::clicked, this, &ExtractDialog::accept);
    connect(m_dest, &PathField::changed, this, [this, ok] { ok->setEnabled(!m_dest->path().isEmpty()); });

    auto* form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->addRow(L("create.destination"), m_dest);
    form->addRow(QString(), m_subfolder);

    auto* buttons = new QHBoxLayout;
    buttons->addStretch();
    buttons->addWidget(cancel);
    buttons->addWidget(ok);

    auto* layout = new QVBoxLayout(this);
    const int m = Theme::get().metric("dialogMargin", 20);
    layout->setContentsMargins(m, m, m, m - 4);
    layout->setSpacing(10);
    layout->addWidget(title);
    layout->addLayout(form);
    layout->addLayout(buttons);
    setMinimumWidth(480);
}

void ExtractDialog::accept()
{
    QString dir = m_dest->path();
    if (dir.isEmpty())
        return;
    if (!QFileInfo(dir).isDir()) {
        if (!Dialogs::confirm(this, L("path.missingTitle"), L("path.missingText", {{"path", dir}}), L("path.create")))
            return;
        if (!QDir().mkpath(dir)) {
            Dialogs::showError(this, Status::fail(Error::NoAccess, dir), QString());
            return;
        }
    }
    if (m_subfolder->isChecked())
        dir = QDir(dir).filePath(m_folderName);
    m_result = dir;
    QDialog::accept();
}

} // namespace packix::ui
