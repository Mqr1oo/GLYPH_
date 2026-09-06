//file name:glyph_files.ino
//
// SD card access from the phone: listing, download, upload, OTA.
//
// Download is a state machine serviced from loop(), not a loop that runs to
// completion. A route KML is tens of kilobytes and a BLE notification carries
// about 180 bytes; sending hundreds of notifications in one pass would block
// loop() for seconds, freezing the buttons and tripping the watchdog. Each
// pass sends a few chunks and returns. Payloads are base64 because the phone
// protocol is line based.

#include "mbedtls/base64.h"

static File     xferFile;
static bool     xferActive = false;
static uint32_t xferSent   = 0;
static uint32_t xferTotal  = 0;
static String   xferName   = "";

// The name comes from the phone. It must not escape the card root.
static bool isSafeFileName(const String &name) {
    if (name.length() == 0 || name.length() > 64) return false;
    if (name.indexOf("..") >= 0) return false;
    for (size_t i = 0; i < name.length(); i++) {
        char c = name[i];
        if (c == '/' || c == '\\' || c < 32) return false;
    }
    return true;
}

void abortFileTransfer(const char *reason) {
    if (xferFile) xferFile.close();
    xferActive = false;
    xferName = "";
    if (reason) notifyPhone(String("SYS_FILE_ERR:") + reason);
}

void sendSDListing() {
    if (!sdDetected) { notifyPhone("SYS_LS_ERR:NO CARD"); return; }

    acquireSD();
    File root = SD.open("/");
    if (!root) { notifyPhone("SYS_LS_ERR:CANNOT OPEN"); return; }

    notifyPhone("SYS_LS_BEGIN"); delay(BLE_NOTIFY_GAP_MS);

    int sent = 0;
    root.rewindDirectory();
    while (sent < SD_LIST_MAX_FILES) {
        File f = root.openNextFile();
        if (!f) break;
        if (!f.isDirectory()) {
            String line = "SYS_LS:" + String(f.name()) + "|" + String((uint32_t)f.size());
            notifyPhone(line);
            delay(BLE_NOTIFY_GAP_MS);
            sent++;
        }
        f.close();
    }
    root.close();

    notifyPhone("SYS_LS_END");
}

void startFileTransfer(const String &rawName) {
    if (xferActive) { abortFileTransfer("BUSY"); }

    if (!sdDetected)            { notifyPhone("SYS_FILE_ERR:NO CARD");  return; }
    if (!isSafeFileName(rawName)) { notifyPhone("SYS_FILE_ERR:BAD NAME"); return; }

    acquireSD();
    String path = "/" + rawName;
    xferFile = SD.open(path, FILE_READ);
    if (!xferFile) { notifyPhone("SYS_FILE_ERR:NOT FOUND"); return; }

    xferTotal = (uint32_t)xferFile.size();
    xferSent  = 0;
    xferName  = rawName;
    xferActive = true;

    notifyPhone("SYS_FILE_BEGIN:" + rawName + "|" + String(xferTotal));
}

void serviceFileTransfer() {
    if (!xferActive) return;

    if (!deviceConnected) { abortFileTransfer(NULL); return; }

    for (int i = 0; i < SD_XFER_CHUNKS_PER_LOOP; i++) {
        if (!xferFile || !xferFile.available()) {
            xferFile.close();
            xferActive = false;
            xferName = "";
            notifyPhone("SYS_FILE_END");
            return;
        }

        acquireSD();
        uint8_t raw[SD_XFER_CHUNK_BYTES];
        int n = xferFile.read(raw, SD_XFER_CHUNK_BYTES);
        if (n <= 0) {
            xferFile.close();
            xferActive = false;
            xferName = "";
            notifyPhone("SYS_FILE_END");
            return;
        }

        // base64 grows the data by a third, plus the string terminator.
        unsigned char b64[((SD_XFER_CHUNK_BYTES + 2) / 3) * 4 + 4];
        size_t written = 0;
        if (mbedtls_base64_encode(b64, sizeof(b64), &written,
                                  raw, (size_t)n) != 0) {
            abortFileTransfer("ENCODE");
            return;
        }
        b64[written] = '\0';

        xferSent += n;
        notifyPhone(String("SYS_FILE_DATA:") + (const char*)b64);
        delay(BLE_NOTIFY_GAP_MS);
    }
}

bool fileTransferBusy() { return xferActive; }

// Upload: every chunk is acknowledged before the phone sends the next one. The
// BLE receive path has a single slot (bleRxBuffer); in a burst one chunk would
// overwrite another before loop() reads it and the file would be corrupt with
// nothing reporting it. One chunk in flight makes that impossible.

static File     upFile;
static bool     upActive   = false;
static uint32_t upExpected = 0;
static uint32_t upGot      = 0;
static String   upName     = "";

void abortUpload(const char *reason) {
    if (upFile) upFile.close();
    // A half written file is worse than none: it lists as valid and opens empty.
    if (upActive && upName.length()) SD.remove("/" + upName);
    upActive = false;
    upName = "";
    if (reason) notifyPhone(String("SYS_PUT_ERR:") + reason);
}

