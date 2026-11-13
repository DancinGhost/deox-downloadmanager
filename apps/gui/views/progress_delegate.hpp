// SPDX-License-Identifier: MIT
#pragma once

#include <QStyledItemDelegate>

namespace deoxdm::gui {

// Paints the "Progress" column: a bar with one section per connection, so
// that the state of every segment of a download is visible at a glance.
class ProgressDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    [[nodiscard]] QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;
};

} // namespace deoxdm::gui
