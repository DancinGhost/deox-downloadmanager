// SPDX-License-Identifier: MIT
#pragma once

#include "model/app_settings.hpp"
#include "model/download_task.hpp"

#include <QObject>
#include <QString>

#include <memory>
#include <vector>

namespace deoxdm::gui {

// The list of downloads and the policy for running them: at most
// `maxConcurrent` at a time, the rest wait their turn. The list is persisted
// to a session file so that it survives restarts.
class DownloadQueue : public QObject {
    Q_OBJECT

public:
    // `sessionFile` may be empty to disable persistence.
    DownloadQueue(AppSettings settings, QString sessionFile, QObject* parent = nullptr);
    ~DownloadQueue() override; // stops all transfers (they stay resumable) and saves the session

    [[nodiscard]] int count() const { return static_cast<int>(tasks_.size()); }
    [[nodiscard]] DownloadTask* at(int row) const { return tasks_[static_cast<size_t>(row)].get(); }
    [[nodiscard]] int indexOf(const DownloadTask* task) const;

    DownloadTask* add(const TaskSpec& spec);
    void resume(DownloadTask* task); // Paused or Failed: queue it again
    void pause(DownloadTask* task);  // Running or Queued: stop it, keep it in the list
    void remove(DownloadTask* task, bool deleteFiles);

    [[nodiscard]] const AppSettings& settings() const { return settings_; }
    void setSettings(const AppSettings& settings);

    [[nodiscard]] int runningCount() const;
    [[nodiscard]] int queuedCount() const;
    [[nodiscard]] double totalSpeed() const;

    // Restores the list from the session file. Downloads that were running or
    // waiting when the application closed are queued again.
    void load();
    void save() const;

signals:
    void taskAboutToBeAdded(int row);
    void taskAdded(int row);
    void taskAboutToBeRemoved(int row);
    void taskRemoved(int row);
    void taskChanged(int row);
    void taskCompleted(deoxdm::gui::DownloadTask* task);
    void taskFailed(deoxdm::gui::DownloadTask* task);

private:
    DownloadTask* insert(const TaskSpec& spec);
    void schedule();
    void onTaskFinished(DownloadTask* task);

    AppSettings settings_;
    QString sessionFile_;
    std::vector<std::unique_ptr<DownloadTask>> tasks_;

    // Removed while still running: kept alive until their worker has stopped.
    struct Leaving {
        std::unique_ptr<DownloadTask> task;
        bool deleteFiles;
    };
    std::vector<Leaving> leaving_;
};

} // namespace deoxdm::gui
