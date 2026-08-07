#pragma once
// ===========================================================================
//  ButtonClassify  —  Pure-logic hold-band gesture classification
// ===========================================================================
//  Header-only seam (no HAL dependencies) that classifies a button press
//  duration into a gesture event. Extracted from AppManager for unit testing.
//
//  Timing bands (configurable via config.h constants):
//    held <  DEBOUNCE_MS            → None (contact bounce, ignore)
//    held >= DEBOUNCE_MS            → Tap         (quick press-release)
//    held >= PREVHOLD_MS            → MediumHold  (deliberate hold on release)
//    held >= LONGPRESS_MS (while held) → LongHold (fires before release)
// ===========================================================================
#include <cstdint>

namespace core {

// Result of classifying a button event.
enum class Gesture {
    None,         // bounce or invalid — caller should ignore
    Tap,          // quick press-release
    MediumHold,   // held past the "previous" threshold, released before long-press
    LongHold,     // held past the long-press threshold (fires while still held)
};

// Classify a button RELEASE event given how long it was held (milliseconds).
// Returns None for bounces below the debounce threshold.
// LongHold is NOT returned here — it fires while the button is still held
// (use isLongPress() below to detect that threshold crossing).
inline Gesture classifyRelease(uint32_t heldMs,
                               uint32_t debounceMs  = 30,
                               uint32_t prevHoldMs  = 350,
                               uint32_t longPressMs = 750) {
    (void)longPressMs;  // LongHold fires during hold, not on release
    if (heldMs < debounceMs)  return Gesture::None;
    if (heldMs < prevHoldMs)  return Gesture::Tap;
    return Gesture::MediumHold;
}

// Check whether the button has been held long enough to fire a LongHold.
// Called repeatedly while the button is down; returns true once the threshold
// is crossed (caller should fire LongHold exactly once via a flag).
inline bool isLongPress(uint32_t heldMs, uint32_t longPressMs = 750) {
    return heldMs >= longPressMs;
}

// Wrap-around selection helper for lists (launcher, menus).
// Moves `sel` by `delta` within [0, count), wrapping at both ends.
// Returns the new selection index. Guards against count <= 0.
inline int wrapSelection(int sel, int delta, int count) {
    if (count <= 0) return 0;
    sel += delta;
    if (sel < 0)     sel = count - 1;
    if (sel >= count) sel = 0;
    return sel;
}

// Check if a new tap falls within the burst window of the first tap.
// Returns true if the tap should be accumulated (now - firstTapMs <= windowMs).
// Used by AppManager for multi-tap navigation acceleration (batch 3).
inline bool isInBurstWindow(uint32_t nowMs, uint32_t firstTapMs, uint32_t windowMs) {
    return (nowMs - firstTapMs) <= windowMs;
}

} // namespace core
