// SPDX-License-Identifier: MIT
#include "views/progress_delegate.hpp"

#include "model/download_model.hpp"
#include "model/download_task.hpp"

#include <QApplication>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

namespace deoxdm::gui {

namespace {

// Fill colours per state, chosen to read well on light and dark themes alike.
QColor fillColor(TaskStatus status, const QPalette& palette) {
    switch (status) {
        case TaskStatus::Running:   return palette.color(QPalette::Highlight);
        case TaskStatus::Completed: return QColor(0x2E, 0x9E, 0x5B);
        case TaskStatus::Failed:    return QColor(0xD0, 0x45, 0x45);
        case TaskStatus::Queued:
        case TaskStatus::Paused:    return QColor(0x8A, 0x8F, 0x98);
    }
    return palette.color(QPalette::Highlight);
}

} // namespace

QSize ProgressDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const {
    QSize size = QStyledItemDelegate::sizeHint(option, index);
    size.setHeight(std::max(size.height(), option.fontMetrics.height() + 14));
    return size;
}

void ProgressDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const {
    // Let the style draw the row background (selection, hover, alternating rows).
    QStyleOptionViewItem background(option);
    initStyleOption(&background, index);
    background.text.clear();
    const QWidget* widget = option.widget;
    QStyle* style = widget ? widget->style() : QApplication::style();
    style->drawControl(QStyle::CE_ItemViewItem, &background, painter, widget);

    const auto* model = qobject_cast<const DownloadModel*>(index.model());
    const DownloadTask* task = model ? model->taskAt(index) : nullptr;
    if (!task) {
        return;
    }

    // Layout: [ bar .................... ] 100.0%
    const QRectF cell = QRectF(option.rect).adjusted(6.0, 5.0, -6.0, -5.0);
    const double labelWidth = option.fontMetrics.horizontalAdvance(QStringLiteral("100.0%")) + 8.0;
    QRectF bar = cell;
    bar.setRight(cell.right() - labelWidth);
    if (bar.width() < 20.0 || bar.height() < 6.0) {
        return;
    }

    // On a selected row the background is the highlight colour, so the bar
    // has to be drawn in the colour meant for content on top of it.
    const QPalette& palette = option.palette;
    const bool selected = option.state.testFlag(QStyle::State_Selected);
    const QPalette::ColorGroup group = option.state.testFlag(QStyle::State_Active) ? QPalette::Active : QPalette::Inactive;
    const QColor text = palette.color(group, selected ? QPalette::HighlightedText : QPalette::Text);
    const QColor fill = (selected && task->status() == TaskStatus::Running) ? text : fillColor(task->status(), palette);
    QColor track = text;
    track.setAlphaF(selected ? 0.25f : 0.12f);

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    QPainterPath outline;
    outline.addRoundedRect(bar, 4.0, 4.0);
    painter->fillPath(outline, track);

    const std::optional<quint64> total = task->totalBytes();
    const auto& chunks = task->chunks();
    const bool sizeKnown = total && *total > 0;
    const bool segmented = sizeKnown && chunks.size() > 1 && task->status() != TaskStatus::Completed;

    painter->setClipPath(outline);
    if (segmented) {
        // Every chunk owns the share of the bar that it owns in the file and
        // fills up from its own start.
        const double scale = bar.width() / static_cast<double>(*total);
        for (const auto& chunk : chunks) {
            const double left = bar.left() + static_cast<double>(chunk.range.start_byte) * scale;
            const double width = static_cast<double>(std::min<quint64>(chunk.bytes_downloaded, chunk.range.length())) * scale;
            if (width > 0.0) {
                painter->fillRect(QRectF(left, bar.top(), width, bar.height()), fill);
            }
        }
        // Thin separators between the segments.
        painter->setPen(QPen(palette.color(group, selected ? QPalette::Highlight : QPalette::Base), 1.0));
        for (size_t i = 1; i < chunks.size(); ++i) {
            const double x = std::round(bar.left() + static_cast<double>(chunks[i].range.start_byte) * scale) + 0.5;
            painter->drawLine(QPointF(x, bar.top()), QPointF(x, bar.bottom()));
        }
    } else if (!sizeKnown && task->status() == TaskStatus::Running) {
        // No total to measure against: tint the whole bar to show activity.
        QColor busy = fill;
        busy.setAlphaF(0.4f);
        painter->fillRect(bar, busy);
    } else if (const double ratio = task->progressRatio(); ratio > 0.0) {
        painter->fillRect(QRectF(bar.left(), bar.top(), bar.width() * ratio, bar.height()), fill);
    }
    painter->setClipping(false);

    QString label;
    if (task->status() == TaskStatus::Completed) {
        label = QStringLiteral("100%");
    } else if (sizeKnown && (task->downloadedBytes() > 0 || task->status() == TaskStatus::Running)) {
        label = QStringLiteral("%1%").arg(task->progressRatio() * 100.0, 0, 'f', 1);
    }
    if (!label.isEmpty()) {
        painter->setPen(text);
        painter->drawText(QRectF(bar.right(), cell.top(), labelWidth, cell.height()), Qt::AlignRight | Qt::AlignVCenter, label);
    }

    painter->restore();
}

} // namespace deoxdm::gui
