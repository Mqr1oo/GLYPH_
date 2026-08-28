//file name:lora_radio.ino
//
// CRIPTOGRAFIE - rescrisa.
//
// Ce era inainte:
//   cheie = SHA-256(nume_echipa), fara salt si fara derivare lenta
//   AES-256-CBC, fara niciun cod de autentificare
//
// Doua probleme reale:
//   1. Fara MAC, cine intercepteaza poate modifica biti din mesaj fara sa fie
//      detectat. Padding-ul PKCS#7 verificat local nu e autentificare - e chiar
//      vectorul clasic de padding oracle.
//   2. SHA-256 direct peste un nume de echipa scurt ("ALPHA") se sparge prin
//      dictionar aproape instantaneu. Un singur hash inseamna miliarde de
//      incercari pe secunda pe un GPU.
//
// Ce e acum:
//   cheie = PBKDF2-HMAC-SHA256(nume_echipa, salt fix, 20000 iteratii)
//   AES-256-GCM: cripteaza SI autentifica, cu tag de 16 octeti
//
// Formatul pe fir:
//   0xFE | hex(nonce 12B) | hex(ciphertext) | hex(tag 16B)     <- nou, GCM
//   0xFF | hex(iv 16B)    | hex(ciphertext)                    <- vechi, CBC
//
// Cat timp GLYPH_ACCEPT_LEGACY_CBC e 1, aparatul inca CITESTE mesajele vechi,
// ca sa poti actualiza aparatele pe rand. Nu mai TRIMITE niciodata in formatul
// vechi. Dupa ce toate aparatele au firmware nou, pune-l pe 0.

#define GLYPH_ACCEPT_LEGACY_CBC 1

#include "mbedtls/gcm.h"

// Salt fix, cunoscut. Nu e secret si nu trebuie sa fie: rolul lui e sa lege
// derivarea de aplicatia asta, ca sa nu poata fi refolosite tabele precalculate
// de hash-uri generice. Costul per incercare il dau cele 20000 de iteratii.
static const char* GLYPH_KDF_SALT = "GLYPH-LoRa-KDF-v2";
static const int   GLYPH_KDF_ITERATIONS = 20000;

static const uint8_t GCM_NONCE_LEN = 12;
static const uint8_t GCM_TAG_LEN   = 16;

// PBKDF2 dureaza ~0.3 s la 80 MHz, deci cheia se calculeaza o data si se
// pastreaza pana cand se schimba numele echipei.
static byte  cachedKey[32];
static String cachedKeyTeam = "";
static bool   cachedKeyValid = false;

void invalidateTeamKey() {
    cachedKeyValid = false;
}

// PBKDF2-HMAC-SHA256, o singura iesire de 32 de octeti (deci un singur bloc).
// Scris de mana in loc de mbedtls_pkcs5_pbkdf2_hmac(), care e marcat deprecated
// in mbedtls 3.x si poate lipsi in functie de versiunea de ESP32 core.
// Foloseste doar mbedtls_md_*, care e stabil in ambele versiuni.
static int pbkdf2Sha256(const uint8_t* password, size_t passLen,
                        const uint8_t* salt, size_t saltLen,
                        uint32_t iterations, uint8_t* out32) {
    const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (!info) return -1;

    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    if (mbedtls_md_setup(&ctx, info, 1) != 0) {   // 1 = mod HMAC
        mbedtls_md_free(&ctx);
        return -1;
    }

    uint8_t block[32];
    uint8_t work[32];
    int rc = 0;

    // U1 = HMAC(password, salt || INT_BE32(1))
    const uint8_t counter[4] = { 0, 0, 0, 1 };
    rc |= mbedtls_md_hmac_starts(&ctx, password, passLen);
    rc |= mbedtls_md_hmac_update(&ctx, salt, saltLen);
    rc |= mbedtls_md_hmac_update(&ctx, counter, 4);
    rc |= mbedtls_md_hmac_finish(&ctx, work);
    memcpy(block, work, 32);

    // Ui = HMAC(password, Ui-1); rezultatul e XOR-ul tuturor.
    for (uint32_t i = 1; i < iterations && rc == 0; i++) {
        rc |= mbedtls_md_hmac_reset(&ctx);
        rc |= mbedtls_md_hmac_update(&ctx, work, 32);
        rc |= mbedtls_md_hmac_finish(&ctx, work);
        for (int j = 0; j < 32; j++) block[j] ^= work[j];
    }

    mbedtls_md_free(&ctx);
    if (rc != 0) return -1;

    memcpy(out32, block, 32);
    return 0;
}

