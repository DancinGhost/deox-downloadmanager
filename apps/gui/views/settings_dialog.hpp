// SPDX-License-Identifier: MIT
#pragma once

#include "model/app_settings.hpp"

#include <QDialog>

class QLineEdit;
class QSpinBox;

namespace deoxdm::gui {

class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit SettingsDialog(const AppSettings& settings, QWidget* parent = nullptr);

    [[nodiscard]] AppSettings settings() const;

private:
    QLineEdit* directoryEdit_;
    QSpinBox* connectionsSpin_;
    QSpinBox* concurrentSpin_;
    QSpinBox* speedLimitSpin_;
    QSpinBox* retriesSpin_;
    QSpinBox* timeoutSpin_;
};

} // namespace deoxdm::gui
