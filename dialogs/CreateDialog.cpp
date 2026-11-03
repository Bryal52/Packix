#include "CreateDialog.h"

#include "Dialogs.h"
#include "Formats.h"
#include "Icons.h"
#include "Lang.h"
#include "Names.h"
#include "PathField.h"
#include "Theme.h"

#include <QComboBox>
#include <QDir>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QToolButton>
#include <QToolTip>
#include <QVBoxLayout>

namespace packix::ui {

namespace {

QLabel* smallLabel(QWidget* parent, bool warning)
{
    auto* l = new QLabel(parent);
    l->setWordWrap(true);
    QFont f = l->font();
    f.setPointSizeF(f.pointSizeF() - 1);
    l->setFont(f);
    QPalette pal = l->palette();
    pal.setColor(QPalette::WindowText, warning ? Theme::get().color("warning") : pal.color(QPalette::PlaceholderText));
    l->setPalette(pal);
    l->hide();
    return l;
}

} // namespace

CreateDialog::CreateDialog(const QStringList& sources, QWidget* parent) : QDialog(parent), m_sources(sources)
{
    setWindowTitle(L("create.windowTitle"));
    Theme::get().apply(this);

    m_name = new QLineEdit(this);
    m_name->setPlaceholderText(L("create.namePlaceholder"));
    m_name->setMinimumWidth(320);

    m_format = new QComboBox(this);
    for (const Format& f : Formats::instance().creatable())
        m_format->addItem(f.name, f.id);
    m_format->setCurrentIndex(qMax(0, m_format->findData(Formats::instance().defaultCreate())));

    m_level = new QComboBox(this);
    for (const CompressionLevel& l : Formats::instance().levels())
        m_level->addItem(L((QStringLiteral("level.") + l.id).toLatin1().constData()), l.id);
    m_level->setCurrentIndex(qMax(0, m_level->findData(Formats::instance().defaultLevel())));
    auto* help = new QToolButton(this);
    help->setIcon(helpIcon());
    help->setAutoRaise(true);
    help->setFocusPolicy(Qt::NoFocus);
    help->setStyleSheet(QStringLiteral("QToolButton { border: none; background: transparent; padding: 0 2px; }"));
    help->setToolTip(L("create.levelHelp"));
    connect(help, &QToolButton::clicked, this, [help] {
        QToolTip::showText(help->mapToGlobal(QPoint(help->width(), help->height() / 2)), help->toolTip(), help);
    });
    auto* levelRow = new QHBoxLayout;
    levelRow->setContentsMargins(0, 0, 0, 0);
    levelRow->addWidget(m_level, 1);
    levelRow->addWidget(help);

    m_pass = new QLineEdit(this);
    m_again = new QLineEdit(this);
    for (QLineEdit* e : {m_pass, m_again})
        e->setEchoMode(QLineEdit::Password);
    m_hint = smallLabel(this, false);
    m_problem = smallLabel(this, true);

    m_dest = new PathField(this);
    m_dest->setPath(QFileInfo(sources.value(0)).absolutePath());

    auto* cancel = new QPushButton(L("common.cancel"), this);
    m_create = new QPushButton(L("create.button"), this);
    m_create->setDefault(true);
    m_create->setStyleSheet(Theme::get().accentButtonStyle());
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_create, &QPushButton::clicked, this, &CreateDialog::accept);

    m_form = new QFormLayout;
    m_form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    m_form->addRow(L("create.name"), m_name);
    m_form->addRow(L("create.type"), m_format);
    m_form->addRow(L("create.level"), levelRow);
    m_form->addRow(L("create.password"), m_pass);
    m_form->addRow(L("create.passwordAgain"), m_again);
    m_form->addRow(QString(), m_hint);
    m_form->addRow(QString(), m_problem);
    // "Куда" sits last, right above the buttons, in the same column as the other labels.
    m_form->addRow(L("create.destination"), m_dest);

    auto* buttons = new QHBoxLayout;
    buttons->addStretch();
    buttons->addWidget(cancel);
    buttons->addWidget(m_create);

