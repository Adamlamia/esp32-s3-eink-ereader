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
// #include "../apps/weather/WeatherApp.h"      // future: feature/weather
// #include "../apps/todo/TodoApp.h"            // future: feature/todo
// #include "../apps/kanban/KanbanApp.h"        // future: feature/kanban
// #include "../apps/habits/HabitsApp.h"        // future: feature/habits

// --- Registration ----------------------------------------------------------
inline void registerAllApps(AppManager &mgr, SystemContext &ctx) {
    mgr.addApp(new ReaderApp(ctx));
    // mgr.addApp(new WeatherApp(ctx));         // future: feature/weather
    // mgr.addApp(new TodoApp(ctx));            // future: feature/todo
    // mgr.addApp(new KanbanApp(ctx));          // future: feature/kanban
    // mgr.addApp(new HabitsApp(ctx));          // future: feature/habits
}
