// ===========================================================================
//  BookStorage.cpp
// ===========================================================================
#include "BookStorage.h"
#include "config.h"

#include <LittleFS.h>
#include <SD.h>
#include <SPI.h>

static SPIClass g_sdSPI(HSPI);

bool BookStorage::begin() {
    // 1) Try the microSD card first (best for large libraries).
    g_sdSPI.begin(SD_SCLK, SD_MISO, SD_MOSI, SD_CS);
    if (SD.begin(SD_CS, g_sdSPI)) {
        _usingSD = true;
        if (!SD.exists(BOOKS_DIR)) SD.mkdir(BOOKS_DIR);
        Serial.println("[Storage] using microSD");
        return true;
    }

    // 2) Fall back to internal LittleFS.
    if (!LittleFS.begin(true)) {
        Serial.println("[Storage] LittleFS mount failed");
        return false;
    }
    _usingSD = false;
    if (!LittleFS.exists(BOOKS_DIR)) LittleFS.mkdir(BOOKS_DIR);
    Serial.println("[Storage] using internal LittleFS");
    return true;
}

fs::FS &BookStorage::fs() {
    return _usingSD ? (fs::FS &)SD : (fs::FS &)LittleFS;
}

std::vector<BookInfo> BookStorage::listBooks() {
    std::vector<BookInfo> out;
    File dir = fs().open(BOOKS_DIR);
    if (!dir || !dir.isDirectory()) return out;

    for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
        String path = String(f.name());
        if (!path.startsWith("/")) path = String(BOOKS_DIR) + "/" + path;
        String lower = path; lower.toLowerCase();
        if (lower.endsWith(".txt")) {
            BookInfo b;
            b.path = path;
            b.size = f.size();
            int slash = path.lastIndexOf('/');
            b.name = path.substring(slash + 1);
            b.name = b.name.substring(0, b.name.length() - 4); // strip .txt
            out.push_back(b);
        }
        f.close();
    }
    dir.close();
    return out;
}

bool BookStorage::exists(const String &path) { return fs().exists(path); }

File BookStorage::open(const String &path, const char *mode) {
    return fs().open(path, mode);
}

File BookStorage::createForUpload(const String &fileName) {
    String clean = fileName;
    int slash = clean.lastIndexOf('/');
    if (slash >= 0) clean = clean.substring(slash + 1);          // no path traversal
    if (!clean.endsWith(".txt")) clean += ".txt";
    String path = String(BOOKS_DIR) + "/" + clean;
    return fs().open(path, "w");
}

bool BookStorage::remove(const String &path) { return fs().remove(path); }

uint32_t BookStorage::fileSize(const String &path) {
    File f = fs().open(path, "r");
    if (!f) return 0;
    uint32_t s = f.size();
    f.close();
    return s;
}

String BookStorage::readChunk(const String &path, uint32_t offset, uint32_t maxBytes) {
    File f = fs().open(path, "r");
    if (!f) return String();
    if (offset) f.seek(offset);
    String out;
    out.reserve(maxBytes + 1);
    uint32_t read = 0;
    while (f.available() && read < maxBytes) {
        out += (char)f.read();
        read++;
    }
    f.close();
    return out;
}
