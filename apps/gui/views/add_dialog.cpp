// SPDX-License-Identifier: MIT
#include "views/add_dialog.hpp"

#include "deoxdm/core/checksum.hpp"
#include "deoxdm/core/url.hpp"

#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace deoxdm::gui {

AddDialog::AddDialog(const AppSettings& settings, QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(tr("Add Downloads"));
    setMinimumWidth(560);

    urlEdit_ = new QPlainTextEdit(this);
    urlEdit_->setPlaceholderText(tr("https://example.com/file.zip\nOne address per line"));
    urlEdit_->setTabChangesFocus(true);
    urlEdit_->setLineWrapMode(QPlainTextEdit::NoWrap);
    urlEdit_->setFixedHeight(urlEdit_->fontMetrics().lineSpacing() * 6);

    directoryEdit_ = new QLineEdit(settings.downloadDirectory, this);
    auto* browseButton = new QPushButton(tr("Browse…"), this);
    auto* directoryRow = new QHBoxLayout;
    directoryRow->addWidget(directoryEdit_, 1);
    directoryRow->addWidget(browseButton);

    connectionsSpin_ = new QSpinBox(this);
    connectionsSpin_->setRange(1, static_cast<int>(core::kMaxConnections));
    connectionsSpin_->setValue(settings.connections);
    connectionsSpin_->setToolTip(tr("How many parts of a file are downloaded at the same time"));

    sha256Edit_ = new QLineEdit(this);
    sha256Edit_->setPlaceholderText(tr("Optional: 64 hexadecimal characters"));
    sha256Edit_->setToolTip(tr("The finished file is checked against this checksum"));

    messageLabel_ = new QLabel(this);
    messageLabel_->setTextFormat(Qt::PlainText); // messages quote the typed addresses
    messageLabel_->setWordWrap(true);

    buttons_ = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons_->button(QDialogButtonBox::Ok)->setText(tr("Download"));

    auto* form = new QFormLayout;
    form->addRow(tr("&Addresses:"), urlEdit_);
    form->addRow(tr("&Save to:"), directoryRow);
    form->addRow(tr("&Connections:"), connectionsSpin_);
    form->addRow(tr("SHA-&256:"), sha256Edit_);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(messageLabel_);
    layout->addWidget(buttons_);

    connect(buttons_, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(browseButton, &QPushButton::clicked, this, &AddDialog::browse);
    connect(urlEdit_, &QPlainTextEdit::textChanged, this, &AddDialog::validate);
    connect(directoryEdit_, &QLineEdit::textChanged, this, &AddDialog::validate);
    connect(sha256Edit_, &QLineEdit::textChanged, this, &AddDialog::validate);

    validate();
}

void AddDialog::setUrls(const QStringList& urls) {
    urlEdit_->setPlainText(urls.join('\n'));
}

QStringList AddDialog::extractUrls(const QString& text) {
    QStringList urls;
    for (const QString& token : text.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts)) {
        if (core::parse_url(token.toStdString()) && !urls.contains(token)) {
            urls.append(token);
        }
    }
    return urls;
}

void AddDialog::browse() {
    const QString chosen = QFileDialog::getExistingDirectory(this, tr("Save Downloads To"), directoryEdit_->text());
    if (!chosen.isEmpty()) {
        directoryEdit_->setText(chosen);
    }
}

void AddDialog::validate() {
    QString problem;
    int valid = 0;

    const QStringList lines = urlEdit_->toPlainText().split('\n', Qt::SkipEmptyParts);
    for (const QString& line : lines) {
        const QString url = line.trimmed();
        if (url.isEmpty()) continue;
        if (const auto parsed = core::parse_url(url.toStdString()); !parsed) {
            problem = tr("“%1”: %2").arg(url, QString::fromStdString(parsed.error().message));
            break;
        }
        ++valid;
    }

    const QString checksum = sha256Edit_->text().trimmed();
    if (problem.isEmpty() && !checksum.isEmpty()) {
        if (!core::is_valid_sha256_hex(checksum.toStdString())) {
            problem = tr("The checksum must be 64 hexadecimal characters.");
        } else if (valid > 1) {
            problem = tr("A checksum can only be given for a single address.");
        }
    }
    if (problem.isEmpty() && directoryEdit_->text().trimmed().isEmpty()) {
        problem = tr("Choose a folder to save to.");
    }

    messageLabel_->setText(problem);
    messageLabel_->setVisible(!problem.isEmpty());
    buttons_->button(QDialogButtonBox::Ok)->setEnabled(problem.isEmpty() && valid > 0);
}

std::vector<TaskSpec> AddDialog::specs() const {
    std::vector<TaskSpec> result;
    QStringList seen;
    for (const QString& line : urlEdit_->toPlainText().split('\n', Qt::SkipEmptyParts)) {
        const QString url = line.trimmed();
        if (url.isEmpty() || seen.contains(url) || !core::parse_url(url.toStdString())) {
            continue;
        }
        seen.append(url);
        result.push_back(TaskSpec{
            .url = url,
            .directory = directoryEdit_->text().trimmed(),
            .connections = connectionsSpin_->value(),
            .sha256 = sha256Edit_->text().trimmed()
        });
    }
    return result;
}

} // namespace deoxdm::gui
