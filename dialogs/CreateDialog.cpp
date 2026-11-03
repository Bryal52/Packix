#include "CreateDialog.h"

#include "Dialogs.h"
#include "Effects.h"
#include "Formats.h"
#include "Icons.h"
#include "Lang.h"
#include "Names.h"
#include "PathField.h"
#include "Platform.h"
#include "Theme.h"

#include <QComboBox>
#include <QDir>
#include <QFileInfo>
#include <QFormLayout>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QToolButton>
#include <QToolTip>
#include <QVBoxLayout>
#include <QVariantAnimation>

namespace packix::ui {

namespace {

QLabel* rowLabel(const QString& text, QWidget* parent)
{
    auto* l = new QLabel(text, parent);
    l->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    return l;
}

QFormLayout* form()
{
    auto* f = new QFormLayout;
    f->setContentsMargins(0, 0, 0, 0);
    f->setHorizontalSpacing(10);
    f->setVerticalSpacing(10);
    f->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    return f;
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
    // (?) explains the levels as soon as the cursor is over it.
    m_help = new QToolButton(this);
    m_help->setIcon(helpIcon());
    m_help->setAutoRaise(true);
    m_help->setFocusPolicy(Qt::NoFocus);
    m_help->setStyleSheet(QStringLiteral("QToolButton { border: none; background: transparent; padding: 0 2px; }"));
    m_help->installEventFilter(this);
    connect(m_help, &QToolButton::clicked, this, [this] {
        QToolTip::showText(m_help->mapToGlobal(QPoint(m_help->width(), m_help->height() / 2)), L("create.levelHelp"), m_help);
    });
    auto* levelRow = new QHBoxLayout;
    levelRow->setContentsMargins(0, 0, 0, 0);
    levelRow->addWidget(m_level, 1);
    levelRow->addWidget(m_help);

    // Password block: only for zip, slides open and shut.
    m_passwordBox = new QWidget(this);
    m_pass = new QLineEdit(m_passwordBox);
    m_again = new QLineEdit(m_passwordBox);
    for (QLineEdit* e : {m_pass, m_again})
        e->setEchoMode(QLineEdit::Password);
    m_problem = new QLabel(m_passwordBox);
    m_problem->setWordWrap(true);
    QPalette ep = m_problem->palette();
    ep.setColor(QPalette::WindowText, Theme::get().color("error"));
    m_problem->setPalette(ep);
    m_problem->hide();
    m_hint = new QLabel(m_passwordBox);
    m_hint->setWordWrap(true);
    m_hint->setForegroundRole(QPalette::PlaceholderText);
    QFont hf = m_hint->font();
    hf.setPointSizeF(hf.pointSizeF() - 1);
    m_hint->setFont(hf);
    m_hint->hide();
    auto* fade = new QGraphicsOpacityEffect(m_passwordBox);
    fade->setOpacity(1.0);   // Qt starts it at 0.7
    m_passwordBox->setGraphicsEffect(fade);

    m_dest = new PathField(this);
    m_dest->setPath(QFileInfo(sources.value(0)).absolutePath());

    auto* cancel = new QPushButton(L("common.cancel"), this);
    m_create = new QPushButton(L("create.button"), this);
    m_create->setDefault(true);
    m_create->setStyleSheet(Theme::get().accentButtonStyle());
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_create, &QPushButton::clicked, this, &CreateDialog::accept);

    // Three forms (top, password block, "Куда") share one label width, so they line up.
    QList<QLabel*> labels;
    auto label = [&](const char* key, QWidget* parent) {
        QLabel* l = rowLabel(L(key), parent);
        labels << l;
        return l;
    };
    QFormLayout* top = form();
    top->addRow(label("create.name", this), m_name);
    top->addRow(label("create.type", this), m_format);
    top->addRow(label("create.level", this), levelRow);
    QFormLayout* pw = form();
    pw->addRow(label("create.password", m_passwordBox), m_pass);
    pw->addRow(label("create.passwordAgain", m_passwordBox), m_again);
    pw->addRow(QString(), m_problem);   // right under the field it is about
    pw->addRow(QString(), m_hint);
    pw->setContentsMargins(0, 10, 0, 0);
    m_passwordBox->setLayout(pw);
    QFormLayout* dest = form();
    dest->addRow(label("create.destination", this), m_dest);
    int widest = 0;
    for (QLabel* l : std::as_const(labels))
        widest = qMax(widest, l->sizeHint().width());
    for (QLabel* l : std::as_const(labels))
        l->setFixedWidth(widest);

