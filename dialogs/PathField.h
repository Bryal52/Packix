#pragma once

#include <QList>
#include <QWidget>

class QLineEdit;
class QToolButton;

namespace packix::ui {

// Address line + [⋯]: the usual folders (as in Folix), connected drives, extra places and "Обзор…".
class PathField : public QWidget {
    Q_OBJECT
public:
    explicit PathField(QWidget* parent = nullptr);

    QString path() const;
    void setPath(const QString& path);
    // Extra entries at the top of the menu, e.g. "Рядом с архивом".
    void addPlace(const QString& label, const QString& path);
    QLineEdit* edit() const { return m_edit; }

signals:
    void changed();

private:
    void showMenu();

    QLineEdit* m_edit;
    QToolButton* m_more;
    QList<std::pair<QString, QString>> m_extra;
};

} // namespace packix::ui
