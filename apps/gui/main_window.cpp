// SPDX-License-Identifier: MIT
#include "main_window.hpp"

#include "add_dialog.hpp"
#include "download_model.hpp"
#include "download_queue.hpp"
#include "icons.hpp"
#include "progress_delegate.hpp"
#include "settings_dialog.hpp"

#include "deoxdm/core/formatting.hpp"
#include "deoxdm/version.hpp"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDragEnterEvent>
#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QSettings>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyledItemDelegate>
#include <QToolBar>
#include <QTreeView>
#include <QUrl>

#include <algorithm>

namespace deoxdm::gui {

namespace {

// File names are shortened in the middle so that the extension stays visible.
class FileNameDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

protected:
    void initStyleOption(QStyleOptionViewItem* option, const QModelIndex& index) const override {
        QStyledItemDelegate::initStyleOption(option, index);
        option->textElideMode = Qt::ElideMiddle;
    }
};

} // namespace

MainWindow::MainWindow(DownloadQueue* queue, QWidget* parent)
    : QMainWindow(parent), queue_(queue) {
    setWindowTitle(QString::fromUtf8(PROJECT_NAME));
    setAcceptDrops(true);
    resize(980, 520);

    model_ = new DownloadModel(queue_, this);

    view_ = new QTreeView(this);
    view_->setModel(model_);
    view_->setItemDelegateForColumn(DownloadModel::NameColumn, new FileNameDelegate(view_));
    view_->setItemDelegateForColumn(DownloadModel::ProgressColumn, new ProgressDelegate(view_));
    view_->setRootIsDecorated(false);
    view_->setUniformRowHeights(true);
    view_->setAlternatingRowColors(true);
    view_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    view_->setSelectionBehavior(QAbstractItemView::SelectRows);
    view_->setContextMenuPolicy(Qt::CustomContextMenu);

    QHeaderView* header = view_->header();
    header->setStretchLastSection(true);
    header->setSectionsMovable(false);
    header->resizeSection(DownloadModel::NameColumn, 300);
    header->resizeSection(DownloadModel::SizeColumn, 90);
    header->resizeSection(DownloadModel::ProgressColumn, 220);
    header->resizeSection(DownloadModel::SpeedColumn, 100);
    header->resizeSection(DownloadModel::EtaColumn, 90);

    auto* emptyLabel = new QLabel(
        tr("<h3>No downloads yet</h3>"
           "<p>Click <b>Add</b>, paste a link with Ctrl+V,<br>or drop a link onto this window.</p>"), this);
    emptyLabel->setAlignment(Qt::AlignCenter);
    emptyLabel->setEnabled(false); // rendered in the theme's muted text colour

    stack_ = new QStackedWidget(this);
    stack_->addWidget(emptyLabel);
    stack_->addWidget(view_);
    setCentralWidget(stack_);

    statusLabel_ = new QLabel(this);
    statusBar()->addPermanentWidget(statusLabel_);

    createActions();

    connect(view_, &QTreeView::customContextMenuRequested, this, &MainWindow::showContextMenu);
    connect(view_, &QTreeView::doubleClicked, this, [this] { openSelectedFile(); });
    connect(view_->selectionModel(), &QItemSelectionModel::selectionChanged, this, &MainWindow::updateActions);

    connect(queue_, &DownloadQueue::taskChanged, this, [this] {
        updateActions();
        updateStatusBar();
    });
    connect(queue_, &DownloadQueue::taskAdded, this, [this] {
        updateEmptyState();
        updateStatusBar();
    });
    connect(queue_, &DownloadQueue::taskRemoved, this, [this] {
        updateEmptyState();
        updateActions();
        updateStatusBar();
    });
    connect(queue_, &DownloadQueue::taskCompleted, this, [this](DownloadTask* task) {
        statusBar()->showMessage(tr("Finished: %1").arg(task->displayName()), 8000);
    });
    connect(queue_, &DownloadQueue::taskFailed, this, [this](DownloadTask* task) {
        statusBar()->showMessage(tr("Failed: %1").arg(task->displayName()), 8000);
    });

    const QSettings store;
    restoreGeometry(store.value("window/geometry").toByteArray());
    header->restoreState(store.value("window/columns").toByteArray());

    updateEmptyState();
    updateActions();
    updateStatusBar();
}

