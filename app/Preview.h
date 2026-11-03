#pragma once

#include "Types.h"

#include <QCache>
#include <QFrame>
#include <QImage>

#include <memory>

class QLabel;

namespace packix::ui {

struct PreviewData {
    QImage image;
    QStringList lines;   // resolution, duration, or why there is no picture
};

// Card with a photo or a video frame next to the cursor — the same as in Folix. The file is
// unpacked from the archive into a temporary folder first; moving on cancels that.
class PreviewPopup : public QFrame {
    Q_OBJECT
public:
    static PreviewPopup* instance();
    static bool wants(const QString& name, bool isDir);

    void showFor(const ArchiveInfo& info, const QString& password, const Item& item, const QPoint& globalPos);
    void hidePopup();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    PreviewPopup();
    void fill(const PreviewData& d, const Item& item);
    void place();

    QLabel* m_image;
    QLabel* m_name;
    QLabel* m_info;
    QPoint m_anchor;
    quint64 m_gen = 0;
    std::shared_ptr<Progress> m_running;
    QCache<QString, PreviewData> m_cache{120};
};

} // namespace packix::ui
