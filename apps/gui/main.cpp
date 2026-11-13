// SPDX-License-Identifier: MIT
//
// deox-dm-gui: Qt 6 desktop front end of Deox Download Manager.

#include "views/add_dialog.hpp"
#include "model/app_settings.hpp"
#include "model/download_queue.hpp"
#include "views/icons.hpp"
#include "views/main_window.hpp"

#include "deoxdm/version.hpp"

#include <QApplication>
#include <QCommandLineParser>
#include <QStandardPaths>
#include <QTimer>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    // Settings live in <config dir>/deox-downloadmanager/deox-dm-gui.conf and
    // the download list in <data dir>/deox-downloadmanager/deox-dm-gui/.
    QApplication::setOrganizationName(QStringLiteral("deox-downloadmanager"));
    QApplication::setApplicationName(QString::fromUtf8(deoxdm::GUI_EXECUTABLE));
    QApplication::setApplicationDisplayName(QString::fromUtf8(deoxdm::PROJECT_NAME));
    QApplication::setApplicationVersion(QString::fromUtf8(deoxdm::VERSION_STRING));
    QApplication::setDesktopFileName(QString::fromUtf8(deoxdm::APPLICATION_ID));
    QApplication::setWindowIcon(deoxdm::gui::applicationIcon());

    QCommandLineParser parser;
    parser.setApplicationDescription(QString::fromUtf8(deoxdm::PROJECT_DESCRIPTION));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("url"), QStringLiteral("Addresses to download"), QStringLiteral("[url...]"));
    parser.process(app);

    const QString sessionFile =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/session.json");

    // The queue outlives the window: closing the window ends the event loop,
    // then the queue stops the transfers and saves the list.
    deoxdm::gui::DownloadQueue queue(deoxdm::gui::AppSettings::load(), sessionFile);
    queue.load();

    deoxdm::gui::MainWindow window(&queue);
    window.show();

    const QStringList urls = deoxdm::gui::AddDialog::extractUrls(parser.positionalArguments().join(QLatin1Char('\n')));
    if (!urls.isEmpty()) {
        QTimer::singleShot(0, &window, [&window, urls] { window.promptForDownloads(urls); });
    }

    return QApplication::exec();
}
