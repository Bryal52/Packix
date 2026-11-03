#pragma once

#include "Archiver.h"

#include <QDialog>

class QComboBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QPushButton;

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

private:
    void formatChanged();
    void validate();
    QString fileName() const;

    QStringList m_sources;
    QLineEdit* m_name;
    QComboBox* m_format;
    QComboBox* m_level;
    QLineEdit* m_pass;
    QLineEdit* m_again;
    QLabel* m_hint;
    QLabel* m_problem;
    PathField* m_dest;
    QPushButton* m_create;
    QFormLayout* m_form;
    Archiver::CreateRequest m_request;
};

} // namespace packix::ui
