#pragma once

#include "Types.h"

#include <QList>
#include <QObject>
#include <QPointer>
#include <QWidget>

#include <functional>
#include <memory>

class QLabel;
class QPushButton;

namespace packix::ui {

class Thermometer;

struct JobSpec {
    QString title;           // "Архивирую «Фото.zip»"
    QString archiveName;     // for error messages
    bool packAnimation = false;
    QString notifyTitle;     // shown if the job ran longer than "notifyAfterSec" (macOS)
    // Runs on a worker thread. resultPath: the file to reveal from the notification.
    std::function<Status(Progress& progress, QString& resultPath)> work;
    // Back on the GUI thread, after the window is gone. Errors were already shown.
    std::function<void(const Status& status, const QString& resultPath)> done;
};

class Job;

// Every running operation, each with its own progress window. Any number at once.
class Jobs : public QObject {
    Q_OBJECT
public:
    static Jobs& instance();
    void start(JobSpec spec, QWidget* anchor = nullptr);
    QList<Job*> running() const { return m_jobs; }

signals:
    void changed();

private:
    friend class Job;
    QList<Job*> m_jobs;
};

class Job : public QObject {
    Q_OBJECT
public:
    QString title() const { return m_spec.title; }
    int percent() const;
    void showWindow();

private:
    friend class Jobs;
    Job(JobSpec spec, QWidget* anchor);
    void refresh();
    void finished(const Status& st);
    void close(const Status& st);

    JobSpec m_spec;
    QPointer<QWidget> m_anchor;
    std::shared_ptr<Progress> m_progress = std::make_shared<Progress>();
    std::shared_ptr<QString> m_result = std::make_shared<QString>();
    QWidget* m_window = nullptr;
    Thermometer* m_bar = nullptr;
    QLabel* m_current = nullptr;
    QLabel* m_detail = nullptr;
    QPushButton* m_cancel = nullptr;
    qint64 m_started = 0;
    qint64 m_lastBytes = 0;
    qint64 m_lastMs = 0;
    double m_speed = 0;
};

} // namespace packix::ui