void getAESKey(byte* key) {
    String k = (myTeam.length() > 0) ? myTeam : "ALPHA";

    if (cachedKeyValid && cachedKeyTeam == k) {
        memcpy(key, cachedKey, 32);
        return;
    }

    int rc = pbkdf2Sha256((const uint8_t*)k.c_str(), k.length(),
                          (const uint8_t*)GLYPH_KDF_SALT, strlen(GLYPH_KDF_SALT),
                          GLYPH_KDF_ITERATIONS, key);

    if (rc == 0) {
        memcpy(cachedKey, key, 32);
        cachedKeyTeam = k;
        cachedKeyValid = true;
    } else {
        memset(key, 0, 32);
    }
}

String bufferToHex(const byte* data, int len) {
    String res = "";
    res.reserve(len * 2);
    for (int i = 0; i < len; i++) {
        if (data[i] < 0x10) res += "0";
        res += String(data[i], HEX);
    }
    return res;
}

void hexToBuffer(String hex, byte* output) {
    for (unsigned int i = 0; i + 1 < hex.length(); i += 2) {
        output[i / 2] = (byte)strtol(hex.substring(i, i + 2).c_str(), NULL, 16);
    }
}

static bool isHexString(const String& s) {
    if (s.length() == 0 || s.length() % 2 != 0) return false;
    for (unsigned int i = 0; i < s.length(); i++) {
        if (!isHexadecimalDigit(s[i])) return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// AES-256-GCM
// ---------------------------------------------------------------------------
String cryptMsg(String input) {
    byte key[32];
    getAESKey(key);

    int len = input.length();
    if (len == 0) return "";

    byte nonce[GCM_NONCE_LEN];
    for (int i = 0; i < GCM_NONCE_LEN; i++) nonce[i] = esp_random() & 0xFF;

    byte* output = new (std::nothrow) byte[len];
    if (!output) return "";

    byte tag[GCM_TAG_LEN];

    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);

    int rc = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key, 256);
    if (rc == 0) {
        rc = mbedtls_gcm_crypt_and_tag(&gcm, MBEDTLS_GCM_ENCRYPT, len,
                                       nonce, GCM_NONCE_LEN,
                                       NULL, 0,                       // fara AAD
                                       (const unsigned char*)input.c_str(),
                                       output, GCM_TAG_LEN, tag);
    }
    mbedtls_gcm_free(&gcm);

    String result = "";
    if (rc == 0) {
        result = bufferToHex(nonce, GCM_NONCE_LEN)
               + bufferToHex(output, len)
               + bufferToHex(tag, GCM_TAG_LEN);
    }

    delete[] output;
    return result;
}

#if GLYPH_ACCEPT_LEGACY_CBC
// Doar pentru a citi mesaje de la aparate care inca au firmware vechi.
static String decryptLegacyCBC(String input) {
    if (input.length() < 64) return "";
    if (!isHexString(input)) return "";
    if (((input.length() - 32) / 2) % 16 != 0) return "";

    // Cheia veche era SHA-256 simplu peste numele echipei.
    byte key[32];
    String k = (myTeam.length() > 0) ? myTeam : "ALPHA";
    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 0);
    mbedtls_md_starts(&ctx);
    mbedtls_md_update(&ctx, (const unsigned char*)k.c_str(), k.length());
    mbedtls_md_finish(&ctx, key);
    mbedtls_md_free(&ctx);

    byte iv[16];
    hexToBuffer(input.substring(0, 32), iv);

    int cipherLen = (input.length() - 32) / 2;
    byte* ciphertext = new (std::nothrow) byte[cipherLen];
    byte* output     = new (std::nothrow) byte[cipherLen];
    if (!ciphertext || !output) {
        delete[] ciphertext; delete[] output;
        return "";
    }
    hexToBuffer(input.substring(32), ciphertext);

    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    mbedtls_aes_setkey_dec(&aes, key, 256);
    int rc = mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_DECRYPT, cipherLen, iv, ciphertext, output);
    mbedtls_aes_free(&aes);

    String result = "";
    if (rc == 0) {
        int padLen = output[cipherLen - 1];
        if (padLen > 0 && padLen <= 16 && padLen <= cipherLen) {
            for (int i = 0; i < cipherLen - padLen; i++) result += (char)output[i];
        }
    }

    delete[] ciphertext;
    delete[] output;
    return result;
}
#endif

