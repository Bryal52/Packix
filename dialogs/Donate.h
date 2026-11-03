#pragma once

#include <QToolButton>

namespace packix::ui {

// Coffee cup in the corner of a window; opens the donate window.
class DonateButton : public QToolButton {
    Q_OBJECT
public:
    explicit DonateButton(QWidget* parent = nullptr);
};

// The donate window: the kitten, a note that donating is optional, and the cards from
// shared/donate/donate.json with a copy button each.
void showDonateWindow(QWidget* parent = nullptr);

} // namespace packix::ui