    auto* layout = new QVBoxLayout(this);
    const int m = Theme::get().metric("dialogMargin", 20);
    layout->setContentsMargins(m, m, m, m - 4);
    layout->setSpacing(10);
    layout->addLayout(m_form);
    layout->addLayout(buttons);

    connect(m_format, &QComboBox::currentIndexChanged, this, &CreateDialog::formatChanged);
    for (QLineEdit* e : {m_name, m_pass, m_again})
        connect(e, &QLineEdit::textChanged, this, &CreateDialog::validate);
    connect(m_dest, &PathField::changed, this, &CreateDialog::validate);
    formatChanged();
    m_name->setFocus();
}

void CreateDialog::formatChanged()
{
    const Format f = Formats::instance().byId(m_format->currentData().toString());
    // The password exists only for zip; switching away wipes it, so nothing hidden gets encrypted.
    const bool withPassword = f.canPassword && f.id == QLatin1String("zip");
    if (!withPassword) {
        m_pass->clear();
        m_again->clear();
    }
    m_form->setRowVisible(m_pass, withPassword);
    m_form->setRowVisible(m_again, withPassword);
    m_hint->setText(f.passwordAsciiOnly ? L("password.asciiHint") : QString());
    validate();
    adjustSize();
}

QString CreateDialog::fileName() const
{
    QString name = m_name->text().trimmed();
    const Format f = Formats::instance().byId(m_format->currentData().toString());
    // "Фото.zip" typed with zip selected stays "Фото.zip", not "Фото.zip.zip".
    for (const QString& ext : f.extensions) {
        if (name.endsWith(QLatin1Char('.') + ext, Qt::CaseInsensitive))
            return name;
    }
    return name + QLatin1Char('.') + f.primaryExtension();
}

void CreateDialog::validate()
{
    const Format f = Formats::instance().byId(m_format->currentData().toString());
    const bool withPassword = m_form->isRowVisible(m_pass);
    const QString pass = m_pass->text();
    const QString name = m_name->text().trimmed();

    QString problem;
    if (!name.isEmpty() && Names::forThisSystem(name) != name)
        problem = L("create.badName");
    else if (withPassword && !pass.isEmpty() && !f.acceptsPassword(pass))
        problem = L("password.asciiOnly");
    else if (withPassword && !m_again->text().isEmpty() && m_again->text() != pass)
        problem = L("password.mismatch");
    m_problem->setText(problem);
    m_problem->setVisible(!problem.isEmpty());
    m_hint->setVisible(withPassword && f.passwordAsciiOnly && (!pass.isEmpty() || m_pass->hasFocus()));

    const bool passwordOk = !withPassword || (pass == m_again->text() && f.acceptsPassword(pass));
    m_create->setEnabled(!name.isEmpty() && problem.isEmpty() && passwordOk && !m_dest->path().isEmpty());
}

void CreateDialog::accept()
{
    if (!m_create->isEnabled())
        return;
    const QString dir = m_dest->path();
    if (!QFileInfo(dir).isDir()) {
        if (!Dialogs::confirm(this, L("path.missingTitle"), L("path.missingText", {{"path", dir}}), L("path.create")))
            return;
        if (!QDir().mkpath(dir)) {
            Dialogs::showError(this, Status::fail(Error::NoAccess, dir), QString());
            return;
        }
    }
    const QString file = fileName();
    bool replace = false;
    if (QFileInfo::exists(QDir(dir).filePath(file))) {
        const Dialogs::ConflictAnswer a = Dialogs::askConflict(this, QDir(dir).filePath(file), false);
        if (a.choice == Conflict::Cancel)
            return;   // back to the form, the name can be changed
        replace = a.choice == Conflict::Replace;
    }
    m_request = {};
    m_request.sources = m_sources;
    m_request.destinationDir = dir;
    m_request.fileName = file;
    m_request.formatId = m_format->currentData().toString();
    m_request.levelId = m_level->currentData().toString();
    m_request.password = m_form->isRowVisible(m_pass) ? m_pass->text() : QString();
    m_request.replaceExisting = replace;
    QDialog::accept();
}

} // namespace packix::ui