    auto* buttons = new QHBoxLayout;
    buttons->addStretch();
    buttons->addWidget(cancel);
    buttons->addWidget(m_create);

    auto* layout = new QVBoxLayout(this);
    const int m = Theme::get().metric("dialogMargin", 20);
    layout->setContentsMargins(m, m, m, m - 4);
    layout->setSpacing(0);
    layout->setSizeConstraint(QLayout::SetFixedSize);   // the window follows the password block
    layout->addLayout(top);
    layout->addWidget(m_passwordBox);
    layout->addSpacing(10);
    layout->addLayout(dest);   // "Куда" right above the buttons
    layout->addSpacing(14);
    layout->addLayout(buttons);

    m_fieldsAnim = new QVariantAnimation(this);
    m_fieldsAnim->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_fieldsAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
        const double t = v.toDouble();
        m_passwordBox->setMaximumHeight(int(m_passwordBox->sizeHint().height() * t));
        static_cast<QGraphicsOpacityEffect*>(m_passwordBox->graphicsEffect())->setOpacity(t);
    });
    connect(m_fieldsAnim, &QVariantAnimation::finished, this, [this] {
        if (m_passwordWanted)
            m_passwordBox->setMaximumHeight(QWIDGETSIZE_MAX);
        else
            m_passwordBox->hide();
    });

    connect(m_format, &QComboBox::currentIndexChanged, this, &CreateDialog::formatChanged);
    for (QLineEdit* e : {m_name, m_pass, m_again})
        connect(e, &QLineEdit::textChanged, this, &CreateDialog::validate);
    connect(m_dest, &PathField::changed, this, &CreateDialog::validate);
    // A wrong password shakes its field once the typing is done, not on every key.
    connect(m_again, &QLineEdit::editingFinished, this, [this] {
        if (!m_again->text().isEmpty() && m_again->text() != m_pass->text())
            shakeWidget(m_again);
    });
    connect(m_pass, &QLineEdit::editingFinished, this, [this] {
        const Format f = Formats::instance().byId(m_format->currentData().toString());
        if (!f.acceptsPassword(m_pass->text()))
            shakeWidget(m_pass);
    });
    formatChanged();
    m_name->setFocus();
}

bool CreateDialog::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_help) {
        if (event->type() == QEvent::Enter)
            QToolTip::showText(m_help->mapToGlobal(QPoint(m_help->width(), m_help->height() / 2)), L("create.levelHelp"), m_help);
        else if (event->type() == QEvent::Leave)
            QToolTip::hideText();
    }
    return QDialog::eventFilter(watched, event);
}

void CreateDialog::showEvent(QShowEvent* e)
{
    QDialog::showEvent(e);
    m_center = QRectF(frameGeometry()).center();
    // Translucent red title strip: the tint laid over the window colour.
    QColor tint = Theme::get().color("titleTint");
    const QColor base = palette().color(QPalette::Window);
    const double a = tint.alphaF();
    tint = QColor::fromRgbF(base.redF() * (1 - a) + tint.redF() * a, base.greenF() * (1 - a) + tint.greenF() * a,
                            base.blueF() * (1 - a) + tint.blueF() * a);
    platform::tintTitleBar(this, tint);
}

void CreateDialog::moveEvent(QMoveEvent* e)
{
    QDialog::moveEvent(e);
    if (!m_movingSelf && isVisible())
        m_center = QRectF(frameGeometry()).center();   // dragged by the user
}

