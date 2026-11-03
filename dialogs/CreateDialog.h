#pragma once

#include "Archiver.h"

#include <QDialog>
#include <QPointF>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QToolButton;
class QVariantAnimation;

namespace packix::ui {

class PathField;

// "Архивировать":
//   Имя / Тип / Сжатие (?) / Пароль + Подтвердите (zip only) / Куда / Отмена Создать
// Always opens with zip + Обычное; nothing is remembered.
class CreateDialog : public QDialog {
    Q_OBJECT
public:
    explicit CreateDialog(const QStringList& sources, QWidget* parent = nullptr);
    Archiver::CreateRequest request() const { return m_request; }

protected:
    void accept() override;
    void keyPressEvent(QKeyEvent* e) override;
    void showEvent(QShowEvent* e) override;
    void moveEvent(QMoveEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void formatChanged();
    void showPasswordFields(bool show);
    void validate();
    void shakeProblem();
    QString fileName() const;
    bool passwordShown() const;

    QStringList m_sources;
    QLineEdit* m_name;
    QComboBox* m_format;
    QComboBox* m_level;
    QToolButton* m_help;
    QWidget* m_passwordBox;
    QLineEdit* m_pass;
    QLineEdit* m_again;
    QLabel* m_hint;
    QLabel* m_problem;
    PathField* m_dest;
    QPushButton* m_create;
    QVariantAnimation* m_fieldsAnim;
    bool m_passwordWanted = true;
    QPointF m_center;          // the window stays centred here while its height changes
    bool m_movingSelf = false;
    Archiver::CreateRequest m_request;
};

} // namespace packix::ui