void MainWindow::createActions() {
    const QPalette pal = palette();

    addAction_ = new QAction(actionIcon(Glyph::Add, pal), tr("&Add"), this);
    addAction_->setShortcut(QKeySequence::New);
    addAction_->setToolTip(tr("Add downloads (Ctrl+N)"));
    connect(addAction_, &QAction::triggered, this, &MainWindow::addFromClipboardOrPrompt);

    resumeAction_ = new QAction(actionIcon(Glyph::Resume, pal), tr("&Resume"), this);
    resumeAction_->setToolTip(tr("Resume or retry the selected downloads"));
    connect(resumeAction_, &QAction::triggered, this, &MainWindow::resumeSelected);

    pauseAction_ = new QAction(actionIcon(Glyph::Pause, pal), tr("&Pause"), this);
    pauseAction_->setToolTip(tr("Pause the selected downloads"));
    connect(pauseAction_, &QAction::triggered, this, &MainWindow::pauseSelected);

    removeAction_ = new QAction(actionIcon(Glyph::Remove, pal), tr("Re&move"), this);
    removeAction_->setShortcut(QKeySequence::Delete);
    removeAction_->setToolTip(tr("Remove the selected downloads from the list (Del)"));
    connect(removeAction_, &QAction::triggered, this, &MainWindow::removeSelected);

    openFileAction_ = new QAction(actionIcon(Glyph::OpenFile, pal), tr("&Open"), this);
    openFileAction_->setToolTip(tr("Open the downloaded file"));
    connect(openFileAction_, &QAction::triggered, this, &MainWindow::openSelectedFile);

    openFolderAction_ = new QAction(actionIcon(Glyph::OpenFolder, pal), tr("Show in &Folder"), this);
    openFolderAction_->setToolTip(tr("Open the folder that contains the download"));
    connect(openFolderAction_, &QAction::triggered, this, &MainWindow::openSelectedFolder);

    copyUrlAction_ = new QAction(tr("&Copy Address"), this);
    copyUrlAction_->setShortcut(QKeySequence::Copy);
    connect(copyUrlAction_, &QAction::triggered, this, &MainWindow::copySelectedUrls);

    settingsAction_ = new QAction(actionIcon(Glyph::Settings, pal), tr("&Settings"), this);
    settingsAction_->setToolTip(tr("Download folder, connections and limits"));
    connect(settingsAction_, &QAction::triggered, this, &MainWindow::showSettings);

    auto* pasteAction = new QAction(tr("Paste Address"), this);
    pasteAction->setShortcut(QKeySequence::Paste);
    connect(pasteAction, &QAction::triggered, this, &MainWindow::pasteUrls);

    // Shortcuts work wherever the focus is inside the window.
    addActions({pasteAction, copyUrlAction_});

    QToolBar* toolbar = addToolBar(tr("Downloads"));
    toolbar->setObjectName("mainToolbar");
    toolbar->setMovable(false);
    toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    toolbar->addAction(addAction_);
    toolbar->addSeparator();
    toolbar->addAction(resumeAction_);
    toolbar->addAction(pauseAction_);
    toolbar->addAction(removeAction_);
    toolbar->addSeparator();
    toolbar->addAction(openFileAction_);
    toolbar->addAction(openFolderAction_);

    auto* spacer = new QWidget(toolbar);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    toolbar->addWidget(spacer);
    toolbar->addAction(settingsAction_);
}

std::vector<DownloadTask*> MainWindow::selectedTasks() const {
    std::vector<DownloadTask*> tasks;
    for (const QModelIndex& index : view_->selectionModel()->selectedRows()) {
        if (DownloadTask* task = model_->taskAt(index)) {
            tasks.push_back(task);
        }
    }
    return tasks;
}

void MainWindow::updateActions() {
    const auto tasks = selectedTasks();

    const bool canResume = std::ranges::any_of(tasks, [](const DownloadTask* task) {
        return !task->isActive() && (task->status() == TaskStatus::Paused || task->status() == TaskStatus::Failed);
    });
    const bool canPause = std::ranges::any_of(tasks, [](const DownloadTask* task) {
        return (task->status() == TaskStatus::Running && !task->isPausing()) || task->status() == TaskStatus::Queued;
    });
    const bool single = tasks.size() == 1;

    resumeAction_->setEnabled(canResume);
    pauseAction_->setEnabled(canPause);
    removeAction_->setEnabled(!tasks.empty());
    copyUrlAction_->setEnabled(!tasks.empty());
    openFileAction_->setEnabled(single && tasks.front()->status() == TaskStatus::Completed &&
                                QFileInfo::exists(tasks.front()->outputPath()));
    openFolderAction_->setEnabled(single);
}

void MainWindow::updateStatusBar() {
    const int running = queue_->runningCount();
    const int queued = queue_->queuedCount();

    QStringList parts;
    if (running > 0) {
        parts << tr("%n downloading", nullptr, running);
        parts << QString::fromStdString(core::format_speed(queue_->totalSpeed()));
    }
    if (queued > 0) {
        parts << tr("%n waiting", nullptr, queued);
    }
    if (parts.isEmpty() && queue_->count() > 0) {
        parts << tr("%n download(s)", nullptr, queue_->count());
    }
    statusLabel_->setText(parts.join(QStringLiteral("  ·  ")));
}

void MainWindow::updateEmptyState() {
    stack_->setCurrentIndex(queue_->count() == 0 ? 0 : 1);
}

void MainWindow::promptForDownloads(const QStringList& urls) {
    AddDialog dialog(queue_->settings(), this);
    if (!urls.isEmpty()) {
        dialog.setUrls(urls);
    }
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    for (const TaskSpec& spec : dialog.specs()) {
        queue_->add(spec);
    }
}

