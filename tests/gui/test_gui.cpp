// SPDX-License-Identifier: MIT
// Tests for the desktop application. They run without a display
// (QT_QPA_PLATFORM=offscreen) against the local mock HTTP server.
//
//   deox_gui_tests [FILTER]            run the tests whose name contains FILTER
//   deox_gui_tests --screenshot FILE [--select]
//                                      render the main window with downloads in
//                                      various states into FILE (for the README)

#include "mock_http_server.hpp"
#include "test_framework.hpp"

#include "add_dialog.hpp"
#include "download_model.hpp"
#include "download_queue.hpp"
#include "icons.hpp"
#include "main_window.hpp"

#include "deoxdm/engine/checkpoint_store.hpp"

#include <QAction>
#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QPixmap>
#include <QThread>
#include <QTreeView>

#include <functional>
#include <iostream>

using namespace deoxdm;
using namespace deoxdm::gui;
using deoxdm::test::make_payload;
using deoxdm::test::MockHttpServer;
using deoxdm::test::TempDir;

namespace {

// Runs the event loop until `condition` holds.
bool waitFor(const std::function<bool()>& condition, int timeoutMs = 20000) {
    QElapsedTimer timer;
    timer.start();
    while (!condition()) {
        if (timer.elapsed() > timeoutMs) {
            return false;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QThread::msleep(2);
    }
    return true;
}

AppSettings testSettings(const TempDir& dir) {
    AppSettings settings;
    settings.downloadDirectory = QString::fromStdString(dir.path().string());
    settings.connections = 4;
    settings.maxConcurrent = 3;
    settings.retries = 1;
    settings.timeoutSeconds = 10;
    return settings;
}

TaskSpec specFor(const MockHttpServer& server, const TempDir& dir, std::string_view path = "/file.bin") {
    return TaskSpec{
        .url = QString::fromStdString(server.url(path)),
        .directory = QString::fromStdString(dir.path().string()),
        .connections = 4,
        .sha256 = {}
    };
}

QByteArray readFile(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

QByteArray asBytes(const std::vector<uint8_t>& payload) {
    return QByteArray(reinterpret_cast<const char*>(payload.data()), static_cast<qsizetype>(payload.size()));
}

// Large enough to be split into several segments (the engine does not split below 1 MiB).
constexpr size_t kSegmentedSize = 5 * 1024 * 1024;

} // namespace

DEOXDM_TEST(gui_queue_downloads_a_file) {
    const auto payload = make_payload(kSegmentedSize);
    MockHttpServer server({.payload = payload});
    const TempDir dir;

    DownloadQueue queue(testSettings(dir), {});
    int completedSignals = 0;
    QObject::connect(&queue, &DownloadQueue::taskCompleted, [&] { ++completedSignals; });

    DownloadTask* task = queue.add(specFor(server, dir));
    ASSERT_EQ(queue.count(), 1);
    ASSERT_EQ(task->status(), TaskStatus::Running);

    ASSERT_TRUE(waitFor([&] { return task->status() == TaskStatus::Completed; }));
    ASSERT_EQ(completedSignals, 1);
    ASSERT_FALSE(task->isActive());
    ASSERT_EQ(task->displayName(), QStringLiteral("file.bin"));
    ASSERT_EQ(task->totalBytes(), kSegmentedSize);
    ASSERT_EQ(task->downloadedBytes(), kSegmentedSize);
    ASSERT_EQ(task->progressRatio(), 1.0);
    ASSERT_EQ(readFile(task->outputPath()), asBytes(payload));
    ASSERT_EQ(queue.runningCount(), 0);
}

DEOXDM_TEST(gui_pause_and_resume) {
    const auto payload = make_payload(kSegmentedSize);
    MockHttpServer server({.payload = payload, .throttle_bytes = 32 * 1024, .throttle_pause = std::chrono::milliseconds(10)});
    const TempDir dir;

    DownloadQueue queue(testSettings(dir), {});
    DownloadTask* task = queue.add(specFor(server, dir));

    ASSERT_TRUE(waitFor([&] { return task->downloadedBytes() > kSegmentedSize / 5; }));
    ASSERT_TRUE(task->chunks().size() > 1); // per-segment progress is available for the progress bar

    queue.pause(task);
    ASSERT_TRUE(task->isPausing());
    ASSERT_TRUE(waitFor([&] { return task->status() == TaskStatus::Paused; }));
    ASSERT_FALSE(task->isActive());
    const quint64 atPause = task->downloadedBytes();
    ASSERT_TRUE(atPause > 0 && atPause < kSegmentedSize);
    ASSERT_TRUE(engine::CheckpointStore::exists(task->outputPath().toStdString()));

    server.stop_throttling();
    queue.resume(task);
    ASSERT_EQ(task->status(), TaskStatus::Running);
    ASSERT_TRUE(waitFor([&] { return task->status() == TaskStatus::Completed; }));
    ASSERT_EQ(readFile(task->outputPath()), asBytes(payload));
    ASSERT_FALSE(engine::CheckpointStore::exists(task->outputPath().toStdString()));
}

DEOXDM_TEST(gui_concurrency_limit) {
    const auto payload = make_payload(512 * 1024);
    MockHttpServer server({.payload = payload, .throttle_bytes = 8 * 1024, .throttle_pause = std::chrono::milliseconds(10)});
    const TempDir dir;

    AppSettings settings = testSettings(dir);
    settings.maxConcurrent = 1;
    DownloadQueue queue(settings, {});

    DownloadTask* first = queue.add(specFor(server, dir, "/a.bin"));
    DownloadTask* second = queue.add(specFor(server, dir, "/b.bin"));
    DownloadTask* third = queue.add(specFor(server, dir, "/c.bin"));

    ASSERT_EQ(first->status(), TaskStatus::Running);
    ASSERT_EQ(second->status(), TaskStatus::Queued);
    ASSERT_EQ(third->status(), TaskStatus::Queued);
    ASSERT_EQ(queue.runningCount(), 1);
    ASSERT_EQ(queue.queuedCount(), 2);

    // Pausing a waiting download takes it out of the line.
    queue.pause(third);
    ASSERT_EQ(third->status(), TaskStatus::Paused);

    // Raising the limit starts the next one right away.
    settings.maxConcurrent = 2;
    queue.setSettings(settings);
    ASSERT_EQ(second->status(), TaskStatus::Running);
    ASSERT_EQ(queue.runningCount(), 2);

    server.stop_throttling();
    ASSERT_TRUE(waitFor([&] {
        return first->status() == TaskStatus::Completed && second->status() == TaskStatus::Completed;
    }));
    ASSERT_EQ(third->status(), TaskStatus::Paused); // not started behind the user's back
    ASSERT_EQ(readFile(first->outputPath()), asBytes(payload));
    ASSERT_EQ(readFile(second->outputPath()), asBytes(payload));
}

DEOXDM_TEST(gui_failed_download_is_reported) {
    MockHttpServer server({.payload = make_payload(1024)});
    const TempDir dir;

    DownloadQueue queue(testSettings(dir), {});
    int failedSignals = 0;
    QObject::connect(&queue, &DownloadQueue::taskFailed, [&] { ++failedSignals; });

    DownloadTask* task = queue.add(specFor(server, dir, "/missing"));
    ASSERT_TRUE(waitFor([&] { return task->status() == TaskStatus::Failed; }));
    ASSERT_EQ(failedSignals, 1);
    ASSERT_TRUE(task->statusText().contains(QStringLiteral("404")));

    // A failed download can be tried again.
    queue.resume(task);
    ASSERT_EQ(task->status(), TaskStatus::Running);
    ASSERT_TRUE(waitFor([&] { return task->status() == TaskStatus::Failed; }));
    ASSERT_EQ(failedSignals, 2);
}

DEOXDM_TEST(gui_remove_running_download_deletes_its_files) {
    const auto payload = make_payload(kSegmentedSize);
    MockHttpServer server({.payload = payload, .throttle_bytes = 16 * 1024, .throttle_pause = std::chrono::milliseconds(10)});
    const TempDir dir;

    DownloadQueue queue(testSettings(dir), {});
    DownloadTask* task = queue.add(specFor(server, dir));
    ASSERT_TRUE(waitFor([&] { return task->downloadedBytes() > 0; }));

    const QString path = task->outputPath();
    ASSERT_TRUE(QFileInfo::exists(path));

    queue.remove(task, true);
    ASSERT_EQ(queue.count(), 0); // gone from the list immediately

    // The files disappear as soon as the transfer has stopped in the background.
    ASSERT_TRUE(waitFor([&] {
        return !QFileInfo::exists(path) && !engine::CheckpointStore::exists(path.toStdString());
    }));
}

DEOXDM_TEST(gui_remove_keeps_files_unless_asked) {
    const auto payload = make_payload(4096);
    MockHttpServer server({.payload = payload});
    const TempDir dir;

    DownloadQueue queue(testSettings(dir), {});
    DownloadTask* task = queue.add(specFor(server, dir));
    ASSERT_TRUE(waitFor([&] { return task->status() == TaskStatus::Completed; }));
    const QString path = task->outputPath();

    queue.remove(task, false);
    ASSERT_EQ(queue.count(), 0);
    ASSERT_EQ(readFile(path), asBytes(payload));
}

DEOXDM_TEST(gui_session_survives_restart) {
    const auto payload = make_payload(kSegmentedSize);
    MockHttpServer server({.payload = payload, .throttle_bytes = 32 * 1024, .throttle_pause = std::chrono::milliseconds(10)});
    const TempDir dir;
    const QString sessionFile = QString::fromStdString((dir / "state" / "session.json").string());

    QString interruptedPath;
    {
        DownloadQueue queue(testSettings(dir), sessionFile);
        DownloadTask* interrupted = queue.add(specFor(server, dir, "/interrupted.bin"));
        DownloadTask* paused = queue.add(specFor(server, dir, "/paused.bin"));
        ASSERT_TRUE(waitFor([&] {
            return interrupted->downloadedBytes() > kSegmentedSize / 10 && paused->downloadedBytes() > 0;
        }));
        queue.pause(paused);
        ASSERT_TRUE(waitFor([&] { return paused->status() == TaskStatus::Paused; }));
        interruptedPath = interrupted->outputPath();
        // The queue is destroyed while "interrupted" is still running, as when
        // the application is closed.
    }
    ASSERT_TRUE(QFileInfo::exists(sessionFile));
    ASSERT_TRUE(engine::CheckpointStore::exists(interruptedPath.toStdString()));

    server.stop_throttling();
    DownloadQueue restored(testSettings(dir), sessionFile);
    restored.load();
    ASSERT_EQ(restored.count(), 2);

    DownloadTask* interrupted = restored.at(0);
    DownloadTask* paused = restored.at(1);
    ASSERT_EQ(interrupted->displayName(), QStringLiteral("interrupted.bin"));
    ASSERT_EQ(interrupted->status(), TaskStatus::Running); // picked up again automatically
    ASSERT_EQ(paused->status(), TaskStatus::Paused);       // stays paused
    ASSERT_TRUE(paused->downloadedBytes() > 0);
    ASSERT_EQ(paused->totalBytes(), kSegmentedSize);

    ASSERT_TRUE(waitFor([&] { return interrupted->status() == TaskStatus::Completed; }));
    ASSERT_EQ(readFile(interrupted->outputPath()), asBytes(payload));

    // The interrupted download continued; it did not start over.
    size_t resumedRequests = 0;
    for (const auto& request : server.requests()) {
        if (request.method == "GET" && request.path == "/interrupted.bin" && request.range_start && *request.range_start > 0) {
            ++resumedRequests;
        }
    }
    ASSERT_TRUE(resumedRequests > 4);
}

DEOXDM_TEST(gui_model_presents_tasks) {
    const auto payload = make_payload(2048);
    MockHttpServer server({.payload = payload});
    const TempDir dir;

    DownloadQueue queue(testSettings(dir), {});
    DownloadModel model(&queue);
    ASSERT_EQ(model.rowCount(), 0);
    ASSERT_EQ(model.columnCount(), static_cast<int>(DownloadModel::ColumnCount));
    ASSERT_EQ(model.headerData(DownloadModel::NameColumn, Qt::Horizontal).toString(), QStringLiteral("Name"));

    int changes = 0;
    QObject::connect(&model, &QAbstractItemModel::dataChanged, [&] { ++changes; });

    DownloadTask* task = queue.add(specFor(server, dir, "/report.pdf"));
    ASSERT_EQ(model.rowCount(), 1);
    ASSERT_EQ(model.taskAt(model.index(0, 0)), task);
    ASSERT_TRUE(waitFor([&] { return task->status() == TaskStatus::Completed; }));
    ASSERT_TRUE(changes > 0);

    ASSERT_EQ(model.data(model.index(0, DownloadModel::NameColumn)).toString(), QStringLiteral("report.pdf"));
    ASSERT_EQ(model.data(model.index(0, DownloadModel::SizeColumn)).toString(), QStringLiteral("2.00 KB"));
    ASSERT_EQ(model.data(model.index(0, DownloadModel::StatusColumn)).toString(), QStringLiteral("Completed"));
    ASSERT_TRUE(model.data(model.index(0, DownloadModel::SpeedColumn)).toString().isEmpty());
    ASSERT_TRUE(model.data(model.index(0, DownloadModel::NameColumn), Qt::ToolTipRole).toString().contains(task->spec().url));
    ASSERT_FALSE(model.data(model.index(5, 0)).isValid());

    queue.remove(task, true);
    ASSERT_EQ(model.rowCount(), 0);
}

DEOXDM_TEST(gui_add_dialog_validates_input) {
    const QStringList urls = AddDialog::extractUrls(
        QStringLiteral("see https://example.com/a.zip and\nhttp://example.org/b.iso (mirror) ftp://nope/x https://example.com/a.zip"));
    ASSERT_EQ(urls.size(), 2);
    ASSERT_EQ(urls[0], QStringLiteral("https://example.com/a.zip"));
    ASSERT_EQ(urls[1], QStringLiteral("http://example.org/b.iso"));
    ASSERT_TRUE(AddDialog::extractUrls(QStringLiteral("nothing useful here")).isEmpty());

    const TempDir dir;
    AddDialog dialog(testSettings(dir));
    ASSERT_TRUE(dialog.specs().empty());

    dialog.setUrls({QStringLiteral("https://example.com/a.zip"), QStringLiteral("https://example.com/a.zip"),
                    QStringLiteral("https://example.com/b.zip")});
    const auto specs = dialog.specs();
    ASSERT_EQ(specs.size(), 2u); // the duplicate is dropped
    ASSERT_EQ(specs[0].directory, QString::fromStdString(dir.path().string()));
    ASSERT_EQ(specs[0].connections, 4);

    dialog.setUrls({QStringLiteral("not a url")});
    ASSERT_TRUE(dialog.specs().empty());
}

DEOXDM_TEST(gui_main_window_reflects_selection) {
    const auto payload = make_payload(4096);
    MockHttpServer server({.payload = payload});
    const TempDir dir;

    DownloadQueue queue(testSettings(dir), {});
    MainWindow window(&queue);
    window.show();

    const auto action = [&window](const QString& text) -> QAction* {
        for (QAction* candidate : window.findChildren<QAction*>()) {
            if (candidate->text() == text) return candidate;
        }
        return nullptr;
    };
    QAction* resume = action(QStringLiteral("&Resume"));
    QAction* pause = action(QStringLiteral("&Pause"));
    QAction* open = action(QStringLiteral("&Open"));
    QAction* remove = action(QStringLiteral("Re&move"));
    ASSERT_TRUE(resume && pause && open && remove);

    // Nothing selected: nothing to act on.
    ASSERT_FALSE(resume->isEnabled());
    ASSERT_FALSE(pause->isEnabled());
    ASSERT_FALSE(open->isEnabled());
    ASSERT_FALSE(remove->isEnabled());

    DownloadTask* done = queue.add(specFor(server, dir, "/done.bin"));
    DownloadTask* broken = queue.add(specFor(server, dir, "/missing"));
    ASSERT_TRUE(waitFor([&] {
        return done->status() == TaskStatus::Completed && broken->status() == TaskStatus::Failed;
    }));

    auto* view = window.findChild<QTreeView*>();
    ASSERT_TRUE(view != nullptr);

    view->setCurrentIndex(view->model()->index(0, 0)); // the finished download
    ASSERT_TRUE(open->isEnabled());
    ASSERT_TRUE(remove->isEnabled());
    ASSERT_FALSE(resume->isEnabled());
    ASSERT_FALSE(pause->isEnabled());

    view->setCurrentIndex(view->model()->index(1, 0)); // the failed download
    ASSERT_TRUE(resume->isEnabled());
    ASSERT_FALSE(open->isEnabled());

    // The window paints without a display server.
    const QPixmap rendered = window.grab();
    ASSERT_FALSE(rendered.isNull());
    ASSERT_TRUE(rendered.width() > 400);
}

namespace {

// Renders the main window with one download in each state.
int renderScreenshot(const QString& target, bool selectRunning) {
    const TempDir dir;
    const auto megabytes = [](size_t count) { return make_payload(count * 1024 * 1024); };

    MockHttpServer fast({.payload = megabytes(24)});
    MockHttpServer steady({.payload = megabytes(96), .throttle_bytes = 96 * 1024, .throttle_pause = std::chrono::milliseconds(40)});
    MockHttpServer slow({.payload = megabytes(64), .throttle_bytes = 256 * 1024, .throttle_pause = std::chrono::milliseconds(15)});

    AppSettings settings = testSettings(dir);
    settings.maxConcurrent = 3;
    DownloadQueue queue(settings, {});
    MainWindow window(&queue);
    window.resize(1000, 380);
    window.show();

    DownloadTask* completed = queue.add(specFor(fast, dir, "/debian-13-netinst-amd64.iso"));
    DownloadTask* paused = queue.add(specFor(slow, dir, "/nightly-build-2026-11-11.tar.zst"));
    DownloadTask* running = queue.add(specFor(steady, dir, "/dataset-openimages-part-03.tar.gz"));

    if (!waitFor([&] { return completed->status() == TaskStatus::Completed; }, 60000)) return 1;
    DownloadTask* failed = queue.add(specFor(fast, dir, "/missing/backup-2019.zip"));
    if (!waitFor([&] { return failed->status() == TaskStatus::Failed; }, 60000)) return 1;

    if (!waitFor([&] { return paused->progressRatio() > 0.55; }, 60000)) return 1;
    queue.pause(paused);
    if (!waitFor([&] { return paused->status() == TaskStatus::Paused; }, 60000)) return 1;

    // Two more to show the waiting line.
    settings.maxConcurrent = 1;
    queue.setSettings(settings);
    queue.add(specFor(fast, dir, "/release-notes.pdf"));

    if (!waitFor([&] { return running->progressRatio() > 0.42 && running->speed() > 0.0; }, 120000)) return 1;
    if (auto* view = window.findChild<QTreeView*>()) {
        view->setCurrentIndex({});
        if (selectRunning) {
            view->setCurrentIndex(view->model()->index(queue.indexOf(running), 0));
        }
    }
    waitFor([] { return false; }, 300); // let the last progress update paint

    return window.grab().save(target) ? 0 : 1;
}

} // namespace

int main(int argc, char* argv[]) {
    // Keep the tests away from the user's real settings.
    const TempDir configHome;
    qputenv("XDG_CONFIG_HOME", QByteArray::fromStdString(configHome.path().string()));
    qputenv("XDG_DATA_HOME", QByteArray::fromStdString(configHome.path().string()));

    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("deoxdm-tests"));
    QApplication::setApplicationName(QStringLiteral("deox-dm-gui-tests"));
    QApplication::setWindowIcon(applicationIcon());

    const QStringList arguments = QApplication::arguments();
    if (arguments.size() >= 3 && arguments[1] == QStringLiteral("--screenshot")) {
        return renderScreenshot(arguments[2], arguments.value(3) == QStringLiteral("--select"));
    }

    const std::string filter = arguments.size() > 1 ? arguments[1].toStdString() : std::string();
    size_t run = 0;
    size_t failed = 0;

    for (const auto& test : deoxdm::test::get_registry()) {
        if (test.name.find(filter) == std::string_view::npos) {
            continue;
        }
        ++run;
        try {
            test.func();
            std::cout << "  ok    " << test.name << std::endl;
        } catch (const std::exception& e) {
            ++failed;
            std::cout << "  FAIL  " << test.name << "\n          " << e.what() << std::endl;
        }
    }

    std::cout << "\n" << (run - failed) << " of " << run << " tests passed\n";
    return (run > 0 && failed == 0) ? 0 : 1;
}
