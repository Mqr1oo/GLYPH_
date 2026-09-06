//file name:glyph_crypto.ino
// Crypto primitives only: string in, string out, no hardware. It includes its
// own headers so it also builds outside the sketch, against the mbedtls the tests use.
#include <string.h>
#include <new>
#include "mbedtls/aes.h"
#include "mbedtls/md.h"
#include "mbedtls/gcm.h"

// Fixed public salt, not a secret: it binds derivation to this application so
// generic precomputed tables do not apply. The 20000 iterations set the guess cost.
static const char* GLYPH_KDF_SALT = "GLYPH-LoRa-KDF-v2";
static const int   GLYPH_KDF_ITERATIONS = 20000;

static const uint8_t GCM_NONCE_LEN = 12;
static const uint8_t GCM_TAG_LEN   = 16;

// PBKDF2 takes ~0.3 s at 80 MHz, so the key is cached until the team name changes.
static byte  cachedKey[32];
static String cachedKeyTeam = "";
static bool   cachedKeyValid = false;

void invalidateTeamKey() {
    cachedKeyValid = false;
}

// Hand-written PBKDF2-HMAC-SHA256, one 32-byte output. mbedtls_pkcs5_pbkdf2_hmac
// is deprecated in mbedtls 3.x and may be missing depending on the ESP32 core.
static int pbkdf2Sha256(const uint8_t* password, size_t passLen,
                        const uint8_t* salt, size_t saltLen,
                        uint32_t iterations, uint8_t* out32) {
    const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (!info) return -1;

    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    if (mbedtls_md_setup(&ctx, info, 1) != 0) {   // 1 = HMAC mode
        mbedtls_md_free(&ctx);
        return -1;
    }

    uint8_t block[32];
    uint8_t work[32];
    int rc = 0;

    const uint8_t counter[4] = { 0, 0, 0, 1 };
    rc |= mbedtls_md_hmac_starts(&ctx, password, passLen);
    rc |= mbedtls_md_hmac_update(&ctx, salt, saltLen);
    rc |= mbedtls_md_hmac_update(&ctx, counter, 4);
    rc |= mbedtls_md_hmac_finish(&ctx, work);
    memcpy(block, work, 32);

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
                                       NULL, 0,
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
String decryptLegacyCBC(String input) {
    if (input.length() < 64) return "";
    if (!isHexString(input)) return "";
    if (((input.length() - 32) / 2) % 16 != 0) return "";

    // The old key was a plain SHA-256 of the team name.
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

// Returns "" if the message is not authentic. Unlike CBC, a message altered in
// flight is rejected here instead of being delivered as corrupted text.
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
    if (rc == 0) {
        for (int i = 0; i < cipherLen; i++) result += (char)output[i];
    }

    delete[] ciphertext;
    delete[] output;
    return result;
}

