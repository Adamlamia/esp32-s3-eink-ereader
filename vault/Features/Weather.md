---
title: Weather App
tags: [feature, app, sync]
status: active
batch: "1"
---

# Weather

> Open-Meteo weather (free, no API key). Current conditions + 3-day forecast.

## Overview

Fetches weather from Open-Meteo API. Shows temperature, feels-like, humidity, wind, weather icon, and 3-day forecast. Caches to `/weather.json`.

## Key Files

| File | Role |
|---|---|
| `src/apps/weather/WeatherApp.cpp` | App lifecycle + UI |
| `src/apps/weather/WeatherStore.cpp` | JSON cache persistence |
| `src/apps/weather/WeatherSync.cpp` | HTTPS fetch + RAII WiFi |
| `src/core/OpenMeteo.h` | Open-Meteo JSON parser (core seam) |

## Configuration

| Define | Value | Description |
|---|---|---|
| `WEATHER_CACHE_FILE` | `/weather.json` | Cache path |
| `WEATHER_LABEL_MAX` | 24 | Location label buffer |
| `WEATHER_FORECAST_DAYS` | 3 | Daily forecast rows |
| `WEATHER_URL_MAX` | 320 | Request URL buffer |
| `WEATHER_BODY_MAX` | 8192 | Response body cap |
| `WEATHER_STALE_SEC` | 3h | On-open resync threshold |
| `WEATHER_MIN_BATTERY_FOR_SYNC` | 15% | Battery floor (auto only) |
| `WEATHER_TZ` | `Asia/Kuala_Lumpur` | Open-Meteo timezone |

## Default Location

Kuala Lumpur (lat 3.1390, lon 101.6869). Override via `secrets.h`:
```
#define WEATHER_LAT    3.1390f
#define WEATHER_LON    101.6869f
#define WEATHER_LABEL  "Kuala Lumpur"
```

## Patterns

- Reuses calendar's WiFi/NTP/HTTPS + RAII-WiFi-off + portal-guard
- On-open sync if stale + manual refresh button
- Manual refresh always allowed (no battery floor)

## Tests

Native Unity tests cover `OpenMeteo` parsing.

## See Also
- [[Architecture/Core Logic Seams]]
