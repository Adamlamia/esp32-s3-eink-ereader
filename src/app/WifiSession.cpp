// ===========================================================================
//  WifiSession.cpp  --  shared STA Wi-Fi + NTP + Wi-Fi-off (WTH-R2)
// ===========================================================================
//  Implementation of the shared radio-lifecycle facility declared in
//  WifiSession.h, extracted verbatim from the near-verbatim parallels that
//  CalendarSync.cpp and WeatherSync.cpp each carried (the two WTH-R2 review
//  markers). The trampoline (wifiSessionRunOnTask) is a header-only template;
//  only the Wi-Fi-off guard and the STA+NTP plumbing live here.
//
//  Clock convention (unchanged from the calendar's live-verified behaviour):
//  configTime()'s gmtOffset only steers localtime(); once NTP has fixed the
//  clock, time(nullptr) returns TRUE UTC epoch seconds directly - used with NO
//  offset subtraction. The NTP-validity floor is the shared CAL_CLOCK_MIN_EPOCH
//  from config.h (deliberately not duplicated here).
// ===========================================================================
#include "app/WifiSession.h"
#include "core/CalendarEvent.h"   // CAL_CLOCK_MIN_EPOCH + CAL_TZ_OFFSET_SEC (config.h)

#include <Arduino.h>
#include <WiFi.h>
#include <time.h>

// --- RAII Wi-Fi-off guard ---------------------------------------------------
WifiOffGuard::~WifiOffGuard() { WiFi.mode(WIFI_OFF); }

// --- STA + NTP plumbing (shared by calendar + weather) ----------------------
#if defined(WIFI_STA_SSID) && defined(WIFI_STA_PASS)
bool wifiSessionStaNtp(int64_t &nowUtc, char *msg, size_t msgLen, const char *tag) {
    if (!tag) tag = "[Sync]";

    // --- STA-only Wi-Fi ---
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_STA_SSID, WIFI_STA_PASS);
    Serial.printf("%s joining Wi-Fi (STA)...\n", tag);
    int tries = 0;
    while (WiFi.status() != WL_CONNECTED && tries++ < WIFI_SESSION_WIFI_TRIES) delay(500);
    if (WiFi.status() != WL_CONNECTED) {
        if (msg) snprintf(msg, msgLen, "Wi-Fi connect failed");
        Serial.printf("%s Wi-Fi connect failed (status %d)\n", tag, WiFi.status());
        return false;
    }
    Serial.printf("%s Wi-Fi up, IP %s\n", tag, WiFi.localIP().toString().c_str());

    // --- NTP time sync (fixed UTC+8, no DST - Malaysia) ---
    configTime(CAL_TZ_OFFSET_SEC, 0, "pool.ntp.org", "time.nist.gov");
    time_t nowT = 0;
    for (int i = 0; i < WIFI_SESSION_NTP_TRIES; i++) {
        nowT = time(nullptr);
        if ((int64_t)nowT > CAL_CLOCK_MIN_EPOCH) break;
        delay(500);
    }
    if ((int64_t)nowT <= CAL_CLOCK_MIN_EPOCH) {
        if (msg) snprintf(msg, msgLen, "NTP time sync failed");
        Serial.printf("%s NTP gave no valid time\n", tag);
        return false;
    }
    nowUtc = (int64_t)nowT;   // time() is already TRUE UTC (no offset subtraction)
    return true;
}
#endif // WIFI_STA_SSID && WIFI_STA_PASS
