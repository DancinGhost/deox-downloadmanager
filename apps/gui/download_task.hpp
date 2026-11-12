// SPDX-License-Identifier: MIT
#pragma once

#include "deoxdm/core/progress.hpp"
#include "deoxdm/core/types.hpp"

#include <QObject>
#include <QString>

#include <optional>
#include <thread>
#include <vector>

namespace deoxdm::gui {

enum class TaskStatus {
    Queued,    // waiting for a free slot
    Running,
    Paused,
    Completed,
    Failed
};

[[nodiscard]] QString taskStatusKey(TaskStatus status);
[[nodiscard]] std::optional<TaskStatus> taskStatusFromKey(const QString& key);

// What the user asked for.
struct TaskSpec {
    QString url;
    QString directory;   // folder to save into
    int connections{4};
    QString sha256;      // optional expected checksum
};

// One entry of the download list.
//
// The object lives in the GUI thread. While running, a worker thread drives a
// DownloadEngine and posts its events back to the GUI thread, so every member
// is only ever touched from there.
class DownloadTask : public QObject {
    Q_OBJECT

public:
    explicit DownloadTask(TaskSpec spec, QObject* parent = nullptr);
    ~DownloadTask() override; // stops the transfer and waits for the worker thread

    // Starts or continues the transfer. `base` carries the application-wide
    // settings; URL, target and connection count come from the task.
    void start(const core::DownloadConfig& base);

    // Asks a running transfer to stop. The task turns Paused once the worker
    // thread has wound down (see finished()).
    void pause();

    // Non-blocking: only signals the worker thread.
    void requestStop();

    // Signals the worker thread and blocks until it has ended.
    void waitForStop();

    // True from start() until the worker thread has reported its result.
    [[nodiscard]] bool isActive() const { return active_; }

    // A pause was requested and the worker thread has not stopped yet.
    [[nodiscard]] bool isPausing() const { return active_ && pauseRequested_; }

    [[nodiscard]] const TaskSpec& spec() const { return spec_; }
    [[nodiscard]] TaskStatus status() const { return status_; }
    void setStatus(TaskStatus status);

    [[nodiscard]] QString outputPath() const { return outputPath_; } // empty until the engine has chosen it
    [[nodiscard]] QString displayName() const;
    [[nodiscard]] QString statusText() const;
    [[nodiscard]] QString errorText() const { return errorText_; }

    [[nodiscard]] std::optional<quint64> totalBytes() const { return totalBytes_; }
    [[nodiscard]] quint64 downloadedBytes() const { return downloadedBytes_; }
    [[nodiscard]] double speed() const { return status_ == TaskStatus::Running ? speed_ : 0.0; }
    [[nodiscard]] int etaSeconds() const { return etaSeconds_; }
    [[nodiscard]] double progressRatio() const;
    [[nodiscard]] const std::vector<core::ChunkState>& chunks() const { return chunks_; }
    [[nodiscard]] bool isResumable() const { return resumable_; }

    // Restores what a previous session knew about the task.
    void restore(const QString& outputPath, std::optional<quint64> totalBytes, quint64 downloadedBytes,
                 bool resumable, const QString& errorText);

    // Deletes the downloaded file and its checkpoint, if any.
    void deleteFiles() const;

signals:
    void changed();  // something visible changed
    void finished(); // the worker thread is done: completed, failed or paused

private:
    void handleFinished(bool success, bool cancelled, const QString& message, quint64 totalBytes);

    TaskSpec spec_;
    TaskStatus status_{TaskStatus::Queued};
    QString outputPath_;
    QString phaseText_;
    QString errorText_;
    std::optional<quint64> totalBytes_;
    quint64 downloadedBytes_{0};
    double speed_{0.0};
    int etaSeconds_{0};
    bool resumable_{true};
    bool active_{false};
    bool pauseRequested_{false};
    std::vector<core::ChunkState> chunks_;
    std::jthread worker_;
};

} // namespace deoxdm::gui