void MainWindow::addFromClipboardOrPrompt() {
    // Offer what is on the clipboard: most downloads start with a copied link.
    promptForDownloads(AddDialog::extractUrls(QApplication::clipboard()->text()));
}

void MainWindow::pasteUrls() {
    const QStringList urls = AddDialog::extractUrls(QApplication::clipboard()->text());
    if (urls.isEmpty()) {
        statusBar()->showMessage(tr("The clipboard does not contain a web address"), 4000);
        return;
    }
    promptForDownloads(urls);
}

void MainWindow::resumeSelected() {
    for (DownloadTask* task : selectedTasks()) {
        queue_->resume(task);
    }
}

void MainWindow::pauseSelected() {
    for (DownloadTask* task : selectedTasks()) {
        queue_->pause(task);
    }
}

void MainWindow::removeSelected() {
    const auto tasks = selectedTasks();
    if (tasks.empty()) {
        return;
    }

    const bool anyUnfinished = std::ranges::any_of(tasks, [](const DownloadTask* task) {
        return task->status() != TaskStatus::Completed;
    });

    QMessageBox box(this);
    box.setIcon(QMessageBox::Question);
    box.setWindowTitle(tr("Remove Downloads"));
    box.setText(tasks.size() == 1
        ? tr("Remove “%1” from the list?").arg(tasks.front()->displayName())
        : tr("Remove %n downloads from the list?", nullptr, static_cast<int>(tasks.size())));
    if (anyUnfinished) {
        box.setInformativeText(tr("Unfinished downloads are stopped."));
    }

    auto* deleteFiles = new QCheckBox(tr("Also delete the files from disk"), &box);
    // A partial file is of little use once it is off the list.
    deleteFiles->setChecked(std::ranges::all_of(tasks, [](const DownloadTask* task) {
        return task->status() != TaskStatus::Completed;
    }));
    box.setCheckBox(deleteFiles);

    QAbstractButton* removeButton = box.addButton(tr("Remove"), QMessageBox::AcceptRole);
    box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(qobject_cast<QPushButton*>(removeButton));
    box.exec();
    if (box.clickedButton() != removeButton) {
        return;
    }

    for (DownloadTask* task : tasks) {
        queue_->remove(task, deleteFiles->isChecked());
    }
}

void MainWindow::openSelectedFile() {
    const auto tasks = selectedTasks();
    if (tasks.size() != 1 || tasks.front()->status() != TaskStatus::Completed) {
        return;
    }
    const QString path = tasks.front()->outputPath();
    if (!QFileInfo::exists(path)) {
        statusBar()->showMessage(tr("The file no longer exists: %1").arg(path), 6000);
        return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

void MainWindow::openSelectedFolder() {
    const auto tasks = selectedTasks();
    if (tasks.size() != 1) {
        return;
    }
    const DownloadTask* task = tasks.front();
    const QString folder = task->outputPath().isEmpty()
        ? task->spec().directory
        : QFileInfo(task->outputPath()).absolutePath();
    QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
}

void MainWindow::copySelectedUrls() {
    QStringList urls;
    for (const DownloadTask* task : selectedTasks()) {
        urls << task->spec().url;
    }
    if (!urls.isEmpty()) {
        QApplication::clipboard()->setText(urls.join('\n'));
    }
}

void MainWindow::showSettings() {
    SettingsDialog dialog(queue_->settings(), this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    AppSettings settings = dialog.settings();
    if (settings.downloadDirectory.isEmpty()) {
        settings.downloadDirectory = queue_->settings().downloadDirectory;
    }
    settings.save();
    queue_->setSettings(settings);
}

void MainWindow::showContextMenu(const QPoint& position) {
    if (!view_->indexAt(position).isValid()) {
        return;
    }
    QMenu menu(this);
    menu.addAction(resumeAction_);
    menu.addAction(pauseAction_);
    menu.addSeparator();
    menu.addAction(openFileAction_);
    menu.addAction(openFolderAction_);
    menu.addAction(copyUrlAction_);
    menu.addSeparator();
    menu.addAction(removeAction_);
    menu.exec(view_->viewport()->mapToGlobal(position));
}

void MainWindow::closeEvent(QCloseEvent* event) {
    QSettings store;
    store.setValue("window/geometry", saveGeometry());
    store.setValue("window/columns", view_->header()->saveState());
    // Running downloads are stopped by the queue when the application exits
    // and continue on the next start, so there is nothing to ask here.
    event->accept();
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
    const QMimeData* mime = event->mimeData();
    if (mime->hasUrls() || mime->hasText()) {
        event->acceptProposedAction();
    }
}

void MainWindow::dropEvent(QDropEvent* event) {
    const QMimeData* mime = event->mimeData();
    QString text = mime->text();
    for (const QUrl& url : mime->urls()) {
        text += QLatin1Char('\n') + url.toString(QUrl::FullyEncoded);
    }
    const QStringList urls = AddDialog::extractUrls(text);
    if (urls.isEmpty()) {
        return;
    }
    event->acceptProposedAction();
    promptForDownloads(urls);
}

} // namespace deoxdm::gui
