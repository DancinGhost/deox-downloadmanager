// SPDX-License-Identifier: MIT
#pragma once

#include <QAbstractTableModel>

namespace deoxdm::gui {

class DownloadQueue;
class DownloadTask;

// Presents the download queue as a table.
class DownloadModel : public QAbstractTableModel {
    Q_OBJECT

public:
    enum Column {
        NameColumn = 0,
        SizeColumn,
        ProgressColumn,
        SpeedColumn,
        EtaColumn,
        StatusColumn,
        ColumnCount
    };

    explicit DownloadModel(DownloadQueue* queue, QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] int columnCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    [[nodiscard]] QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    [[nodiscard]] DownloadTask* taskAt(const QModelIndex& index) const;

private:
    DownloadQueue* queue_;
};

} // namespace deoxdm::gui
