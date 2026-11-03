#include "Donate.h"

#include "Icons.h"
#include "Lang.h"
#include "Shared.h"
#include "Theme.h"

#include <QApplication>
#include <QClipboard>
#include <QDialog>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

namespace packix::ui {

namespace {

QString grouped(const QString& number)
{
    QString out;
    for (int i = 0; i < number.size(); ++i) {
        if (i > 0 && i % 4 == 0)
            out += QLatin1Char(' ');
        out += number.at(i);
    }
    return out;
}

// A bank card drawn like the real thing: Visa blue, Mastercard dark with its two circles.
class CardWidget : public QWidget {
public:
    CardWidget(QString brand, QString number, QString holder, QWidget* parent)
        : QWidget(parent), m_brand(std::move(brand)), m_number(std::move(number)), m_holder(std::move(holder))
    {
        setFixedSize(330, 208);
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF r = QRectF(rect()).adjusted(4, 2, -4, -8);
        const bool visa = m_brand == QLatin1String("visa");

        // shadow
        for (int i = 6; i >= 1; --i) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0, 0, 0, 8));
            p.drawRoundedRect(r.translated(0, i * 0.8).adjusted(-i * 0.4, 0, i * 0.4, i * 0.6), 16, 16);
        }
        QLinearGradient bg(r.topLeft(), r.bottomRight());
        bg.setColorAt(0, visa ? QColor(26, 31, 113) : QColor(40, 40, 44));
        bg.setColorAt(1, visa ? QColor(48, 86, 196) : QColor(18, 18, 20));
        p.setBrush(bg);
        p.drawRoundedRect(r, 16, 16);
        // a soft sheen
        QLinearGradient sheen(r.topLeft(), QPointF(r.left(), r.center().y()));
        sheen.setColorAt(0, QColor(255, 255, 255, 34));
        sheen.setColorAt(1, QColor(255, 255, 255, 0));
        p.setBrush(sheen);
        p.drawRoundedRect(r, 16, 16);

        // chip
        QLinearGradient chip(0, r.top() + 58, 0, r.top() + 92);
        chip.setColorAt(0, QColor(236, 205, 130));
        chip.setColorAt(1, QColor(196, 158, 80));
        p.setBrush(chip);
        p.drawRoundedRect(QRectF(r.left() + 24, r.top() + 58, 44, 34), 6, 6);
        p.setPen(QPen(QColor(150, 115, 50, 140), 1));
        p.drawLine(QPointF(r.left() + 24, r.top() + 75), QPointF(r.left() + 68, r.top() + 75));
        p.drawLine(QPointF(r.left() + 46, r.top() + 58), QPointF(r.left() + 46, r.top() + 92));

        // brand mark
        if (visa) {
            QFont f;
            f.setBold(true);
            f.setItalic(true);
            f.setPixelSize(34);
            f.setLetterSpacing(QFont::AbsoluteSpacing, 1);
            p.setFont(f);
            p.setPen(Qt::white);
            p.drawText(QRectF(r.right() - 120, r.top() + 14, 100, 44), Qt::AlignRight | Qt::AlignVCenter, QStringLiteral("VISA"));
        } else {
            const QPointF c(r.right() - 58, r.top() + 36);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(235, 0, 27));
            p.drawEllipse(c, 20, 20);
            p.setBrush(QColor(247, 158, 27, 230));
            p.drawEllipse(c + QPointF(24, 0), 20, 20);
        }

        // number and holder
        QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        mono.setPixelSize(20);
        mono.setLetterSpacing(QFont::AbsoluteSpacing, 0.5);
        p.setFont(mono);
        p.setPen(QColor(255, 255, 255, 240));
        p.drawText(QRectF(r.left() + 24, r.top() + 108, r.width() - 48, 34), Qt::AlignLeft | Qt::AlignVCenter, grouped(m_number));
        QFont small;
        small.setPixelSize(13);
        small.setBold(true);
        small.setLetterSpacing(QFont::AbsoluteSpacing, 1.6);
        p.setFont(small);
        p.setPen(QColor(255, 255, 255, 200));
        p.drawText(QRectF(r.left() + 24, r.bottom() - 46, r.width() - 48, 26), Qt::AlignLeft | Qt::AlignVCenter, m_holder);
    }

