// SPDX-License-Identifier: MIT
#include "model/download_queue.hpp"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <algorithm>

namespace deoxdm::gui {

DownloadQueue::DownloadQueue(AppSettings settings, QString sessionFile, QObject* parent)
    : QObject(parent), settings_(std::move(settings)), sessionFile_(std::move(sessionFile)) {}

DownloadQueue::~DownloadQueue() {
    // Save first: a transfer that is running now should be picked up again on
    // the next start.
    save();

    // Signal every worker before waiting for any of them, so that they all
    // wind down at the same time.
    for (const auto& task : tasks_) task->requestStop();
    for (const auto& leaving : leaving_) leaving.task->requestStop();

    for (const auto& leaving : leaving_) {
        leaving.task->disconnect(this);
        leaving.task->waitForStop();
        if (leaving.deleteFiles) {
            leaving.task->deleteFiles();
        }
    }
    leaving_.clear();

    for (const auto& task : tasks_) {
        task->disconnect(this);
        task->waitForStop();
    }
    tasks_.clear();
}

int DownloadQueue::indexOf(const DownloadTask* task) const {
    const auto it = std::ranges::find_if(tasks_, [task](const auto& owned) { return owned.get() == task; });
    return it == tasks_.end() ? -1 : static_cast<int>(it - tasks_.begin());
}

DownloadTask* DownloadQueue::insert(const TaskSpec& spec) {
    const int row = count();
    emit taskAboutToBeAdded(row);

    tasks_.push_back(std::make_unique<DownloadTask>(spec));
    DownloadTask* task = tasks_.back().get();

    connect(task, &DownloadTask::changed, this, [this, task] {
        if (const int index = indexOf(task); index >= 0) {
            emit taskChanged(index);
        }
    });
    connect(task, &DownloadTask::finished, this, [this, task] { onTaskFinished(task); });

    emit taskAdded(row);
    return task;
}

DownloadTask* DownloadQueue::add(const TaskSpec& spec) {
    DownloadTask* task = insert(spec);
    schedule();
    save();
    return task;
}

void DownloadQueue::resume(DownloadTask* task) {
    if (task->isActive()) {
        return; // still winding down from a pause; it can be resumed once it has stopped
    }
    if (task->status() == TaskStatus::Paused || task->status() == TaskStatus::Failed) {
        task->setStatus(TaskStatus::Queued);
        schedule();
        save();
    }
}

void DownloadQueue::pause(DownloadTask* task) {
    if (task->status() == TaskStatus::Running) {
        task->pause(); // turns Paused when its worker has stopped
    } else if (task->status() == TaskStatus::Queued) {
        task->setStatus(TaskStatus::Paused);
        save();
    }
}

void DownloadQueue::remove(DownloadTask* task, bool deleteFiles) {
    const int row = indexOf(task);
    if (row < 0) {
        return;
    }

    emit taskAboutToBeRemoved(row);
    std::unique_ptr<DownloadTask> owned = std::move(tasks_[static_cast<size_t>(row)]);
    tasks_.erase(tasks_.begin() + row);
    emit taskRemoved(row);

    if (owned->isActive()) {
        // The worker may still be writing. Let it stop in the background; the
        // files are dealt with in onTaskFinished().
        owned->requestStop();
        leaving_.push_back(Leaving{std::move(owned), deleteFiles});
    } else {
        if (deleteFiles) {
            owned->deleteFiles();
        }
        owned.reset();
    }

    schedule();
    save();
}

void DownloadQueue::setSettings(const AppSettings& settings) {
    settings_ = settings;
    schedule(); // the limit on simultaneous downloads may have been raised
}

int DownloadQueue::runningCount() const {
    return static_cast<int>(std::ranges::count_if(tasks_, [](const auto& task) { return task->isActive(); }));
}

int DownloadQueue::queuedCount() const {
    return static_cast<int>(std::ranges::count_if(tasks_, [](const auto& task) {
        return task->status() == TaskStatus::Queued && !task->isActive();
    }));
}

double DownloadQueue::totalSpeed() const {
    double total = 0.0;
    for (const auto& task : tasks_) total += task->speed();
    return total;
}

void DownloadQueue::schedule() {
    int running = runningCount();
    for (const auto& task : tasks_) {
        if (running >= settings_.maxConcurrent) {
            break;
        }
        if (task->status() == TaskStatus::Queued && !task->isActive()) {
            QDir().mkpath(task->spec().directory);
            task->start(settings_.baseConfig());
            ++running;
        }
    }
}

void DownloadQueue::onTaskFinished(DownloadTask* task) {
    // A task that was removed while running has now stopped for good.
    const auto leaving = std::ranges::find_if(leaving_, [task](const Leaving& entry) { return entry.task.get() == task; });
    if (leaving != leaving_.end()) {
        if (leaving->deleteFiles) {
            task->deleteFiles();
        }
        leaving->task.release()->deleteLater(); // we are inside one of its signals
        leaving_.erase(leaving);
        return;
    }

    if (task->status() == TaskStatus::Completed) {
        emit taskCompleted(task);
    } else if (task->status() == TaskStatus::Failed) {
        emit taskFailed(task);
    }

    schedule();
    save();
}

void DownloadQueue::load() {
    if (sessionFile_.isEmpty()) {
        return;
    }
    QFile file(sessionFile_);
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }

    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    for (const QJsonValue& value : root.value("downloads").toArray()) {
        const QJsonObject entry = value.toObject();

        TaskSpec spec;
        spec.url = entry.value("url").toString();
        spec.directory = entry.value("directory").toString(settings_.downloadDirectory);
        spec.connections = std::clamp(entry.value("connections").toInt(settings_.connections), 1, static_cast<int>(core::kMaxConnections));
        spec.sha256 = entry.value("sha256").toString();
        if (spec.url.isEmpty()) {
            continue;
        }

        TaskStatus status = taskStatusFromKey(entry.value("status").toString()).value_or(TaskStatus::Paused);
        if (status == TaskStatus::Running) {
            status = TaskStatus::Queued; // it was interrupted by the application closing
        }

        // JSON numbers are doubles; sizes are stored as strings to stay exact.
        bool totalKnown = false;
        const quint64 total = entry.value("totalBytes").toString().toULongLong(&totalKnown);
        const quint64 downloaded = entry.value("downloadedBytes").toString().toULongLong();
        const QString outputPath = entry.value("outputPath").toString();

        // A finished download whose file is gone is still listed, but an
        // unfinished one cannot continue from a file that no longer exists.
        const bool fileExists = !outputPath.isEmpty() && QFileInfo::exists(outputPath);

        DownloadTask* task = insert(spec);
        task->restore(outputPath,
                      totalKnown ? std::optional<quint64>(total) : std::nullopt,
                      (status == TaskStatus::Completed || fileExists) ? downloaded : 0,
                      entry.value("resumable").toBool(true),
                      entry.value("error").toString());
        task->setStatus(status);
    }

