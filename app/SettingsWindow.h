#pragma once

#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QListWidget;
class QPushButton;

namespace packix::ui {

// Language (with the folder of language files), start at login, default archiver, saved passwords.
class SettingsWindow : public QWidget {
    Q_OBJECT
public:
    SettingsWindow();

protected:
    void showEvent(QShowEvent* e) override;

private:
    void retranslate();
    void fillLanguages();
    void fillPasswords();
    void refreshState();

    QLabel* m_langTitle;
    QComboBox* m_lang;
    QLabel* m_langPath;
    QPushButton* m_reveal;
    QPushButton* m_copy;
    QCheckBox* m_login;
    QLabel* m_defaultState;
    QPushButton* m_makeDefault;
    QLabel* m_passwordsTitle;
    QListWidget* m_passwords;
    QCheckBox* m_showPasswords;
    QPushButton* m_forget;
};

} // namespace packix::ui
