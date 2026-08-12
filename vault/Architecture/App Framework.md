---
title: App Framework
tags: [architecture, framework]
status: reference
---

# App Framework

> The application lifecycle and registration system.

## App Base Class (`app/App.h`)

Every device app inherits from `App`:

```cpp
class App {
    virtual const char* name() const = 0;
    virtual const char* icon() const { return ""; }
    virtual void onEnter() = 0;      // activated — draw initial screen
    virtual void onExit() = 0;       // deactivated — persist, release
    virtual void onButton(ButtonEvent ev) = 0;
    virtual void onLoop(uint32_t nowMs) {}
    virtual bool wantsSleep() { return true; }
    virtual int32_t sleepWakeupSec() { return -1; }
};
```

## Button Events

Decoded by `AppManager` from GPIO hold-duration bands:

| Event | Trigger | Reading Mode | Menu/Library |
|---|---|---|---|
| `Tap` | Quick press-release (≤350ms) | Next page | Move highlight |
| `MediumHold` | 350–750ms | Previous page | — |
| `LongHold` | ≥750ms | Open menu | Select item |

## AppManager (`app/AppManager.cpp`)

- Owns the app array (max `APP_MAX_COUNT` = 8)
- Decodes GPIO → `ButtonEvent` via hold-duration bands
- Routes events to the active app
- Manages sleep/wake transitions
- Handles `returnToLauncher()` (Button B home)

## SystemContext (`app/SystemContext.h`)

Dependency injection container. Holds:
- `DisplayManager&`
- `BookStorage&`
- `WifiSession&`
- Other shared services

Apps receive `SystemContext` in their constructor — no global singletons.

## AppRegistry (`app/AppRegistry.h`)

One-line-per-app registration. Adding a new app:
1. Create `src/apps/<name>/<Name>App.h + .cpp`
2. Add `#include` and `addApp()` line in `AppRegistry.h`
3. Done — launcher picks it up automatically

## WifiSession (`app/WifiSession.h`)

RAII guard for the WiFi radio. Portal-guarded pattern:
- Turn on WiFi for sync
- Multiple apps share the same WiFi window
- Turn off when done
- NTP sync for clock validity

## See Also
- [[Architecture/System Architecture]]
- [[Architecture/Core Logic Seams]]
- [[Hardware/Pin Map]]
