#pragma once
// ===========================================================================
//  App  —  Abstract base class for all device applications
// ===========================================================================
//  Every "app" on the device (E-Reader, Weather, To-Do, etc.) inherits from
//  this interface. The AppManager owns the lifecycle: it calls onEnter() when
//  the user selects the app from the launcher, routes decoded button events
//  via onButton(), ticks onLoop() each iteration, and calls onExit() when the
//  user navigates back to the launcher.
//
//  Apps never read GPIO directly — the AppManager decodes the single-button
//  hold-band gestures and delivers them as ButtonEvent values.
// ===========================================================================
#include <Arduino.h>

// --- Button events (decoded by AppManager from GPIO21 hold bands) ----------
enum class ButtonEvent {
    Tap,          // quick press-release (< BTN_PREVHOLD_MS)
    MediumHold,   // held BTN_PREVHOLD_MS .. BTN_LONGPRESS_MS, fired on release
    LongHold,     // held >= BTN_LONGPRESS_MS, fired while held
};

// --- App interface ---------------------------------------------------------
class App {
public:
    virtual ~App() = default;

    // --- Identity (shown in the launcher list) ---
    virtual const char* name() const = 0;           // e.g. "E-Reader"
    virtual const char* icon() const { return ""; } // optional glyph / ASCII art

    // --- Lifecycle ---
    virtual void onEnter() = 0;     // app activated — draw the initial screen
    virtual void onExit()  = 0;     // app deactivated — persist state, release

    // --- Input ---
    virtual void onButton(ButtonEvent ev) = 0;

    // --- Optional hooks ---
    virtual void onLoop(uint32_t nowMs) {}          // periodic tick (network poll, etc.)
    virtual bool wantsSleep() { return true; }      // allow light-sleep when idle?
};
