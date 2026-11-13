// SPDX-License-Identifier: MIT
#include "views/icons.hpp"

#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

namespace deoxdm::gui {

namespace {

QPixmap drawApplicationIcon(int size) {
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const double s = size;

    QLinearGradient gradient(0, 0, s, s);
    gradient.setColorAt(0.0, QColor(0x1F, 0x9D, 0x8B));
    gradient.setColorAt(1.0, QColor(0x1B, 0x6F, 0xC2));
    painter.setPen(Qt::NoPen);
    painter.setBrush(gradient);
    painter.drawRoundedRect(QRectF(s * 0.04, s * 0.04, s * 0.92, s * 0.92), s * 0.2, s * 0.2);

    // Downward arrow above a tray
    QPen pen(Qt::white, s * 0.09, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawLine(QPointF(s * 0.5, s * 0.22), QPointF(s * 0.5, s * 0.58));
    QPainterPath head;
    head.moveTo(s * 0.33, s * 0.43);
    head.lineTo(s * 0.5, s * 0.60);
    head.lineTo(s * 0.67, s * 0.43);
    painter.drawPath(head);
    painter.drawLine(QPointF(s * 0.27, s * 0.76), QPointF(s * 0.73, s * 0.76));

    return pixmap;
}

QPixmap drawGlyph(Glyph glyph, const QColor& color, int size) {
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const double s = size;
    const QPen pen(color, s * 0.09, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    switch (glyph) {
        case Glyph::Add:
            painter.drawLine(QPointF(s * 0.5, s * 0.2), QPointF(s * 0.5, s * 0.8));
            painter.drawLine(QPointF(s * 0.2, s * 0.5), QPointF(s * 0.8, s * 0.5));
            break;
        case Glyph::Resume: {
            QPainterPath triangle;
            triangle.moveTo(s * 0.3, s * 0.2);
            triangle.lineTo(s * 0.78, s * 0.5);
            triangle.lineTo(s * 0.3, s * 0.8);
            triangle.closeSubpath();
            painter.setBrush(color);
            painter.drawPath(triangle);
            break;
        }
        case Glyph::Pause:
            painter.setPen(QPen(color, s * 0.16, Qt::SolidLine, Qt::RoundCap));
            painter.drawLine(QPointF(s * 0.36, s * 0.25), QPointF(s * 0.36, s * 0.75));
            painter.drawLine(QPointF(s * 0.64, s * 0.25), QPointF(s * 0.64, s * 0.75));
            break;
        case Glyph::Remove: {
            // Waste bin
            painter.drawLine(QPointF(s * 0.2, s * 0.28), QPointF(s * 0.8, s * 0.28));
            painter.drawLine(QPointF(s * 0.4, s * 0.17), QPointF(s * 0.6, s * 0.17));
            QPainterPath bin;
            bin.moveTo(s * 0.28, s * 0.28);
            bin.lineTo(s * 0.33, s * 0.83);
            bin.lineTo(s * 0.67, s * 0.83);
            bin.lineTo(s * 0.72, s * 0.28);
            painter.drawPath(bin);
            break;
        }
        case Glyph::OpenFile: {
            QPainterPath sheet;
            sheet.moveTo(s * 0.27, s * 0.15);
            sheet.lineTo(s * 0.58, s * 0.15);
            sheet.lineTo(s * 0.75, s * 0.33);
            sheet.lineTo(s * 0.75, s * 0.85);
            sheet.lineTo(s * 0.27, s * 0.85);
            sheet.closeSubpath();
            painter.drawPath(sheet);
            QPainterPath fold;
            fold.moveTo(s * 0.57, s * 0.16);
            fold.lineTo(s * 0.57, s * 0.34);
            fold.lineTo(s * 0.74, s * 0.34);
            painter.drawPath(fold);
            break;
        }
        case Glyph::OpenFolder: {
            QPainterPath folder;
            folder.moveTo(s * 0.15, s * 0.8);
            folder.lineTo(s * 0.15, s * 0.22);
            folder.lineTo(s * 0.42, s * 0.22);
            folder.lineTo(s * 0.5, s * 0.33);
            folder.lineTo(s * 0.85, s * 0.33);
            folder.lineTo(s * 0.85, s * 0.8);
            folder.closeSubpath();
            painter.drawPath(folder);
            break;
        }
        case Glyph::Settings: {
            // Three sliders
            for (int i = 0; i < 3; ++i) {
                const double y = s * (0.27 + 0.23 * i);
                const double knob = s * (i == 1 ? 0.36 : 0.64);
                painter.drawLine(QPointF(s * 0.17, y), QPointF(s * 0.83, y));
                painter.setBrush(color);
                painter.drawEllipse(QPointF(knob, y), s * 0.07, s * 0.07);
                painter.setBrush(Qt::NoBrush);
            }
            break;
        }
    }

    return pixmap;
}

const char* themeName(Glyph glyph) {
    switch (glyph) {
        case Glyph::Add:        return "list-add";
        case Glyph::Resume:     return "media-playback-start";
        case Glyph::Pause:      return "media-playback-pause";
        case Glyph::Remove:     return "edit-delete";
        case Glyph::OpenFile:   return "document-open";
        case Glyph::OpenFolder: return "folder-open";
        case Glyph::Settings:   return "preferences-system";
    }
    return "";
}

} // namespace

QIcon applicationIcon() {
    QIcon icon;
    for (const int size : {16, 24, 32, 48, 64, 128, 256}) {
        icon.addPixmap(drawApplicationIcon(size));
    }
    return icon;
}

QIcon actionIcon(Glyph glyph, const QPalette& palette) {
    const QString name = QString::fromLatin1(themeName(glyph));
    if (QIcon::hasThemeIcon(name)) {
        return QIcon::fromTheme(name);
    }

    QIcon icon;
    QColor disabled = palette.color(QPalette::Disabled, QPalette::ButtonText);
    if (disabled == palette.color(QPalette::Active, QPalette::ButtonText)) {
        disabled.setAlphaF(0.35f);
    }
    for (const int size : {16, 24, 32, 48}) {
        icon.addPixmap(drawGlyph(glyph, palette.color(QPalette::Active, QPalette::ButtonText), size), QIcon::Normal);
        icon.addPixmap(drawGlyph(glyph, disabled, size), QIcon::Disabled);
    }
    return icon;
}

} // namespace deoxdm::gui