void CreateDialog::resizeEvent(QResizeEvent* e)
{
    QDialog::resizeEvent(e);
    if (!isVisible() || m_center.isNull())
        return;
    // Grow and shrink around the centre instead of hanging from the top edge.
    const QSize frame = frameGeometry().size();
    m_movingSelf = true;
    move(QPointF(m_center.x() - frame.width() / 2.0, m_center.y() - frame.height() / 2.0).toPoint());
    m_movingSelf = false;
}

bool CreateDialog::passwordShown() const
{
    return m_passwordWanted;
}

void CreateDialog::showPasswordFields(bool show)
{
    if (show == m_passwordWanted && (m_passwordBox->isVisible() == show || !isVisible()))
        return;
    m_passwordWanted = show;
    m_fieldsAnim->stop();
    if (!isVisible() || Theme::reduceMotion()) {
        m_passwordBox->setVisible(show);
        m_passwordBox->setMaximumHeight(QWIDGETSIZE_MAX);
        static_cast<QGraphicsOpacityEffect*>(m_passwordBox->graphicsEffect())->setOpacity(1.0);
        return;
    }
    const double from = m_passwordBox->isVisible()
        ? double(m_passwordBox->height()) / qMax(1, m_passwordBox->sizeHint().height())
        : 0.0;
    if (show) {
        m_passwordBox->setMaximumHeight(0);
        m_passwordBox->show();
    }
    m_fieldsAnim->setDuration(Theme::get().motion("fieldsMs", 200));
    m_fieldsAnim->setStartValue(qBound(0.0, from, 1.0));
    m_fieldsAnim->setEndValue(show ? 1.0 : 0.0);
    m_fieldsAnim->start();
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
    m_hint->setText(f.passwordAsciiOnly ? L("password.asciiHint") : QString());
    showPasswordFields(withPassword);
    validate();
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
    const bool withPassword = passwordShown();
    const QString pass = m_pass->text();
    const QString name = m_name->text().trimmed();

    QString problem;
    if (withPassword && !pass.isEmpty() && !f.acceptsPassword(pass))
        problem = L("password.asciiOnly");
    else if (withPassword && !m_again->text().isEmpty() && m_again->text() != pass)
        problem = L("password.mismatch");
    m_problem->setText(problem);
    m_problem->setVisible(!problem.isEmpty());
    m_hint->setVisible(withPassword && problem.isEmpty() && f.passwordAsciiOnly && !pass.isEmpty());

    const bool badName = !name.isEmpty() && Names::forThisSystem(name) != name;
    m_name->setToolTip(badName ? L("create.badName") : QString());
    QPalette np = m_name->palette();
    np.setColor(QPalette::Text, badName ? Theme::get().color("error") : palette().color(QPalette::Text));
    m_name->setPalette(np);

    const bool passwordOk = !withPassword || (pass == m_again->text() && f.acceptsPassword(pass));
    m_create->setEnabled(!name.isEmpty() && !badName && problem.isEmpty() && passwordOk && !m_dest->path().isEmpty());
}

void CreateDialog::shakeProblem()
{
    const Format f = Formats::instance().byId(m_format->currentData().toString());
    const QString name = m_name->text().trimmed();
    if (passwordShown() && !f.acceptsPassword(m_pass->text()))
        shakeWidget(m_pass);
    else if (passwordShown() && m_again->text() != m_pass->text())
        shakeWidget(m_again);
    else if (name.isEmpty() || Names::forThisSystem(name) != name)
        shakeWidget(m_name);
}

void CreateDialog::keyPressEvent(QKeyEvent* e)
{
    if ((e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) && !m_create->isEnabled()) {
        shakeProblem();
        return;
    }
    QDialog::keyPressEvent(e);
}

void CreateDialog::accept()
{
    if (!m_create->isEnabled()) {
        shakeProblem();
        return;
    }
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
    m_request.password = passwordShown() ? m_pass->text() : QString();
    m_request.replaceExisting = replace;
    QDialog::accept();
}

} // namespace packix::ui
