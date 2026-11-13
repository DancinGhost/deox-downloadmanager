// SPDX-License-Identifier: MIT
#include "model/app_settings.hpp"

#include <QDir>
#include <QSettings>
#include <QStandardPaths>

#include <algorithm>

namespace deoxdm::gui {

namespace {

QString defaultDownloadDirectory() {
    const QString location = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    return location.isEmpty() ? QDir::homePath() : location;
}

} // namespace

AppSettings AppSettings::load() {
    const QSettings store;
    AppSettings settings;
    settings.downloadDirectory = store.value("downloadDirectory", defaultDownloadDirectory()).toString();
    settings.connections = std::clamp(store.value("connections", 4).toInt(), 1, static_cast<int>(core::kMaxConnections));
    settings.maxConcurrent = std::clamp(store.value("maxConcurrent", 3).toInt(), 1, 10);
    settings.speedLimitKib = std::max(store.value("speedLimitKib", 0).toInt(), 0);
    settings.retries = std::clamp(store.value("retries", 3).toInt(), 0, 100);
    settings.timeoutSeconds = std::clamp(store.value("timeoutSeconds", 30).toInt(), 5, 600);
    if (settings.downloadDirectory.isEmpty()) {
        settings.downloadDirectory = defaultDownloadDirectory();
    }
    return settings;
}

void AppSettings::save() const {
    QSettings store;
    store.setValue("downloadDirectory", downloadDirectory);
    store.setValue("connections", connections);
    store.setValue("maxConcurrent", maxConcurrent);
    store.setValue("speedLimitKib", speedLimitKib);
    store.setValue("retries", retries);
    store.setValue("timeoutSeconds", timeoutSeconds);
}

core::DownloadConfig AppSettings::baseConfig() const {
    core::DownloadConfig config;
    config.connections = static_cast<uint32_t>(connections);
    config.max_retries = static_cast<uint32_t>(retries);
    config.timeout_seconds = static_cast<uint32_t>(timeoutSeconds);
    config.max_speed_bps = static_cast<uint64_t>(speedLimitKib) * 1024;
    return config;
}

} // namespace deoxdm::gui
