#pragma once
// ===========================================================================
//  AppRegistry  —  Compile-time app registration (one line per feature branch)
// ===========================================================================
//  MERGE STRATEGY: each feature branch adds ONE #include and ONE addApp() line
//  in this file. When multiple feature branches are merged via PR, conflicts
//  are trivially additive — just keep all lines. No other file is touched by
//  a feature branch's registration, so cross-feature conflicts are impossible.
//
//  To add a new app:
//    1. Create src/apps/<name>/<Name>App.h + .cpp (inherits App).
//    2. Add the #include and addApp() line below.
//    3. Done — the launcher picks it up automatically.
// ===========================================================================
#include "app/AppManager.h"

// --- App includes (one per registered app) ---------------------------------
#include "../apps/reader/ReaderApp.h"
#include "../apps/calendar/CalendarApp.h"   // feature/calendar (R2)
#include "../apps/weather/WeatherApp.h"     // feature/weather (WTH·R1)
#include "../apps/qr/QrApp.h"             // feature/qr (QR-R1)
#include "../apps/devcompanion/DevCompanionApp.h"  // feature/devcompanion (DEV·R1)
#include "../apps/voicejournal/VoiceJournalApp.h"  // feature/voicejournal (VJ.R1)
// DEFERRED (2026-08-02): Todo is built, tested and merged but DISABLED in the
// launcher pending a backend decision. Google Tasks (the product) exposes NO
// ICS/CalDAV feed — only an OAuth2 JSON API — so the current "Tasks Google
// Calendar (all-day events)" source only suits date-bounded tasks; the owner's
// tasks are mostly undated. Revisit with a backend that serves undated tasks
// (e.g. self-hosted/3rd-party ICS bridge — "Option C"). To re-enable: uncomment
// the two lines below. Code + native tests stay live either way. TODO(TODO-BACKEND)
// #include "../apps/todo/TodoApp.h"             // feature/todo (TODO-R1) — deferred
// #include "../apps/kanban/KanbanApp.h"        // future: feature/kanban
// #include "../apps/habits/HabitsApp.h"        // future: feature/habits

// --- Registration ----------------------------------------------------------
inline void registerAllApps(AppManager &mgr, SystemContext &ctx) {
    mgr.addApp(new ReaderApp(ctx));
    mgr.addApp(new CalendarApp(ctx));           // feature/calendar (R2)
    mgr.addApp(new WeatherApp(ctx));            // feature/weather (WTH·R1)
    mgr.addApp(new QrApp(ctx));               // feature/qr (QR-R1)
    mgr.addApp(new DevCompanionApp(ctx));      // feature/devcompanion (DEV·R1)
    mgr.addApp(new apps::VoiceJournalApp(ctx));      // feature/voicejournal (VJ.R1)
    // mgr.addApp(new TodoApp(ctx));           // feature/todo (TODO-R1) — DEFERRED, see note above
    // mgr.addApp(new KanbanApp(ctx));          // future: feature/kanban
    // mgr.addApp(new HabitsApp(ctx));          // future: feature/habits
}