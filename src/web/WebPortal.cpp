// ===========================================================================
//  WebPortal.cpp  —  local upload website + JSON API
// ===========================================================================
#include "WebPortal.h"
#include "config.h"
#include "core/PathValidation.h"

#include <WiFi.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>

bool WebPortal::begin() {
    // SoftAP is always up so the site is reachable with no router present.
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(AP_SSID, AP_PASSWORD);

    // Optionally also join the home network if credentials were supplied.
#if defined(WIFI_STA_SSID) && defined(WIFI_STA_PASS)
    WiFi.begin(WIFI_STA_SSID, WIFI_STA_PASS);
    for (int i = 0; i < 20 && WiFi.status() != WL_CONNECTED; i++) delay(250);
#endif

    if (MDNS.begin(WEB_HOSTNAME)) MDNS.addService("http", "tcp", WEB_PORT);

    _server = new AsyncWebServer(WEB_PORT);
    registerRoutes();
    _server->begin();
    _running = true;

    Serial.printf("[Web] portal up  AP:%s  http://%s.local  (%s)\n",
                  WiFi.softAPIP().toString().c_str(), WEB_HOSTNAME,
                  ipAddress().c_str());
    return true;
}

void WebPortal::stop() {
    if (_server) { _server->end(); delete _server; _server = nullptr; }
    MDNS.end();
    WiFi.mode(WIFI_OFF);
    _running = false;
    Serial.println("[Web] portal stopped (power saving)");
}

String WebPortal::ipAddress() const {
    if (WiFi.status() == WL_CONNECTED) return WiFi.localIP().toString();
    return WiFi.softAPIP().toString();
}

void WebPortal::registerRoutes() {
    // --- Static UI from LittleFS -----------------------------------------
    _server->serveStatic("/", LittleFS, "/www/")
            .setDefaultFile("index.html");

    // --- GET /api/books  ->  library listing -----------------------------
    _server->on("/api/books", HTTP_GET, [this](AsyncWebServerRequest *req) {
        JsonDocument doc;
        JsonArray arr = doc["books"].to<JsonArray>();
        for (auto &b : _storage.listBooks()) {
            JsonObject o = arr.add<JsonObject>();
            o["name"] = b.name;
            o["path"] = b.path;
            o["size"] = b.size;
        }
        doc["storage"] = _storage.usingSD() ? "sd" : "flash";
        JsonObject sp = doc["space"].to<JsonObject>();
        sp["total"] = _storage.totalBytes();
        sp["used"]  = _storage.usedBytes();
        sp["free"]  = _storage.freeBytes();
        String out; serializeJson(doc, out);
        req->send(200, "application/json", out);
    });

    // --- POST /api/upload  ->  streamed .txt upload ----------------------
    _server->on("/api/upload", HTTP_POST,
        [](AsyncWebServerRequest *req) { req->send(200, "application/json", "{\"ok\":true}"); },
        [this](AsyncWebServerRequest *req, String filename, size_t index,
               uint8_t *data, size_t len, bool final) {
            static File up;
            if (index == 0) {
                Serial.printf("[Web] upload start: %s\n", filename.c_str());
                up = _storage.createForUpload(filename);
            }
            if (up) up.write(data, len);
            if (final && up) {
                up.close();
                Serial.printf("[Web] upload done: %u bytes\n",
                              (unsigned)(index + len));
            }
        });

    // --- POST /api/delete?path=/books/foo.txt ----------------------------
    _server->on("/api/delete", HTTP_POST, [this](AsyncWebServerRequest *req) {
        if (!req->hasParam("path")) { req->send(400); return; }
        String path = req->getParam("path")->value();
        // C1: only allow deleting a .txt directly under BOOKS_DIR -- never
        // /bookmarks.json, /www/*, ".." traversal or (on SD) an absolute path.
        if (!core::isBookPath(path.c_str(), BOOKS_DIR)) {
            req->send(400, "application/json", "{\"ok\":false,\"error\":\"invalid path\"}");
            return;
        }
        bool ok = _storage.remove(path);
        req->send(ok ? 200 : 500, "application/json",
                  ok ? "{\"ok\":true}" : "{\"ok\":false}");
    });

    // --- GET /api/bookmarks?path=/books/foo.txt --------------------------
    _server->on("/api/bookmarks", HTTP_GET, [this](AsyncWebServerRequest *req) {
        if (!req->hasParam("path")) { req->send(400); return; }
        String path = req->getParam("path")->value();
        if (!core::isBookPath(path.c_str(), BOOKS_DIR)) {   // S5
            req->send(400, "application/json", "{\"error\":\"invalid path\"}");
            return;
        }
        JsonDocument doc;
        JsonArray arr = doc["bookmarks"].to<JsonArray>();
        for (auto &m : _bookmarks.listBookmarks(path)) {
            JsonObject o = arr.add<JsonObject>();
            o["offset"] = m.offset;
            o["label"]  = m.label;
        }
        doc["lastPosition"] = _bookmarks.getLastPosition(path);
        String out; serializeJson(doc, out);
        req->send(200, "application/json", out);
    });

    // --- POST /api/bookmark  ->  add a named bookmark --------------------
    _server->on("/api/bookmark", HTTP_POST, [this](AsyncWebServerRequest *req) {
        if (!req->hasParam("path", true) || !req->hasParam("offset", true)) {
            req->send(400); return;
        }
        String path   = req->getParam("path", true)->value();
        if (!core::isBookPath(path.c_str(), BOOKS_DIR)) {   // S5
            req->send(400, "application/json", "{\"ok\":false,\"error\":\"invalid path\"}");
            return;
        }
        uint32_t off  = req->getParam("offset", true)->value().toInt();
        String label  = req->hasParam("label", true)
                          ? req->getParam("label", true)->value() : "Bookmark";
        _bookmarks.addBookmark(path, off, label);
        _bookmarks.save();
        req->send(200, "application/json", "{\"ok\":true}");
    });

    _server->onNotFound([](AsyncWebServerRequest *req) {
        req->send(404, "text/plain", "Not found");
    });
}
