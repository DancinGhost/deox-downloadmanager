// SPDX-License-Identifier: MIT
#pragma once

#include <QMainWindow>
#include <QStringList>

#include <vector>

class QAction;
class QLabel;
class QStackedWidget;
class QTreeView;

namespace deoxdm::gui {

class DownloadModel;
class DownloadQueue;
class DownloadTask;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(DownloadQueue* queue, QWidget* parent = nullptr);

    // Opens the "Add Downloads" dialog, optionally pre-filled, and queues
    // whatever the user confirms.
    void promptForDownloads(const QStringList& urls = {});

protected:
    void closeEvent(QCloseEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    void createActions();
    void updateActions();
    void updateStatusBar();
    void updateEmptyState();
    void showContextMenu(const QPoint& position);

    [[nodiscard]] std::vector<DownloadTask*> selectedTasks() const;
    void addFromClipboardOrPrompt();
    void pasteUrls();
    void resumeSelected();
    void pauseSelected();
    void removeSelected();
    void openSelectedFile();
    void openSelectedFolder();
    void copySelectedUrls();
    void showSettings();
    void showAbout();

    DownloadQueue* queue_;
    DownloadModel* model_;
    QTreeView* view_;
    QStackedWidget* stack_;
    QLabel* statusLabel_;

    QAction* addAction_{nullptr};
    QAction* resumeAction_{nullptr};
    QAction* pauseAction_{nullptr};
    QAction* removeAction_{nullptr};
    QAction* openFileAction_{nullptr};
    QAction* openFolderAction_{nullptr};
    QAction* copyUrlAction_{nullptr};
    QAction* settingsAction_{nullptr};
    QAction* aboutAction_{nullptr};
};

} // namespace deoxdm::gui
