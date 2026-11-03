#pragma once

#include <QWidget>

#include <functional>

class QVariantAnimation;

namespace packix::ui {

// Quick sideways shake of a window, like a wrong password at the macOS login (≈250 ms).
void shake(QWidget* window);
// The same for one widget inside its window (a field with a wrong value).
void shakeWidget(QWidget* widget);

// File shapes slide into a box that closes: ≈250 ms over the given screen area. Never blocks anything.
void playPackAnimation(const QRect& globalArea);

// The book with the clasp, as a widget; animateOpen() snaps the clasp open.
class BookLockWidget : public QWidget {
    Q_OBJECT
public:
    explicit BookLockWidget(int size, QWidget* parent = nullptr);
    void setOpen(bool open);
    void animateOpen(std::function<void()> done = {});

protected:
    void paintEvent(QPaintEvent*) override;

private:
    qreal m_open = 0;
    QVariantAnimation* m_anim = nullptr;
};

// Dashed rounded area with a text; glows softly while files are dragged over it.
class DropZone : public QWidget {
    Q_OBJECT
public:
    explicit DropZone(QWidget* parent = nullptr);
    void setText(const QString& text);
    // Driven by whoever handles the drag (the zone itself or its window).
    void setHot(bool hot);

signals:
    void filesDropped(const QStringList& paths);

protected:
    void paintEvent(QPaintEvent*) override;
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dragLeaveEvent(QDragLeaveEvent* e) override;
    void dropEvent(QDropEvent* e) override;

private:
    QString m_text;
    qreal m_glow = 0;
    QVariantAnimation* m_anim;
};

} // namespace packix::ui
