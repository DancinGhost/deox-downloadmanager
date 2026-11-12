// SPDX-License-Identifier: MIT
#include "download_model.hpp"

#include "download_queue.hpp"

#include "deoxdm/core/formatting.hpp"

#include <chrono>

namespace deoxdm::gui {

namespace {

QString formatBytes(quint64 bytes) {
    return QString::fromStdString(core::format_bytes(bytes));
}

} // namespace

DownloadModel::DownloadModel(DownloadQueue* queue, QObject* parent)
    : QAbstractTableModel(parent), queue_(queue) {
    connect(queue_, &DownloadQueue::taskAboutToBeAdded, this, [this](int row) { beginInsertRows({}, row, row); });
    connect(queue_, &DownloadQueue::taskAdded, this, [this] { endInsertRows(); });
    connect(queue_, &DownloadQueue::taskAboutToBeRemoved, this, [this](int row) { beginRemoveRows({}, row, row); });
    connect(queue_, &DownloadQueue::taskRemoved, this, [this] { endRemoveRows(); });
    connect(queue_, &DownloadQueue::taskChanged, this, [this](int row) {
        emit dataChanged(index(row, 0), index(row, ColumnCount - 1));
    });
}

int DownloadModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : queue_->count();
}

int DownloadModel::columnCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : ColumnCount;
}

DownloadTask* DownloadModel::taskAt(const QModelIndex& index) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= queue_->count()) {
        return nullptr;
    }
    return queue_->at(index.row());
}

QVariant DownloadModel::data(const QModelIndex& index, int role) const {
    const DownloadTask* task = taskAt(index);
    if (!task) {
        return {};
    }

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
            case NameColumn:
                return task->displayName();
            case SizeColumn:
                if (task->totalBytes()) return formatBytes(*task->totalBytes());
                // Size not reported by the server: show what has arrived so far.
                return task->downloadedBytes() > 0 ? formatBytes(task->downloadedBytes()) : QString();
            case ProgressColumn:
                return {}; // painted by ProgressDelegate
            case SpeedColumn:
                return task->status() == TaskStatus::Running && task->speed() > 0.0
                    ? QString::fromStdString(core::format_speed(task->speed()))
                    : QString();
            case EtaColumn:
                return task->status() == TaskStatus::Running && task->etaSeconds() > 0
                    ? QString::fromStdString(core::format_duration(std::chrono::seconds(task->etaSeconds())))
                    : QString();
            case StatusColumn:
                return task->statusText();
            default:
                return {};
        }
    }

    if (role == Qt::ToolTipRole) {
        switch (index.column()) {
            case NameColumn:
                return task->outputPath().isEmpty()
                    ? task->spec().url
                    : QStringLiteral("%1\n%2").arg(task->outputPath(), task->spec().url);
            case StatusColumn:
                if (task->status() == TaskStatus::Paused && !task->isResumable()) {
                    return tr("This server cannot continue a partial download. Resuming starts over.");
                }
                return task->statusText();
            default:
                return {};
        }
    }

    if (role == Qt::TextAlignmentRole) {
        switch (index.column()) {
            case SizeColumn:
            case SpeedColumn:
            case EtaColumn:
                return QVariant(Qt::AlignRight | Qt::AlignVCenter);
            default:
                return QVariant(Qt::AlignLeft | Qt::AlignVCenter);
        }
    }

    return {};
}

QVariant DownloadModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return {};
    }
    switch (section) {
        case NameColumn:     return tr("Name");
        case SizeColumn:     return tr("Size");
        case ProgressColumn: return tr("Progress");
        case SpeedColumn:    return tr("Speed");
        case EtaColumn:      return tr("Time left");
        case StatusColumn:   return tr("Status");
        default:             return {};
    }
}

} // namespace deoxdm::gui
