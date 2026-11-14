// SPDX-License-Identifier: MIT
#include "model/download_task.hpp"

#include "deoxdm/core/formatting.hpp"
#include "deoxdm/core/url.hpp"
#include "deoxdm/engine/checkpoint_store.hpp"
#include "deoxdm/engine/download_engine.hpp"
#include "deoxdm/platform/path.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace deoxdm::gui {

QString taskStatusKey(TaskStatus status) {
    switch (status) {
        case TaskStatus::Queued:    return QStringLiteral("queued");
        case TaskStatus::Running:   return QStringLiteral("running");
        case TaskStatus::Paused:    return QStringLiteral("paused");
        case TaskStatus::Completed: return QStringLiteral("completed");
        case TaskStatus::Failed:    return QStringLiteral("failed");
    }
    return QStringLiteral("queued");
}

std::optional<TaskStatus> taskStatusFromKey(const QString& key) {
    for (const TaskStatus status : {TaskStatus::Queued, TaskStatus::Running, TaskStatus::Paused,
                                    TaskStatus::Completed, TaskStatus::Failed}) {
        if (taskStatusKey(status) == key) return status;
    }
    return std::nullopt;
}

DownloadTask::DownloadTask(TaskSpec spec, QObject* parent)
    : QObject(parent), spec_(std::move(spec)) {}

DownloadTask::~DownloadTask() {
    waitForStop();
}

void DownloadTask::setStatus(TaskStatus status) {
    if (status_ == status) return;
    status_ = status;
    emit changed();
}

void DownloadTask::start(const core::DownloadConfig& base) {
    if (active_) {
        return;
    }

    core::DownloadConfig config = base;
    config.url = spec_.url.toStdString();
    config.connections = static_cast<uint32_t>(spec_.connections);
    config.expected_sha256 = spec_.sha256.toStdString();
    if (outputPath_.isEmpty()) {
        // A trailing separator tells the engine to pick the file name itself,
        // without ever replacing an existing file.
        config.output_path = platform::path_from_utf8(spec_.directory.toStdString()) / "";
    } else {
        // Continue with the file this task already owns.
        config.output_path = platform::path_from_utf8(outputPath_.toStdString());
    }

    active_ = true;
    pauseRequested_ = false;
    errorText_.clear();
    phaseText_ = tr("Connecting");
    speed_ = 0.0;
    etaSeconds_ = 0;
    status_ = TaskStatus::Running;
    emit changed();

    // The engine's listeners run on its own threads. Each of them copies what
    // it needs and hands it to the GUI thread. A queued call whose receiver
    // has been destroyed in the meantime is dropped by Qt.
    worker_ = std::jthread([this, config = std::move(config)](std::stop_token stop) mutable {
        engine::DownloadEngine engine(std::move(config));

        engine.set_state_listener([this](core::DownloadPhase phase, std::string_view) {
            const QString text = QString::fromUtf8(core::download_phase_to_string(phase));
            QMetaObject::invokeMethod(this, [this, text] {
                phaseText_ = text;
                emit changed();
            }, Qt::QueuedConnection);
        });

        engine.set_info_listener([this](const engine::DownloadInfo& info) {
            QMetaObject::invokeMethod(this, [this, info] {
                outputPath_ = QString::fromStdString(platform::path_to_utf8(info.output_path));
                totalBytes_ = info.total_size ? std::optional<quint64>(*info.total_size) : std::nullopt;
                downloadedBytes_ = info.bytes_already_present;
                resumable_ = info.resumable;
                emit changed();
            }, Qt::QueuedConnection);
        });

        engine.set_progress_listener([this](const core::ProgressSnapshot& snapshot, std::span<const core::ChunkState> chunks) {
            QMetaObject::invokeMethod(this, [this, snapshot, copy = std::vector<core::ChunkState>(chunks.begin(), chunks.end())]() mutable {
                downloadedBytes_ = snapshot.bytes_downloaded;
                speed_ = snapshot.current_speed_bps;
                etaSeconds_ = static_cast<int>(snapshot.estimated_remaining.count());
                chunks_ = std::move(copy);
                emit changed();
            }, Qt::QueuedConnection);
        });

        const auto result = engine.run(stop);

        const bool success = result.has_value();
        const bool cancelled = !success && result.error().code == core::ErrorCode::Cancelled;
        const QString message = success ? QString() : QString::fromStdString(result.error().message);
        const quint64 total = success ? result->total_bytes : 0;

        QMetaObject::invokeMethod(this, [this, success, cancelled, message, total] {
            handleFinished(success, cancelled, message, total);
        }, Qt::QueuedConnection);
    });
}

void DownloadTask::handleFinished(bool success, bool cancelled, const QString& message, quint64 totalBytes) {
    active_ = false;
    speed_ = 0.0;
    etaSeconds_ = 0;

    if (success) {
        status_ = TaskStatus::Completed;
        totalBytes_ = totalBytes;
        downloadedBytes_ = totalBytes;
        chunks_.clear();
    } else if (cancelled || pauseRequested_) {
        status_ = TaskStatus::Paused;
        if (!resumable_) {
            // Nothing was kept: the server cannot continue a partial transfer.
            downloadedBytes_ = 0;
            chunks_.clear();
        }
    } else {
        status_ = TaskStatus::Failed;
        errorText_ = message;
        if (!resumable_) {
            downloadedBytes_ = 0;
            chunks_.clear();
        }
    }
    pauseRequested_ = false;

    emit changed();
    emit finished();
}

void DownloadTask::pause() {
    if (!active_) {
        return;
    }
    pauseRequested_ = true;
    phaseText_ = tr("Pausing");
    requestStop();
    emit changed();
}

void DownloadTask::requestStop() {
    worker_.request_stop();
}

void DownloadTask::waitForStop() {
    requestStop();
    if (worker_.joinable()) {
        worker_.join();
    }
}

QString DownloadTask::displayName() const {
    if (!outputPath_.isEmpty()) {
        return QFileInfo(outputPath_).fileName();
    }
    if (const auto url = core::parse_url(spec_.url.toStdString())) {
        return QString::fromStdString(url->filename);
    }
    return QString::fromStdString(core::redact_credentials(spec_.url.toStdString()));
}

QString DownloadTask::statusText() const {
    switch (status_) {
        case TaskStatus::Queued:
            return tr("Queued");
        case TaskStatus::Running:
            return phaseText_;
        case TaskStatus::Paused:
            return tr("Paused");
        case TaskStatus::Completed:
            return tr("Completed");
        case TaskStatus::Failed:
            return errorText_.isEmpty() ? tr("Failed") : tr("Failed: %1").arg(errorText_);
    }
    return {};
}

double DownloadTask::progressRatio() const {
    if (status_ == TaskStatus::Completed) {
        return 1.0;
    }
    if (!totalBytes_ || *totalBytes_ == 0) {
        return 0.0;
    }
    return std::min(1.0, static_cast<double>(downloadedBytes_) / static_cast<double>(*totalBytes_));
}

void DownloadTask::restore(const QString& outputPath, std::optional<quint64> totalBytes, quint64 downloadedBytes,
                           bool resumable, const QString& errorText) {
    outputPath_ = outputPath;
    totalBytes_ = totalBytes;
    downloadedBytes_ = downloadedBytes;
    resumable_ = resumable;
    errorText_ = errorText;
}

void DownloadTask::deleteFiles() const {
    if (outputPath_.isEmpty()) {
        return;
    }
    QFile::remove(outputPath_);
    engine::CheckpointStore::remove(platform::path_from_utf8(outputPath_.toStdString()));
}

} // namespace deoxdm::gui
