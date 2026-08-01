#pragma once
// ===========================================================================
//  core/OpenMeteo.h  —  Open-Meteo weather parsing + cache seam (WTH·R1)
// ===========================================================================
//  Header-only, heap-free (no new/malloc; fixed buffers bounded by the
//  WEATHER_* macros), HAL-free pure-logic seam for the Weather app, in the
//  same style as core/CalendarEvent.h + core/IcsParser.h:
//
//    parseOpenMeteo          — tolerant Open-Meteo /v1/forecast JSON -> struct
//    serializeWeatherCache   — snapshot -> compact /weather.json document
//    deserializeWeatherCache — /weather.json document -> snapshot (tolerant)
//    buildOpenMeteoUrl       — pure request-URL builder (fixed buffer)
//    weatherCodeGlyph        — WMO code -> short ASCII glyph for the e-ink
//    formatTenthsC           — tenths-of-°C int -> "25.1" style string
//
//  Units convention (documented per the schema choice):
//    WeatherCurrent holds float °C / km/h exactly as the API reports them.
//    WeatherDay holds int16_t TENTHS of a °C (251 == 25.1 °C, -52 == -5.2 °C)
//    so the daily forecast stays compact integer JSON in the cache and needs
//    no float formatting in the UI seam.
//
//  fetchedUtc / label convention: parseOpenMeteo() leaves fetchedUtc = 0 and
//  label = "" — the parser is pure and cannot know "now" or the configured
//  location. The sync session stamps both (NTP time + WEATHER_LABEL) before
//  persisting, and the cache round-trip carries them.
//
//  ArduinoJson 7 is header-only and identical on host and device, so the whole
//  seam compiles under `pio test -e native` with no Arduino core.
// ===========================================================================
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cmath>
#include <cstdio>
#include <string>
#include <ArduinoJson.h>
#include "config.h"   // WEATHER_* sizing constants (pure macros, no HAL)

// --- Safe fallbacks so the header compiles standalone (no config.h values) --
#ifndef WEATHER_LABEL_MAX
  #define WEATHER_LABEL_MAX 24
#endif
#ifndef WEATHER_FORECAST_DAYS
  #define WEATHER_FORECAST_DAYS 3
#endif
#ifndef WEATHER_URL_MAX
  #define WEATHER_URL_MAX 320
#endif
#ifndef WEATHER_BODY_MAX
  #define WEATHER_BODY_MAX 8192
#endif

