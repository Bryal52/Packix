#include "Jobs.h"

#include "Dialogs.h"
#include "Effects.h"
#include "Lang.h"
#include "Log.h"
#include "Platform.h"
#include "Theme.h"
#include "Thermometer.h"

#include <QDateTime>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QScreen>
#include <QTimer>
#include <QVBoxLayout>
#include <QtConcurrent>

namespace packix::ui {

namespace {

qint64 nowMs()
{
    return QDateTime::currentMSecsSinceEpoch();
}

} // namespace

Jobs& Jobs::instance()
{
    static Jobs* j = new Jobs;
    return *j;
}

void Jobs::start(JobSpec spec, QWidget* anchor)
{
    auto* job = new Job(std::move(spec), anchor);
    m_jobs.append(job);
    emit changed();
}

Job::Job(JobSpec spec, QWidget* anchor) : QObject(&Jobs::instance()), m_spec(std::move(spec)), m_anchor(anchor)
{
    m_started = nowMs();
    Log::info(QStringLiteral("job"), m_spec.title);

    auto* watcher = new QFutureWatcher<Status>(this);
    connect(watcher, &QFutureWatcher<Status>::finished, this, [this, watcher] { finished(watcher->result()); });
    auto progress = m_progress;
    auto result = m_result;
    auto work = m_spec.work;
    watcher->setFuture(QtConcurrent::run([progress, result, work] {
        try {
            return work(*progress, *result);
        } catch (const std::exception& e) {
            return Status::fail(Error::Failed, QString::fromUtf8(e.what()));
        }
    }));

    // A window only for jobs that take a moment; quick ones finish unseen.
    QTimer::singleShot(350, this, [this] {
        if (m_progress && !m_window && Jobs::instance().m_jobs.contains(this))
            showWindow();
    });
    auto* timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &Job::refresh);
    timer->start(120);
}

int Job::percent() const
{
    return m_progress->snapshot().percent;
}

void Job::showWindow()
{
    if (m_window) {
        m_window->show();
        m_window->raise();
        m_window->activateWindow();
        return;
    }
    m_window = new QWidget(nullptr, Qt::Window | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
    m_window->setAttribute(Qt::WA_DeleteOnClose, false);
    m_window->setWindowTitle(m_spec.title);
    Theme::get().apply(m_window);

    auto* heading = new QLabel(m_spec.title, m_window);
    QFont f = heading->font();
    f.setBold(true);
    f.setPointSizeF(f.pointSizeF() + 1);
    heading->setFont(f);
    m_current = new QLabel(m_window);
    m_current->setTextFormat(Qt::PlainText);
    m_current->setMinimumWidth(420);
    m_bar = new Thermometer(m_window);
    m_detail = new QLabel(m_window);
    m_detail->setForegroundRole(QPalette::PlaceholderText);
    m_cancel = new QPushButton(L("common.cancel"), m_window);
    connect(m_cancel, &QPushButton::clicked, this, [this] {
        m_progress->cancel = true;
        m_cancel->setEnabled(false);
        m_cancel->setText(L("job.cancelling"));
    });

    auto* buttons = new QHBoxLayout;
    buttons->addStretch();
    buttons->addWidget(m_cancel);
    auto* layout = new QVBoxLayout(m_window);
    const int m = Theme::get().metric("dialogMargin", 20);
    layout->setContentsMargins(m, m - 2, m, m - 4);
    layout->setSpacing(8);
    layout->addWidget(heading);
    layout->addWidget(m_current);
    layout->addWidget(m_bar);
    layout->addWidget(m_detail);
    layout->addLayout(buttons);

    if (m_anchor && m_anchor->isVisible()) {
        m_window->adjustSize();
        m_window->move(m_anchor->window()->geometry().center() - m_window->rect().center());
    }
    refresh();
    m_window->show();
    m_window->raise();
    m_window->activateWindow();
}

void Job::refresh()
{
    if (!m_window || !m_bar)
        return;
    const auto s = m_progress->snapshot();
    m_bar->setValue(s.percent < 0 ? -1.0 : s.percent / 100.0);
    m_current->setText(m_current->fontMetrics().elidedText(s.current, Qt::ElideMiddle, m_current->width()));
    if (s.totalBytes <= 0 || s.percent < 0) {
        m_detail->setText(L("job.preparing"));
        return;
    }
    const qint64 done = s.totalBytes * s.percent / 100;
    const qint64 now = nowMs();
    if (now - m_lastMs >= 500) {
        const double inst = double(done - m_lastBytes) * 1000.0 / double(qMax<qint64>(1, now - m_lastMs));
        m_speed = m_speed <= 0 ? inst : m_speed * 0.7 + inst * 0.3;
        m_lastBytes = done;
        m_lastMs = now;
    }
    QString text = L("job.amount", {{"done", humanSize(done)}, {"total", humanSize(s.totalBytes)}});
    if (m_speed > 1) {
        text += QStringLiteral(" · ") + L("job.speed", {{"speed", humanSize(qint64(m_speed))}});
        const qint64 left = qint64(double(s.totalBytes - done) / m_speed);
        if (left >= 60)
            text += QStringLiteral(" · ") + Ln("job.leftMin", (left + 59) / 60);
        else if (left > 0)
            text += QStringLiteral(" · ") + Ln("job.leftSec", left);
    }
    m_detail->setText(text);
}

void Job::finished(const Status& st)
{
    if (!m_window || !m_window->isVisible()) {
        close(st);
        return;
    }
    if (st.ok()) {
        m_bar->finish([this, st] { close(st); });
    } else if (st.error == Error::Cancelled) {
        m_bar->drain([this, st] { close(st); });
    } else {
        m_bar->setFailed(true);
        close(st);
    }
}

void Job::close(const Status& st)
{
    const qint64 elapsed = nowMs() - m_started;
    QRect area;
    if (m_window && m_window->isVisible())
        area = m_window->frameGeometry();
    else if (m_anchor && m_anchor->isVisible())
        area = m_anchor->window()->frameGeometry();
    else if (QScreen* screen = QGuiApplication::primaryScreen())
        area = QRect(screen->availableGeometry().center() - QPoint(100, 100), QSize(200, 200));
    if (m_window) {
        m_window->hide();
        m_window->deleteLater();
        m_window = nullptr;
        m_bar = nullptr;
    }

    if (st.ok()) {
        Log::info(QStringLiteral("job"), m_spec.title + QStringLiteral(" — ok"));
        if (m_spec.packAnimation)
            playPackAnimation(area);
#ifdef Q_OS_MACOS
        if (!m_spec.notifyTitle.isEmpty() && elapsed >= Theme::get().motion("notifyAfterSec", 10) * 1000)
            platform::notify(m_spec.notifyTitle, QFileInfo(*m_result).fileName(), *m_result);
#else
        Q_UNUSED(elapsed);
#endif
    } else {
        Dialogs::showError(m_anchor, st, m_spec.archiveName);
    }
    Jobs::instance().m_jobs.removeAll(this);
    emit Jobs::instance().changed();
    if (m_spec.done)
        m_spec.done(st, *m_result);
    deleteLater();
}

} // namespace packix::ui