void startUpload(const String &arg) {
    if (upActive) abortUpload(NULL);

    int bar = arg.indexOf('|');
    if (bar <= 0) { notifyPhone("SYS_PUT_ERR:BAD ARGS"); return; }
    String name = arg.substring(0, bar);
    uint32_t size = (uint32_t)arg.substring(bar + 1).toInt();

    if (!sdDetected)              { notifyPhone("SYS_PUT_ERR:NO CARD");  return; }
    if (!isSafeFileName(name))    { notifyPhone("SYS_PUT_ERR:BAD NAME"); return; }
    if (size == 0 || size > SD_UPLOAD_MAX_BYTES) {
        notifyPhone("SYS_PUT_ERR:TOO BIG"); return;
    }

    acquireSD();
    // "w" truncates: re-uploading a route replaces it instead of appending.
    upFile = SD.open("/" + name, FILE_WRITE);
    if (!upFile) { notifyPhone("SYS_PUT_ERR:CANNOT WRITE"); return; }

    upExpected = size;
    upGot = 0;
    upName = name;
    upActive = true;
    notifyPhone("SYS_PUT_READY");
}

void uploadChunk(const String &b64) {
    if (!upActive) { notifyPhone("SYS_PUT_ERR:NOT STARTED"); return; }

    unsigned char raw[SD_UPLOAD_CHUNK_BYTES + 8];
    size_t written = 0;
    if (mbedtls_base64_decode(raw, sizeof(raw), &written,
                              (const unsigned char*)b64.c_str(), b64.length()) != 0) {
        abortUpload("DECODE");
        return;
    }

    acquireSD();
    if (upFile.write(raw, written) != written) { abortUpload("WRITE"); return; }
    upGot += written;

    if (upGot > upExpected) { abortUpload("OVERRUN"); return; }

    notifyPhone("SYS_PUT_ACK:" + String(upGot));
}

void finishUpload() {
    if (!upActive) { notifyPhone("SYS_PUT_ERR:NOT STARTED"); return; }

    upFile.flush();
    upFile.close();

    if (upGot != upExpected) {
        // Something was lost. No file is better than a truncated one.
        SD.remove("/" + upName);
        upActive = false;
        notifyPhone("SYS_PUT_ERR:SHORT");
        return;
    }

    String name = upName;
    upActive = false;
    upName = "";

    scanKMLFiles();

    String lower = name; lower.toLowerCase();
    if (lower.endsWith(".kml")) {
        currentKmlOverlay = "/" + name;
        loadKMLCache();
        currentState = PAGE_MAP;
        fullRefreshNeeded = true;
        requestUIUpdate = true;
    }

    notifyPhone("SYS_PUT_DONE:" + name);
}

bool uploadBusy() { return upActive; }

// OTA uses the same channel, but the target is flash. Writes go to the INACTIVE
// application partition and the switch happens only at the end, after
// Update.end() validates the image. A power or Bluetooth loss midway leaves the
// device booting the old partition.

#include <Update.h>

static bool     otaActive   = false;
static uint32_t otaExpected = 0;
static uint32_t otaGot      = 0;

void abortOta(const char *reason) {
    if (otaActive) Update.abort();
    otaActive = false;
    if (reason) notifyPhone(String("SYS_OTA_ERR:") + reason);
}

void startOta(uint32_t size) {
    if (otaActive) abortOta(NULL);

    if (size < 65536UL) { notifyPhone("SYS_OTA_ERR:TOO SMALL"); return; }

    if (!Update.begin(size, U_FLASH)) {
        notifyPhone("SYS_OTA_ERR:NO ROOM");
        return;
    }

    otaExpected = size;
    otaGot = 0;
    otaActive = true;

    // Standby or a screen redraw must not interrupt the update.
    pingActivity();
    notifyPhone("SYS_OTA_READY");
}

void otaChunk(const String &b64) {
    if (!otaActive) { notifyPhone("SYS_OTA_ERR:NOT STARTED"); return; }

    unsigned char raw[SD_UPLOAD_CHUNK_BYTES + 8];
    size_t written = 0;
    if (mbedtls_base64_decode(raw, sizeof(raw), &written,
                              (const unsigned char*)b64.c_str(), b64.length()) != 0) {
        abortOta("DECODE");
        return;
    }

    if (Update.write(raw, written) != written) { abortOta("WRITE"); return; }
    otaGot += written;
    if (otaGot > otaExpected) { abortOta("OVERRUN"); return; }

    pingActivity();
    notifyPhone("SYS_OTA_ACK:" + String(otaGot));
}

void finishOta() {
    if (!otaActive) { notifyPhone("SYS_OTA_ERR:NOT STARTED"); return; }

    if (otaGot != otaExpected) { abortOta("SHORT"); return; }

    // end(true) activates the new partition. False means an invalid image and
    // nothing is switched.
    if (!Update.end(true)) {
        otaActive = false;
        notifyPhone("SYS_OTA_ERR:INVALID IMAGE");
        return;
    }

    otaActive = false;
    notifyPhone("SYS_OTA_DONE");

    // Let the notification leave over Bluetooth before restarting.
    delay(400);
    ESP.restart();
}

bool otaBusy() { return otaActive; }
