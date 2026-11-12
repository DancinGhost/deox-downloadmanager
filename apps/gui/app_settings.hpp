// SPDX-License-Identifier: MIT
#pragma once

#include "deoxdm/core/types.hpp"

#include <QString>

namespace deoxdm::gui {

// User preferences of the desktop application.
struct AppSettings {
    QString downloadDirectory;  // where new downloads are saved by default
    int connections{4};         // connections per download
    int maxConcurrent{3};       // downloads running at the same time
    int speedLimitKib{0};       // per download, KiB/s; 0 = unlimited
    int retries{3};
    int timeoutSeconds{30};

    // Reads the stored preferences, falling back to defaults.
    [[nodiscard]] static AppSettings load();
    void save() const;

    // Engine configuration shared by all downloads.
    [[nodiscard]] core::DownloadConfig baseConfig() const;
};

} // namespace deoxdm::gui
