// Draws the white kitten with a coffee mug for the donate window: makekitten <out.png>
// The PNG lives in shared/donate/ and can be replaced by any picture with the same name.

#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>

#include <cstdio>

int main(int argc, char* argv[])
{
    if (argc < 2) {
        std::fprintf(stderr, "usage: makekitten <out.png>\n");
        return 2;
    }
    QGuiApplication app(argc, argv);
    QImage img(1024, 1024, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    p.scale(2, 2);   // drawn on a 512 grid

    const QColor fur(255, 255, 255);
    const QColor line(226, 210, 208);
    const QColor pink(255, 190, 200);
    const QColor ink(46, 32, 36);
    const QColor red(210, 74, 62);
    const QPen outline(line, 4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);

    // Soft warm glow behind.
    QRadialGradient glow(QPointF(256, 290), 230);
    glow.setColorAt(0, QColor(255, 226, 222));
    glow.setColorAt(1, QColor(255, 226, 222, 0));
    p.setPen(Qt::NoPen);
    p.setBrush(glow);
    p.drawEllipse(QPointF(256, 290), 230, 230);

    // Tail curling up on the left.
    QPainterPath tail;
    tail.moveTo(170, 410);
    tail.cubicTo(90, 420, 70, 330, 120, 300);
    tail.cubicTo(140, 290, 150, 305, 138, 318);
    tail.cubicTo(110, 345, 130, 385, 180, 385);
    p.setPen(outline);
    p.setBrush(fur);
    p.drawPath(tail);

    // Body.
    p.drawEllipse(QPointF(256, 370), 118, 100);

    // Ears.
    QPainterPath ears;
    ears.moveTo(160, 175);
    ears.lineTo(150, 70);
    ears.quadTo(155, 62, 165, 68);
    ears.lineTo(230, 128);
    ears.moveTo(352, 175);
    ears.lineTo(362, 70);
    ears.quadTo(357, 62, 347, 68);
    ears.lineTo(282, 128);
    p.drawPath(ears);
    QPainterPath inner;
    inner.moveTo(170, 150);
    inner.lineTo(163, 92);
    inner.lineTo(208, 133);
    inner.closeSubpath();
    inner.moveTo(342, 150);
    inner.lineTo(349, 92);
    inner.lineTo(304, 133);
    inner.closeSubpath();
    p.setPen(Qt::NoPen);
    p.setBrush(pink);
    p.drawPath(inner);

    // Head.
    p.setPen(outline);
    p.setBrush(fur);
    p.drawEllipse(QPointF(256, 215), 122, 104);

    // Blush.
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 170, 185, 110));
    p.drawEllipse(QPointF(188, 258), 22, 13);
    p.drawEllipse(QPointF(324, 258), 22, 13);

    // Eyes with highlights.
    p.setBrush(ink);
    p.drawEllipse(QPointF(212, 222), 17, 21);
    p.drawEllipse(QPointF(300, 222), 17, 21);
    p.setBrush(Qt::white);
    p.drawEllipse(QPointF(218, 213), 6.5, 6.5);
    p.drawEllipse(QPointF(306, 213), 6.5, 6.5);
    p.drawEllipse(QPointF(207, 230), 3, 3);
    p.drawEllipse(QPointF(295, 230), 3, 3);

    // Nose and the little "ω" mouth.
    QPainterPath nose;
    nose.moveTo(248, 246);
    nose.quadTo(256, 242, 264, 246);
    nose.quadTo(262, 254, 256, 257);
    nose.quadTo(250, 254, 248, 246);
    p.setBrush(QColor(255, 140, 160));
    p.drawPath(nose);
    p.setPen(QPen(ink, 3.2, Qt::SolidLine, Qt::RoundCap));
    p.setBrush(Qt::NoBrush);
    QPainterPath mouth;
    mouth.moveTo(256, 257);
    mouth.quadTo(256, 270, 244, 270);
    mouth.quadTo(234, 270, 234, 262);
    mouth.moveTo(256, 257);
    mouth.quadTo(256, 270, 268, 270);
    mouth.quadTo(278, 270, 278, 262);
    p.drawPath(mouth);

    // Whiskers.
    p.setPen(QPen(QColor(200, 186, 184), 2.4, Qt::SolidLine, Qt::RoundCap));
    for (int i = -1; i <= 1; ++i) {
        p.drawLine(QPointF(170, 250 + i * 10), QPointF(110, 244 + i * 18));
        p.drawLine(QPointF(342, 250 + i * 10), QPointF(402, 244 + i * 18));
    }

    // The mug, held between the front paws, with a little P and steam.
    QPainterPath mug;
    mug.addRoundedRect(QRectF(214, 352, 84, 92), 16, 16);
    p.setPen(QPen(red.darker(125), 3));
    QLinearGradient mugGrad(0, 352, 0, 444);
    mugGrad.setColorAt(0, QColor(232, 92, 80));
    mugGrad.setColorAt(1, QColor(178, 40, 30));
    p.setBrush(mugGrad);
    p.drawPath(mug);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(178, 40, 30), 9, Qt::SolidLine, Qt::RoundCap));
    p.drawArc(QRectF(284, 372, 40, 46), -80 * 16, 160 * 16);
    p.setPen(QPen(QColor(255, 255, 255, 230), 5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    QPainterPath letter;
    letter.moveTo(246, 422);
    letter.lineTo(246, 378);
    letter.lineTo(262, 378);
    letter.quadTo(276, 378, 276, 391);
    letter.quadTo(276, 404, 262, 404);
    letter.lineTo(246, 404);
    p.drawPath(letter);
    p.setPen(QPen(QColor(210, 74, 62, 150), 4, Qt::SolidLine, Qt::RoundCap));
    for (int i = 0; i < 3; ++i) {
        QPainterPath steam;
        const qreal x = 234 + i * 22;
        steam.moveTo(x, 340);
        steam.cubicTo(x - 10, 325, x + 10, 315, x, 300);
        p.drawPath(steam);
    }

    // Paws over the mug.
    p.setPen(outline);
    p.setBrush(fur);
    p.drawEllipse(QPointF(214, 410), 26, 20);
    p.drawEllipse(QPointF(298, 410), 26, 20);
    p.setPen(QPen(line, 2.5, Qt::SolidLine, Qt::RoundCap));
    for (qreal x : {206.0, 214.0, 222.0, 290.0, 298.0, 306.0})
        p.drawLine(QPointF(x, 419), QPointF(x, 426));
    p.end();

    if (!img.save(QString::fromLocal8Bit(argv[1]))) {
        std::fprintf(stderr, "cannot write %s\n", argv[1]);
        return 1;
    }
    return 0;
}
