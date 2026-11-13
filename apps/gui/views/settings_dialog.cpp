// SPDX-License-Identifier: MIT
#include "views/settings_dialog.hpp"

#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace deoxdm::gui {

SettingsDialog::SettingsDialog(const AppSettings& settings, QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(tr("Settings"));
    setMinimumWidth(480);

    directoryEdit_ = new QLineEdit(settings.downloadDirectory, this);
    auto* browseButton = new QPushButton(tr("Browse…"), this);
    auto* directoryRow = new QHBoxLayout;
    directoryRow->addWidget(directoryEdit_, 1);
    directoryRow->addWidget(browseButton);

    connectionsSpin_ = new QSpinBox(this);
    connectionsSpin_->setRange(1, static_cast<int>(core::kMaxConnections));
    connectionsSpin_->setValue(settings.connections);

    concurrentSpin_ = new QSpinBox(this);
    concurrentSpin_->setRange(1, 10);
    concurrentSpin_->setValue(settings.maxConcurrent);

    speedLimitSpin_ = new QSpinBox(this);
    speedLimitSpin_->setRange(0, 10'000'000);
    speedLimitSpin_->setSingleStep(100);
    speedLimitSpin_->setSuffix(tr(" KiB/s"));
    speedLimitSpin_->setSpecialValueText(tr("Unlimited"));
    speedLimitSpin_->setValue(settings.speedLimitKib);

    retriesSpin_ = new QSpinBox(this);
    retriesSpin_->setRange(0, 100);
    retriesSpin_->setValue(settings.retries);

    timeoutSpin_ = new QSpinBox(this);
    timeoutSpin_->setRange(5, 600);
    timeoutSpin_->setSuffix(tr(" s"));
    timeoutSpin_->setValue(settings.timeoutSeconds);

    auto* form = new QFormLayout;
    form->addRow(tr("Default &folder:"), directoryRow);
    form->addRow(tr("&Connections per download:"), connectionsSpin_);
    form->addRow(tr("&Simultaneous downloads:"), concurrentSpin_);
    form->addRow(tr("Speed &limit per download:"), speedLimitSpin_);
    form->addRow(tr("&Retries after a connection error:"), retriesSpin_);
    form->addRow(tr("Give up on a stalled connection &after:"), timeoutSpin_);

    auto* note = new QLabel(tr("Changes apply to downloads that start from now on."), this);
    note->setEnabled(false);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(note);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(browseButton, &QPushButton::clicked, this, [this] {
        const QString chosen = QFileDialog::getExistingDirectory(this, tr("Default Download Folder"), directoryEdit_->text());
        if (!chosen.isEmpty()) {
            directoryEdit_->setText(chosen);
        }
    });
}

AppSettings SettingsDialog::settings() const {
    AppSettings settings;
    settings.downloadDirectory = directoryEdit_->text().trimmed();
    settings.connections = connectionsSpin_->value();
    settings.maxConcurrent = concurrentSpin_->value();
    settings.speedLimitKib = speedLimitSpin_->value();
    settings.retries = retriesSpin_->value();
    settings.timeoutSeconds = timeoutSpin_->value();
    return settings;
}

} // namespace deoxdm::gui
