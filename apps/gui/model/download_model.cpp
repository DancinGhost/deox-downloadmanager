// SPDX-License-Identifier: MIT
#include "model/download_model.hpp"

#include "model/download_queue.hpp"

#include "deoxdm/core/formatting.hpp"
#include "deoxdm/core/url.hpp"

#include <chrono>

namespace deoxdm::gui {

namespace {

QString formatBytes(quint64 bytes) {
    return QString::fromStdString(core::format_bytes(bytes));
}

// Qt shows a tooltip as HTML when it looks like markup, and URLs and server
// messages can contain "<b>" or "<img>". Making it explicitly rich text with
// the content escaped keeps it literal.
QString plainToolTip(const QString& text) {
    return QStringLiteral("<p style='white-space:pre'>%1</p>").arg(text.toHtmlEscaped());
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

    if (role == Qt::AccessibleTextRole) {
        if (index.column() == ProgressColumn) {
            if (task->status() == TaskStatus::Completed) {
                return QStringLiteral("100%");
            }
            if (task->totalBytes() && *task->totalBytes() > 0) {
                return QStringLiteral("%1%").arg(task->progressRatio() * 100.0, 0, 'f', 1);
            }
            return task->statusText();
        }
        return data(index, Qt::DisplayRole);
    }

    if (role == Qt::ToolTipRole) {
        switch (index.column()) {
            case NameColumn: {
                const QString url = QString::fromStdString(core::redact_credentials(task->spec().url.toStdString()));
                return plainToolTip(task->outputPath().isEmpty()
                    ? url
                    : QStringLiteral("%1\n%2").arg(task->outputPath(), url));
            }
            case StatusColumn:
                if (task->status() == TaskStatus::Paused && !task->isResumable()) {
                    return tr("This server cannot continue a partial download. Resuming starts over.");
                }
                return plainToolTip(task->statusText());
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
