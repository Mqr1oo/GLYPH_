//file name:glyph_files.ino
//
// Acces la cardul SD de pe telefon: listare si descarcare.
//
// De ce e o masina de stari si nu o simpla bucla: un fisier KML de traseu are
// zeci de kilobytes, iar printr-o notificare BLE incap ~180 de octeti. Trimise
// intr-o bucla, cele cateva sute de notificari ar bloca loop() destule secunde
// cat butoanele sa nu mai raspunda si watchdog-ul sa se supere - exact bug-ul
// pe care tocmai l-am reparat la GPS. Asa ca trimitem cateva bucati la fiecare
// trecere prin loop() si ne intoarcem imediat.
//
// Datele merg in base64 fiindca protocolul catre telefon e pe linii de text,
// iar un KML contine sfarsituri de linie care ar rupe incadrarea.

#include "mbedtls/base64.h"

static File     xferFile;
static bool     xferActive = false;
static uint32_t xferSent   = 0;
static uint32_t xferTotal  = 0;
static String   xferName   = "";

// Numele vine de la telefon. Nu are voie sa iasa din radacina cardului.
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

// Lista completa a cardului, nu doar fisierele KML: telefonul arata si
// jurnalele de mesaje, nu doar traseele.
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

// Chemata din loop(). Trimite cel mult SD_XFER_CHUNKS_PER_LOOP bucati si iese.
void serviceFileTransfer() {
    if (!xferActive) return;

    // Telefonul a plecat la mijlocul transferului: nu are rost sa citim mai departe.
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

        // base64 creste volumul cu o treime, plus terminatorul de sir.
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
