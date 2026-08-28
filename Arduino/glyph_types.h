#pragma once
//
// Tipurile proprii ale proiectului stau intr-un header, nu in .ino.
//
// Motiv: builder-ul Arduino genereaza automat prototipuri pentru toate functiile
// din fisierele .ino si le insereaza imediat dupa blocul de #include din sketch.
// Daca o functie are in semnatura un enum definit mai jos in .ino (de exemplu
// applyPowerMode(PowerMode)), prototipul generat apare INAINTE de definitia
// enum-ului si compilarea esueaza cu un mesaj greu de legat de cauza.
// Definit intr-un header inclus sus, tipul exista deja cand se insereaza
// prototipurile.

#include <Arduino.h>
#include <functional>

enum SystemState {
    PAGE_INIT_LANG,
    PAGE_INIT_FREQ,
    PAGE_INIT_NAME,
    PAGE_INIT_TIME,
    PAGE_MAP,
    PAGE_KML_LIST,
    PAGE_CLOCK,
    PAGE_LORA,
    PAGE_KEYBOARD,
    PAGE_MENU_MAIN,
    PAGE_MENU_TEAM,
    PAGE_TEAM_NAME_EDIT,
    PAGE_MENU_POWER,
    PAGE_MENU_LANG,
    PAGE_MENU_TIME,
    STANDBY_MODE
};

enum PowerMode { NORMAL_MODE, ECO_MODE, STEALTH_MODE };

struct GeoPoint { float lat; float lon; };

struct Teammate {
    String name;
    double lat;
    double lon;
    unsigned long lastSeen;
};

// Callback pentru parcurgerea punctelor dintr-un fisier KML.
// newSegment e true la primul punct dintr-un bloc <coordinates> nou.
typedef std::function<void(float lat, float lon, bool newSegment)> KmlPointFn;