// Intoarce "" daca mesajul nu e autentic. Spre deosebire de CBC, aici un mesaj
// modificat pe drum e respins, nu livrat ca text alterat.
String decryptMsg(String input) {
    unsigned int minLen = (GCM_NONCE_LEN + GCM_TAG_LEN) * 2;
    if (input.length() <= minLen) return "";
    if (!isHexString(input)) return "";

    int cipherLen = (input.length() - minLen) / 2;
    if (cipherLen <= 0) return "";

    byte key[32];
    getAESKey(key);

    byte nonce[GCM_NONCE_LEN];
    byte tag[GCM_TAG_LEN];
    hexToBuffer(input.substring(0, GCM_NONCE_LEN * 2), nonce);
    hexToBuffer(input.substring(input.length() - GCM_TAG_LEN * 2), tag);

    byte* ciphertext = new (std::nothrow) byte[cipherLen];
    byte* output     = new (std::nothrow) byte[cipherLen];
    if (!ciphertext || !output) {
        delete[] ciphertext; delete[] output;
        return "";
    }
    hexToBuffer(input.substring(GCM_NONCE_LEN * 2, input.length() - GCM_TAG_LEN * 2), ciphertext);

    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);

    int rc = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key, 256);
    if (rc == 0) {
        rc = mbedtls_gcm_auth_decrypt(&gcm, cipherLen,
                                      nonce, GCM_NONCE_LEN,
                                      NULL, 0,
                                      tag, GCM_TAG_LEN,
                                      ciphertext, output);
    }
    mbedtls_gcm_free(&gcm);

    String result = "";
    if (rc == 0) {                       // rc != 0 => tag invalid, mesaj respins
        for (int i = 0; i < cipherLen; i++) result += (char)output[i];
    }

    delete[] ciphertext;
    delete[] output;
    return result;
}

// Dispecer: alege formatul dupa primul octet al pachetului.
String decryptPacket(byte marker, String payload) {
    if (marker == 0xFE) return decryptMsg(payload);
#if GLYPH_ACCEPT_LEGACY_CBC
    if (marker == 0xFF) return decryptLegacyCBC(payload);
#endif
    return "";
}

// ---------------------------------------------------------------------------
void executeSendMsg() {
    if (msgDraft.length() == 0 || currentPowerMode == STEALTH_MODE) return;

    double lat, lon;
    getGpsPosition(lat, lon);

    String fullMsg = myName + ": " + msgDraft;
    String msgData = fullMsg + "|" + String(lat, 5) + "," + String(lon, 5);

    String msgToSend = "";
    if (secureMode) {
        String ct = cryptMsg(msgData);
        if (ct.length() == 0) {          // criptarea a esuat: nu trimitem in clar
            notifyPhone("[SYS] Encryption failed, message not sent");
            return;
        }
        msgToSend += (char)0xFE;
        msgToSend += ct;
    } else {
        msgToSend = msgData;
    }

    radio.standby();
    radio.setSpreadingFactor(11);
    radio.transmit(msgToSend);

    String screenMsg = ">> [" + formatLocalTime() + "] " + fullMsg;
    pushLoraHistory(screenMsg);
    notifyPhone("[TX]: " + fullMsg);

    radio.startReceive();
    loraListening = true;

    msgDraft = "";
    currentState = PAGE_LORA;
    fullRefreshNeeded = false;
    requestUIUpdate = true;
    pingActivity();
}
