// SPDX-License-Identifier: MIT
#pragma once

#include <QIcon>
#include <QPalette>

namespace deoxdm::gui {

enum class Glyph {
    Add,
    Resume,
    Pause,
    Remove,
    OpenFile,
    OpenFolder,
    Settings
};

// The application icon, drawn in code so that no image plugin is required.
[[nodiscard]] QIcon applicationIcon();

// A toolbar icon: the desktop theme's icon when it has one, otherwise a
// simple glyph drawn in the palette's text colour.
[[nodiscard]] QIcon actionIcon(Glyph glyph, const QPalette& palette);

} // namespace deoxdm::gui
