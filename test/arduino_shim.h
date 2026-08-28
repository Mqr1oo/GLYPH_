#pragma once
//
// Un strat subtire care face codul GLYPH compilabil pe PC.
//
// Rostul lui: o buna parte din firmware nu atinge hardware deloc - conversia
// de timp, calculul distantei, parserul KML, construirea si citirea pachetelor
// radio, criptografia. Alea sunt functii pure: primesc date, intorc date.
// Singurul motiv pentru care nu puteau fi testate era ca folosesc tipul String
// din Arduino si cateva functii din nucleu.
//
// Shim-ul asta le ofera. Nu e o emulare de Arduino si nu incearca sa fie -
// contine exact atat cat au nevoie functiile testate.

#include <string>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cmath>
#include <ctime>
#include <functional>

typedef uint8_t byte;

#define PI 3.1415926535897932384626433832795
#define HEX 16

// ---------------------------------------------------------------------------
// String
//
// Aceleasi metode ca in Arduino, cu aceeasi semantica pentru indici in afara
// intervalului (Arduino intoarce sir gol in loc sa arunce exceptie).
// ---------------------------------------------------------------------------
class String {
public:
    std::string s;

    String() {}
    String(const char* c) : s(c ? c : "") {}
    String(const std::string& v) : s(v) {}
    String(char c) : s(1, c) {}

    String(int v)          { char b[24]; snprintf(b, sizeof(b), "%d", v);  s = b; }
    String(unsigned v)     { char b[24]; snprintf(b, sizeof(b), "%u", v);  s = b; }
    String(long v)         { char b[24]; snprintf(b, sizeof(b), "%ld", v); s = b; }
    String(unsigned long v){ char b[24]; snprintf(b, sizeof(b), "%lu", v); s = b; }

    String(byte v, int base) {
        char b[16];
        if (base == HEX) snprintf(b, sizeof(b), "%x", (unsigned)v);
        else             snprintf(b, sizeof(b), "%u", (unsigned)v);
        s = b;
    }
    String(double v, int decimals) {
        char b[64];
        snprintf(b, sizeof(b), "%.*f", decimals, v);
        s = b;
    }

    unsigned int length() const { return (unsigned int)s.size(); }
    const char* c_str() const   { return s.c_str(); }
    void reserve(unsigned n)    { s.reserve(n); }

    char operator[](unsigned i) const { return i < s.size() ? s[i] : 0; }

    String& operator+=(const String& o) { s += o.s; return *this; }
    String& operator+=(const char* o)   { s += (o ? o : ""); return *this; }
    String& operator+=(char c)          { s += c; return *this; }

    bool operator==(const String& o) const { return s == o.s; }
    bool operator!=(const String& o) const { return s != o.s; }
    bool operator==(const char* o)   const { return s == std::string(o ? o : ""); }
    bool operator!=(const char* o)   const { return !(*this == o); }

    String substring(unsigned from) const {
        if (from >= s.size()) return String();
        return String(s.substr(from));
    }
    String substring(unsigned from, unsigned to) const {
        if (from >= s.size() || to <= from) return String();
        if (to > s.size()) to = (unsigned)s.size();
        return String(s.substr(from, to - from));
    }

    int indexOf(char c) const           { auto p = s.find(c); return p == std::string::npos ? -1 : (int)p; }
    int indexOf(char c, unsigned f) const {
        if (f >= s.size()) return -1;
        auto p = s.find(c, f); return p == std::string::npos ? -1 : (int)p;
    }
    int indexOf(const char* t) const    { auto p = s.find(t); return p == std::string::npos ? -1 : (int)p; }
    int lastIndexOf(char c) const       { auto p = s.rfind(c); return p == std::string::npos ? -1 : (int)p; }
    int lastIndexOf(const char* t) const{ auto p = s.rfind(t); return p == std::string::npos ? -1 : (int)p; }

    bool startsWith(const char* t) const { return s.rfind(t, 0) == 0; }
    bool endsWith(const char* t) const {
        std::string x(t);
        return s.size() >= x.size() && s.compare(s.size() - x.size(), x.size(), x) == 0;
    }

    void trim() {
        size_t a = s.find_first_not_of(" \t\r\n");
        if (a == std::string::npos) { s.clear(); return; }
        size_t b = s.find_last_not_of(" \t\r\n");
        s = s.substr(a, b - a + 1);
    }
    void remove(unsigned i)             { if (i < s.size()) s.erase(i); }
    void remove(unsigned i, unsigned n) { if (i < s.size()) s.erase(i, n); }
    void replace(const char* a, const char* b) {
        std::string A(a), B(b);
        if (A.empty()) return;
        size_t p = 0;
        while ((p = s.find(A, p)) != std::string::npos) { s.replace(p, A.size(), B); p += B.size(); }
    }
    void toLowerCase() { for (auto& c : s) c = (char)tolower((unsigned char)c); }

    double toDouble() const { return atof(s.c_str()); }
    float  toFloat()  const { return (float)atof(s.c_str()); }
    long   toInt()    const { return atol(s.c_str()); }

    bool equalsIgnoreCase(const String& o) const {
        if (s.size() != o.s.size()) return false;
        for (size_t i = 0; i < s.size(); i++)
            if (tolower((unsigned char)s[i]) != tolower((unsigned char)o.s[i])) return false;
        return true;
    }
};

inline String operator+(const String& a, const String& b) { String r(a); r += b; return r; }
inline String operator+(const String& a, const char* b)   { String r(a); r += b; return r; }
inline String operator+(const char* a, const String& b)   { String r(a); r += b; return r; }
inline String operator+(const String& a, char b)          { String r(a); r += b; return r; }

// ---------------------------------------------------------------------------
// Functii de nucleu folosite de codul testat
// ---------------------------------------------------------------------------
inline bool isHexadecimalDigit(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

// Pe PC nu exista millis(); testele care au nevoie de timp il controleaza.
extern unsigned long g_fakeMillis;
inline unsigned long millis() { return g_fakeMillis; }

inline uint32_t esp_random() { return (uint32_t)rand() ^ ((uint32_t)rand() << 16); }

#define RTC_DATA_ATTR