    schedule();
}

void DownloadQueue::save() const {
    if (sessionFile_.isEmpty()) {
        return;
    }

    QJsonArray downloads;
    for (const auto& task : tasks_) {
        QJsonObject entry;
        entry["url"] = task->spec().url;
        entry["directory"] = task->spec().directory;
        entry["connections"] = task->spec().connections;
        if (!task->spec().sha256.isEmpty()) entry["sha256"] = task->spec().sha256;
        // A download that is in the middle of pausing counts as paused.
        entry["status"] = taskStatusKey(task->isPausing() ? TaskStatus::Paused : task->status());
        if (!task->outputPath().isEmpty()) entry["outputPath"] = task->outputPath();
        if (task->totalBytes()) entry["totalBytes"] = QString::number(*task->totalBytes());
        entry["downloadedBytes"] = QString::number(task->downloadedBytes());
        entry["resumable"] = task->isResumable();
        if (!task->errorText().isEmpty()) entry["error"] = task->errorText();
        downloads.append(entry);
    }

    QJsonObject root;
    root["version"] = 1;
    root["downloads"] = downloads;

    QDir().mkpath(QFileInfo(sessionFile_).absolutePath());
    QSaveFile file(sessionFile_); // written to a temporary file, then renamed into place
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        file.commit();
    }
}

} // namespace deoxdm::gui