namespace core {

// --- Data types -------------------------------------------------------------
// Current conditions, exactly as Open-Meteo reports them (metric units).
struct WeatherCurrent {
    float tempC;        // temperature_2m, °C
    float feelsC;       // apparent_temperature, °C
    float windKph;      // wind_speed_10m, km/h
    int   humidityPct;  // relative_humidity_2m, % (sanitised to 0..100)
    int   weatherCode;  // WMO weather interpretation code (0..99; -1 = unknown)
    bool  valid;        // false unless parsed from a usable "current" object
};

// One forecast day. Temperatures are tenths of a °C (see header note).
struct WeatherDay {
    int16_t tMin;       // °C x 10  (251 == 25.1 °C)
    int16_t tMax;       // °C x 10
    int     weatherCode;  // WMO code (0..99; -1 = unknown)
    bool    valid;        // false unless at least one usable temperature parsed
};

// Everything the Weather app renders + the cache persists.
struct WeatherSnapshot {
    WeatherCurrent cur;
    WeatherDay     days[WEATHER_FORECAST_DAYS];
    int            dayCount;    // usable daily entries, 0..WEATHER_FORECAST_DAYS
    int64_t        fetchedUtc;  // UTC epoch seconds of the fetch (0 = never)
    char           label[WEATHER_LABEL_MAX];   // location label, NUL-terminated
};

// Reset a snapshot to safe defaults so partially-parsed fields are never
// garbage (mirrors calEventClear).
inline void weatherSnapshotClear(WeatherSnapshot &s) {
    s.cur.tempC       = 0.0f;
    s.cur.feelsC      = 0.0f;
    s.cur.windKph     = 0.0f;
    s.cur.humidityPct = 0;
    s.cur.weatherCode = -1;
    s.cur.valid       = false;
    for (int i = 0; i < WEATHER_FORECAST_DAYS; ++i) {
        s.days[i].tMin        = 0;
        s.days[i].tMax        = 0;
        s.days[i].weatherCode = -1;
        s.days[i].valid       = false;
    }
    s.dayCount    = 0;
    s.fetchedUtc  = 0;
    s.label[0]    = '\0';
}

// --- Numeric sanitisation helpers -------------------------------------------
// Tenths-of-°C from a float °C reading. Non-finite (NaN/inf) -> 0; the result
// is clamped to the int16_t range so no conversion is ever undefined.
inline int16_t tempToTenths(float c) {
    if (!std::isfinite(c)) return 0;
    float v = c * 10.0f;
    if (v >  32760.0f) v =  32760.0f;
    if (v < -32760.0f) v = -32760.0f;
    return (int16_t)(v >= 0.0f ? v + 0.5f : v - 0.5f);   // round half away from 0
}

// Format tenths-of-°C as a plain ASCII string ("25.1", "-5.2", "0.0") — no
// '°' glyph (the bundled FiraSans is ASCII-only). Always NUL-terminates; a
// too-small buffer yields an empty string rather than an overflow.
inline void formatTenthsC(int16_t tenths, char *out, size_t cap) {
    if (!out || cap == 0) return;
    if (cap < 5) { out[0] = '\0'; return; }              // need room for "x.y\0"
    int v = (int)tenths;
    int whole = v / 10;
    int frac  = v % 10;
    if (frac < 0) frac = -frac;
    snprintf(out, cap, "%d.%d", whole, frac);
}

// --- WMO weather interpretation code -> ASCII glyph ---------------------------
// Bucketing (WMO 4677 code table, as reported by Open-Meteo "weather_code"):
//    0          "SUN"  clear sky
//    1..2       "PRT"  mainly clear / partly cloudy
//    3          "CLD"  overcast
//    45, 48     "FOG"  fog / depositing rime fog
//    51..67     "RAN"  drizzle + rain (incl. freezing variants)
//    80..82     "RAN"  rain showers
//    71..77     "SNW"  snow fall + snow grains
//    85..86     "SNW"  snow showers
//    95..99     "STM"  thunderstorm (incl. hail variants)
//    anything else (negative, 4, 5..44 gaps, >= 100) -> "?"
// Returns a string literal (static storage — never freed, no heap).
inline const char *weatherCodeGlyph(int code) {
    if (code == 0)                          return "SUN";
    if (code >= 1  && code <= 2)            return "PRT";
    if (code == 3)                          return "CLD";
    if (code == 45 || code == 48)           return "FOG";
    if (code >= 51 && code <= 67)           return "RAN";
    if (code >= 80 && code <= 82)           return "RAN";
    if (code >= 71 && code <= 77)           return "SNW";
    if (code >= 85 && code <= 86)           return "SNW";
    if (code >= 95 && code <= 99)           return "STM";
    return "?";
}

// --- Open-Meteo response parser ----------------------------------------------
// Parse one /v1/forecast response (current + daily) into `out` (overwritten).
// Tolerant by contract — never throws, never crashes on hostile input:
//   returns false on: empty / unparseable / non-object input, or a missing
//   "current" or "daily" object (the two sections the app cannot do without).
//   returns true otherwise, with:
//     - cur.valid   = false unless temperature_2m parsed as a FINITE float
//                     (NaN/inf, e.g. from 1e999, are rejected); humidity is
//                     clamped to 0..100, wind to 0..500 km/h, weather code
//                     kept only inside 0..99 (else -1 = unknown).
//     - days[0..n]  = up to WEATHER_FORECAST_DAYS entries; longer API arrays
//                     are clamped, shorter ones leave dayCount smaller; a day
//                     is valid iff at least one of its min/max is finite
//                     (a missing side mirrors the present one).
//   fetchedUtc stays 0 and label stays "" — the sync session stamps them.
inline bool parseOpenMeteo(const std::string &json, WeatherSnapshot &out) {
    weatherSnapshotClear(out);
    if (json.empty()) return false;

    JsonDocument doc;
    if (deserializeJson(doc, json)) return false;   // corrupt / truncated
    if (!doc.is<JsonObject>()) return false;        // array / scalar / null
    JsonObject root = doc.as<JsonObject>();

    JsonVariant curV = root["current"];
    if (curV.isNull() || !curV.is<JsonObject>()) return false;
    JsonObject cur = curV.as<JsonObject>();
    JsonVariant dailyV = root["daily"];
    if (dailyV.isNull() || !dailyV.is<JsonObject>()) return false;
    JsonObject daily = dailyV.as<JsonObject>();

    // --- current conditions --------------------------------------------------
    JsonVariant tv = cur["temperature_2m"];
    float t = tv.as<float>();
    if (!tv.isNull() && std::isfinite(t)) {
        out.cur.tempC  = t;
        out.cur.valid  = true;
        // feels-like falls back to the real temperature when absent/bad.
        JsonVariant fv = cur["apparent_temperature"];
        float f = fv.as<float>();
        out.cur.feelsC = (!fv.isNull() && std::isfinite(f)) ? f : t;
    }

    long h = cur["relative_humidity_2m"] | -1L;
    if (h < 0)   h = 0;
    if (h > 100) h = 100;                            // sanitise to a physical range
    out.cur.humidityPct = (int)h;

    JsonVariant wv = cur["wind_speed_10m"];
    float w = wv.as<float>();
    if (!wv.isNull() && std::isfinite(w)) {
        if (w < 0.0f)   w = 0.0f;
        if (w > 500.0f) w = 500.0f;                  // beyond any surface gust
        out.cur.windKph = w;
    }

    long cc = cur["weather_code"] | -1L;
    out.cur.weatherCode = (cc >= 0 && cc <= 99) ? (int)cc : -1;

    // --- daily forecast --------------------------------------------------------
    JsonArray dMax  = daily["temperature_2m_max"].as<JsonArray>();
    JsonArray dMin  = daily["temperature_2m_min"].as<JsonArray>();
    JsonArray dCode = daily["weather_code"].as<JsonArray>();

    int n = (int)dMax.size();
    if ((int)dMin.size()  > n) n = (int)dMin.size();
    if ((int)dCode.size() > n) n = (int)dCode.size();
    if (n > WEATHER_FORECAST_DAYS) n = WEATHER_FORECAST_DAYS;   // clamp to capacity

    for (int i = 0; i < n; ++i) {
        WeatherDay &d = out.days[i];
        bool haveMax = i < (int)dMax.size() && dMax[i].is<float>() && std::isfinite(dMax[i].as<float>());
        bool haveMin = i < (int)dMin.size() && dMin[i].is<float>() && std::isfinite(dMin[i].as<float>());
        if (haveMax || haveMin) {
            float mx = haveMax ? dMax[i].as<float>() : dMin[i].as<float>();
            float mn = haveMin ? dMin[i].as<float>() : mx;   // missing side mirrors
            if (mn > mx) { float tmp = mn; mn = mx; mx = tmp; }  // never inverted
            d.tMax = tempToTenths(mx);
            d.tMin = tempToTenths(mn);
            d.valid = true;
        }
        if (i < (int)dCode.size()) {
            long dc = dCode[i] | -1L;
            d.weatherCode = (dc >= 0 && dc <= 99) ? (int)dc : -1;
        }
    }
    out.dayCount = n;
    return true;
}

// --- /weather.json cache serialization (ArduinoJson 7, compact keys) ----------
// Schema v1 (mirrors CalendarStore's cache shape):
//   {
//     "v":    1,                    // schema version (bump if fields change)
//     "sync": 1785715200,           // fetchedUtc, UTC epoch seconds
//     "lbl":  "Kuala Lumpur",       // location label
//     "cur":  { "ok": true, "t": 26.9, "f": 33.7, "w": 1.1, "h": 92, "c": 2 },
//     "days": [ { "ok": true, "n": 251, "x": 316, "c": 81 }, ... ]
//   }
//   cur:  t=tempC f=feelsC w=windKph h=humidityPct c=weatherCode ok=valid
//   days: n=tMin x=tMax (tenths of °C) c=weatherCode ok=valid
inline void serializeWeatherCache(std::string &out, const WeatherSnapshot &s) {
    JsonDocument doc;
    doc["v"]    = 1;
    doc["sync"] = s.fetchedUtc;
    doc["lbl"]  = s.label;

    JsonObject c = doc["cur"].to<JsonObject>();
    c["ok"] = s.cur.valid;
    c["t"]  = s.cur.tempC;
    c["f"]  = s.cur.feelsC;
    c["w"]  = s.cur.windKph;
    c["h"]  = s.cur.humidityPct;
    c["c"]  = s.cur.weatherCode;

    JsonArray arr = doc["days"].to<JsonArray>();
    for (int i = 0; i < s.dayCount && i < WEATHER_FORECAST_DAYS; ++i) {
        const WeatherDay &d = s.days[i];
        JsonObject o = arr.add<JsonObject>();
        o["ok"] = d.valid;
        o["n"]  = d.tMin;
        o["x"]  = d.tMax;
        o["c"]  = d.weatherCode;
    }
    out.clear();
    serializeJson(doc, out);
}

// Deserialize a cache document into `out` (overwritten). Tolerant by design:
//   - empty / unparseable / non-object input -> false, cleared snapshot
//     (this is the WeatherStore::load corrupt-file contract, host-testable)
//   - a valid object always yields true with sanitised fields: missing "cur"
//     -> cur.valid=false; missing/short "days" -> smaller dayCount; label is
//     truncated to WEATHER_LABEL_MAX-1 and always NUL-terminated.
inline bool deserializeWeatherCache(const std::string &in, WeatherSnapshot &out) {
    weatherSnapshotClear(out);
    if (in.empty()) return false;

    JsonDocument doc;
    if (deserializeJson(doc, in)) return false;     // corrupt / truncated
    if (!doc.is<JsonObject>()) return false;

    out.fetchedUtc = doc["sync"] | (int64_t)0;

    const char *lbl = doc["lbl"] | "";
    strncpy(out.label, lbl, WEATHER_LABEL_MAX - 1);
    out.label[WEATHER_LABEL_MAX - 1] = '\0';        // always NUL-terminated

    JsonVariant c = doc["cur"];
    if (!c.isNull() && c.is<JsonObject>()) {
        out.cur.valid       = c["ok"] | false;
        out.cur.tempC       = c["t"]  | 0.0f;
        out.cur.feelsC      = c["f"]  | 0.0f;
        out.cur.windKph     = c["w"]  | 0.0f;
        long h = c["h"] | 0L;
        if (h < 0)   h = 0;
        if (h > 100) h = 100;
        out.cur.humidityPct = (int)h;
        long cc = c["c"] | -1L;
        out.cur.weatherCode = (cc >= -1 && cc <= 99) ? (int)cc : -1;
        if (!std::isfinite(out.cur.tempC) || !std::isfinite(out.cur.feelsC) ||
            !std::isfinite(out.cur.windKph)) {
            out.cur.valid = false;                  // never surface non-finite floats
        }
    }

    JsonArray arr = doc["days"].as<JsonArray>();
    int n = 0;
    for (JsonObject o : arr) {
        if (n >= WEATHER_FORECAST_DAYS) break;      // bound to capacity
        WeatherDay &d = out.days[n];
        d.valid = o["ok"] | false;
        long mn = o["n"] | 0L;
        long mx = o["x"] | 0L;
        if (mn < -32760) mn = -32760;
        if (mn >  32760) mn =  32760;
        if (mx < -32760) mx = -32760;
        if (mx >  32760) mx =  32760;
        d.tMin = (int16_t)mn;
        d.tMax = (int16_t)mx;
        long dc = o["c"] | -1L;
        d.weatherCode = (dc >= -1 && dc <= 99) ? (int)dc : -1;
        ++n;
    }
    out.dayCount = n;
    return true;
}

// --- Request URL builder (pure, fixed buffer, no heap) -------------------------
// Build the Open-Meteo /v1/forecast GET URL for (lat, lon, tz) requesting
// exactly the fields parseOpenMeteo() consumes (current: temperature_2m,
// apparent_temperature, relative_humidity_2m, wind_speed_10m, weather_code;
// daily: weather_code, temperature_2m_max, temperature_2m_min) with
// forecast_days = WEATHER_FORECAST_DAYS.
//
// `tz` is percent-encoded (RFC 3986 unreserved characters pass through, so
// "Asia/Kuala_Lumpur" -> "Asia%2FKuala_Lumpur"). The full KL URL is ~270
// chars, so the internal staging buffer is WEATHER_URL_MAX wide.
//
// Failure is LOUD and safe: a null/zero-cap buffer is a no-op; if the URL
// does not fit (staging overflow OR caller cap too small) out[0] is set to
// '\0' so the caller's `url[0] == '\0'` check aborts the sync with a readable
// error instead of requesting a truncated URL. Never overflows.
inline void buildOpenMeteoUrl(char *out, size_t cap, float lat, float lon,
                              const char *tz) {
    if (!out || cap == 0) return;
    out[0] = '\0';
    if (!tz) tz = "";

    // Percent-encode the timezone into a small stack buffer.
    char tzEnc[64];
    size_t p = 0;
    for (const char *s = tz; *s && p + 3 < sizeof(tzEnc); ++s) {
        unsigned char ch = (unsigned char)*s;
        bool unreserved = (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
                          (ch >= '0' && ch <= '9') ||
                          ch == '-' || ch == '_' || ch == '.' || ch == '~';
        if (unreserved) {
            tzEnc[p++] = (char)ch;
        } else {
            snprintf(tzEnc + p, 4, "%%%02X", ch);
            p += 3;
        }
    }
    tzEnc[p] = '\0';

    char url[WEATHER_URL_MAX];
    int n = snprintf(url, sizeof(url),
        "https://api.open-meteo.com/v1/forecast"
        "?latitude=%.4f&longitude=%.4f"
        "&current=temperature_2m,apparent_temperature,relative_humidity_2m,"
        "wind_speed_10m,weather_code"
        "&daily=weather_code,temperature_2m_max,temperature_2m_min"
        "&timezone=%s&forecast_days=%d",
        (double)lat, (double)lon, tzEnc, (int)WEATHER_FORECAST_DAYS);

    if (n < 0 || n >= (int)sizeof(url)) return;     // staging overflow -> empty
    if ((size_t)n >= cap) return;                   // caller cap too small -> empty
    memcpy(out, url, (size_t)n + 1);
}

} // namespace core
