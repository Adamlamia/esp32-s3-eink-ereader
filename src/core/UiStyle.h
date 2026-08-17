#pragma once
// ===========================================================================
//  core/UiStyle.h  —  shared UI layout tokens (STD·R1)
// ===========================================================================
//  BASELINE MODEL: in ALL DisplayManager draw functions, `y` is the text
//  BASELINE — glyphs extend ~30 px ABOVE it (ascender) and a few px below
//  (descender). Every token in this header is a baseline. Never treat a y
//  value as the top of a text box.
//
//  TWO FONTS (there is no size hierarchy — FiraSans comes in one size):
//    drawText / drawTextCentered  ->  FiraSans, the single UI font
//                                     (titles, menu items, section headers)
//    drawBookText                 ->  Georgia reading font (~14pt, or ~12pt
//                                     when the user selects the small size):
//                                     subtitles, list rows, footers
//
//  Panel: 960x540 ED047TC1, MARGIN_X=26, MARGIN_Y=20, STATUS_H=30 (config.h).
//  Values below encode the house conventions fixed by Calendar/Todo/
//  DevCompanion; apps must anchor shared elements on these tokens instead of
//  private magic numbers.
// ===========================================================================
#include "config.h"   // DISPLAY_HEIGHT (pure macros, no HAL — host-safe)

namespace ui {

// --- Content screens (Calendar / Todo / DevCompanion convention) -----------
constexpr int TITLE_Y    = 50;                        // FiraSans title, centred
constexpr int SUBTITLE_Y = 92;                        // Georgia sync line, left at MARGIN_X
constexpr int CONTENT_Y  = 130;                       // first content row baseline
constexpr int ROW_GAP    = 8;                         // list rows advance readerLineHeight() + ROW_GAP
constexpr int FOOTER_Y   = DISPLAY_HEIGHT - 14;       // Georgia gesture legend

// --- Menus (Tap = move, LongHold = select; items x = DISPLAY_WIDTH/2-220) --
constexpr int MENU_TITLE_Y  = 74;                     // FiraSans menu title, centred
constexpr int MENU_START_Y  = 230;                    // first menu item baseline
constexpr int MENU_FOOTER_Y = DISPLAY_HEIGHT - 16;    // "Tap = move    Hold = select"

} // namespace ui