private:
    QString m_brand, m_number, m_holder;
};

} // namespace

DonateButton::DonateButton(QWidget* parent) : QToolButton(parent)
{
    setAutoRaise(true);
    setFocusPolicy(Qt::NoFocus);
    setIcon(coffeeIcon());
    setIconSize(QSize(18, 18));
    auto retranslate = [this] { setToolTip(L("donate.tooltip")); };
    retranslate();
    connect(&Lang::instance(), &Lang::changed, this, retranslate);
    connect(this, &QToolButton::clicked, this, [this] { showDonateWindow(window()); });
}

void showDonateWindow(QWidget* parent)
{
    static QPointer<QDialog> open;
    if (open) {
        open->raise();
        open->activateWindow();
        return;
    }
    const QJsonObject data = Shared::load(QStringLiteral("donate/donate.json"));
    const QString holder = data.value(QStringLiteral("holder")).toString();

    auto* dlg = new QDialog(parent);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowTitle(L("donate.windowTitle"));
    Theme::get().apply(dlg);
    open = dlg;

    // The kitten (any PNG with the same name in shared/donate replaces it).
    auto* kitten = new QLabel(dlg);
    QPixmap pm(Shared::path(data.value(QStringLiteral("image")).toString()));
    if (!pm.isNull()) {
        const qreal dpr = dlg->devicePixelRatioF();
        pm = pm.scaled(QSize(190, 190) * dpr, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        pm.setDevicePixelRatio(dpr);
        kitten->setPixmap(pm);
    }
    kitten->setAlignment(Qt::AlignCenter);

    auto* title = new QLabel(L("donate.title"), dlg);
    QFont tf = title->font();
    tf.setBold(true);
    tf.setPointSizeF(tf.pointSizeF() + 5);
    title->setFont(tf);
    title->setAlignment(Qt::AlignCenter);
    auto* note = new QLabel(L("donate.note"), dlg);
    note->setWordWrap(true);
    note->setAlignment(Qt::AlignCenter);
    note->setForegroundRole(QPalette::PlaceholderText);

    auto* cards = new QHBoxLayout;
    cards->setSpacing(22);
    cards->addStretch();
    for (const QJsonValue& v : data.value(QStringLiteral("cards")).toArray()) {
        const QJsonObject c = v.toObject();
        const QString number = c.value(QStringLiteral("number")).toString();
        auto* column = new QVBoxLayout;
        column->setSpacing(8);
        column->addWidget(new CardWidget(c.value(QStringLiteral("brand")).toString(), number, holder, dlg));
        auto* copy = new QPushButton(L("donate.copy"), dlg);
        copy->setAutoDefault(false);
        QObject::connect(copy, &QPushButton::clicked, copy, [copy, number] {
            QApplication::clipboard()->setText(number);
            copy->setText(L("donate.copied"));
            QTimer::singleShot(1600, copy, [copy] { copy->setText(L("donate.copy")); });
        });
        column->addWidget(copy, 0, Qt::AlignHCenter);
        cards->addLayout(column);
    }
    cards->addStretch();

    auto* close = new QPushButton(L("donate.close"), dlg);
    close->setDefault(true);
    close->setStyleSheet(Theme::get().accentButtonStyle());
    QObject::connect(close, &QPushButton::clicked, dlg, &QDialog::accept);
    auto* buttons = new QHBoxLayout;
    buttons->addStretch();
    buttons->addWidget(close);

    auto* layout = new QVBoxLayout(dlg);
    layout->setContentsMargins(28, 22, 28, 18);
    layout->setSpacing(10);
    layout->addWidget(kitten);
    layout->addWidget(title);
    layout->addWidget(note);   // no alignment flag: a wrapped label then gets its full height
    layout->addSpacing(12);
    layout->addLayout(cards);
    layout->addSpacing(8);
    layout->addLayout(buttons);
    dlg->show();
    dlg->raise();
    dlg->activateWindow();
    close->setFocus();
}

} // namespace packix::ui
