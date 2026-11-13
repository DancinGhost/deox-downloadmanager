// SPDX-License-Identifier: MIT
#pragma once

#include "model/app_settings.hpp"
#include "model/download_task.hpp"

#include <QDialog>

#include <vector>

class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QSpinBox;

namespace deoxdm::gui {

// Asks for one or more URLs and where to save them.
class AddDialog : public QDialog {
    Q_OBJECT

public:
    explicit AddDialog(const AppSettings& settings, QWidget* parent = nullptr);

    // Pre-fills the URL field.
    void setUrls(const QStringList& urls);

    // One spec per valid URL entered. Only meaningful after accept().
    [[nodiscard]] std::vector<TaskSpec> specs() const;

    // Picks the http(s) URLs out of arbitrary text, e.g. the clipboard.
    [[nodiscard]] static QStringList extractUrls(const QString& text);

private:
    void validate();
    void browse();

    QPlainTextEdit* urlEdit_;
    QLineEdit* directoryEdit_;
    QSpinBox* connectionsSpin_;
    QLineEdit* sha256Edit_;
    QLabel* messageLabel_;
    QDialogButtonBox* buttons_;
};

} // namespace deoxdm::gui
