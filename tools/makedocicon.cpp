// Document icon for archives in Finder: a white page with a folded corner, the Packix logo on it
// and the format in a soft red band. Usage: makedocicon <logo.png> <LABEL> <out.png> [r g b]

#include <QFont>
#include <QGuiApplication>
#include <QImage>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>

#include <cstdio>
#include <cstdlib>

int main(int argc, char* argv[])
{
    if (argc < 4) {
        std::fprintf(stderr, "usage: makedocicon <logo.png> <LABEL> <out.png> [r g b]\n");
        return 2;
    }
    QGuiApplication app(argc, argv);   // fonts need it
    const QImage logo(QString::fromLocal8Bit(argv[1]));
    const QString label = QString::fromUtf8(argv[2]);
    const QColor band = argc >= 7 ? QColor(std::atoi(argv[4]), std::atoi(argv[5]), std::atoi(argv[6])) : QColor(200, 100, 90);

    QImage out(1024, 1024, QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);
    QPainter p(&out);
    p.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform | QPainter::TextAntialiasing);

    // Page on Apple's document grid: about 3:4 inside the 1024 square.
    const QRectF page(176, 64, 672, 896);
    const qreal fold = 190;
    QPainterPath body;
    body.moveTo(page.left() + 40, page.top());
    body.lineTo(page.right() - fold, page.top());
    body.lineTo(page.right(), page.top() + fold);
    body.lineTo(page.right(), page.bottom() - 40);
    body.quadTo(page.right(), page.bottom(), page.right() - 40, page.bottom());
    body.lineTo(page.left() + 40, page.bottom());
    body.quadTo(page.left(), page.bottom(), page.left(), page.bottom() - 40);
    body.lineTo(page.left(), page.top() + 40);
    body.quadTo(page.left(), page.top(), page.left() + 40, page.top());

    for (int i = 10; i >= 1; --i) {   // soft shadow
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 6));
        p.drawPath(body.translated(0, 6 + i));
    }
    QLinearGradient g(0, page.top(), 0, page.bottom());
    g.setColorAt(0, QColor(255, 255, 255));
    g.setColorAt(1, QColor(240, 240, 243));
    p.setBrush(g);
    p.setPen(QPen(QColor(0, 0, 0, 40), 3));
    p.drawPath(body);

    QPainterPath corner;
    corner.moveTo(page.right() - fold, page.top());
    corner.lineTo(page.right() - fold, page.top() + fold - 30);
    corner.quadTo(page.right() - fold, page.top() + fold, page.right() - fold + 30, page.top() + fold);
    corner.lineTo(page.right(), page.top() + fold);
    corner.closeSubpath();
    p.setBrush(QColor(222, 222, 228));
    p.drawPath(corner);

    // The logo tile, centred in the upper part of the page.
    const QRectF tile(page.center().x() - 210, page.top() + 170, 420, 420);
    QPainterPath clip;
    clip.addRoundedRect(tile, 94, 94);
    p.save();
    p.setClipPath(clip);
    // Crop the logo to its own tile (the source has a black margin around it).
    const qreal s = logo.width();
    p.drawImage(tile, logo, QRectF(s * 0.095, s * 0.095, s * 0.81, s * 0.81));
    p.restore();

    // Format band.
    const QRectF bandRect(page.left() + 70, page.bottom() - 250, page.width() - 140, 150);
    p.setPen(Qt::NoPen);
    p.setBrush(band);
    p.drawRoundedRect(bandRect, 34, 34);
    QFont f;
    f.setBold(true);
    f.setPixelSize(label.size() > 3 ? 92 : 112);
    f.setLetterSpacing(QFont::AbsoluteSpacing, 6);
    p.setFont(f);
    p.setPen(Qt::white);
    p.drawText(bandRect, Qt::AlignCenter, label);
    p.end();

    if (!out.save(QString::fromLocal8Bit(argv[3]))) {
        std::fprintf(stderr, "cannot write %s\n", argv[3]);
        return 1;
    }
    return 0;
}
