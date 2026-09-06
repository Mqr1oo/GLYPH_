#pragma once
//
// Toate deciziile de produs care erau numere scrise direct in mijlocul codului.
//
// Fiecare valoare de aici raspunde la o intrebare reala - dupa cat timp intra
// aparatul in standby, de la ce distanta consideram ca te-ai miscat, cand
// oprim aparatul ca sa nu corupem cardul. Erau printre cele mai importante
// decizii din proiect si erau ingropate in conditii de tip if.

// ---------------------------------------------------------------------------
// Alimentare si somn
// ---------------------------------------------------------------------------

// Fara activitate si fara miscare atata timp -> ecran de standby.
static const unsigned long STANDBY_AFTER_MS = 240000;   // 4 minute

// Cat timp dupa ultima miscare detectata de IMU consideram aparatul "in uz".
static const unsigned long MOVEMENT_KEEPALIVE_MS = 30000;

// In standby, cat de recenta trebuie sa fie o miscare ca sa trezeasca aparatul.
static const unsigned long STANDBY_WAKE_MOVEMENT_MS = 2000;

// Cat doarme intre trezirile de verificare in modul shutdown.
static const uint32_t DEEP_SLEEP_INTERVAL_MS = 8UL * 60UL * 1000UL;

// Cat sta treaz ca sa verifice daca s-a intamplat ceva, la fiecare trezire.
static const unsigned long WAKE_CHECK_WINDOW_MS = 4000;

// Frecventa CPU pe moduri.
static const uint32_t CPU_MHZ_NORMAL  = 80;
static const uint32_t CPU_MHZ_SAVING  = 40;
static const uint32_t CPU_MHZ_STANDBY = 20;

// ---------------------------------------------------------------------------
// Baterie
//
// Celula 18650 se considera goala pe la 3.0-3.2 V, dar sub sarcina tensiunea
// scade brusc. Oprim la 3.40 V ca sa mai avem rezerva pentru inchiderea
// ordonata a fisierelor - o scriere intrerupta pe SD poate lasa traseul
// trunchiat sau tabela FAT corupta.
// ---------------------------------------------------------------------------
static const float BATTERY_WARN_V     = 3.55f;   // avertisment pe ecran
static const float BATTERY_CUTOFF_V   = 3.40f;   // oprire controlata

// Tensiunea trebuie sa stea sub prag atata timp inainte sa actionam, ca un
// varf de consum (transmisie LoRa) sa nu opreasca aparatul degeaba.
static const unsigned long BATTERY_CUTOFF_HOLD_MS = 30000;

// Cate citiri ADC mediem. ADC-ul ESP32 e zgomotos; o singura citire poate
// sari cu 100 mV.
static const int BATTERY_ADC_SAMPLES = 16;

// Divizorul rezistiv de pe placa. Vbat = Vadc * BATTERY_DIVIDER.
static const float BATTERY_DIVIDER = 2.0f;

// ---------------------------------------------------------------------------
// GPS si inregistrarea traseului
// ---------------------------------------------------------------------------

// Ignoram complet pozitiile cu o eroare estimata mai mare de atat.
static const float GPS_MAX_ACCURACY_M = 100.0f;

// Cat de mult trebuie sa te fi deplasat ca sa inregistram un punct nou,
// atunci cand IMU-ul confirma ca aparatul se misca.
static const double GPS_MOVING_MIN_STEP_M = 2.0;

// Acelasi lucru cand aparatul sta pe loc: pragul e mult mai mare, ca sa nu
// inregistram drift-ul GPS ca pe o plimbare.
static const double GPS_STATIC_MIN_STEP_M = 25.0;

// Cat timp dupa ultima miscare IMU consideram ca aparatul se deplaseaza.
static const unsigned long GPS_IMU_MOVING_WINDOW_MS = 5000;

// Diferenta de pozitie de la care merita redesenat ecranul.
static const double UI_POSITION_REDRAW_DEG = 0.0001;

// Ratele de masurare ale modulului GNSS, pe moduri de putere.
static const uint16_t GPS_RATE_NORMAL_MS  = 1000;
static const uint16_t GPS_RATE_SAVING_MS  = 10000;
static const uint16_t GPS_RATE_STANDBY_MS = 60000;

// ---------------------------------------------------------------------------
// Radio
// ---------------------------------------------------------------------------
static const int   LORA_SPREADING_FACTOR = 11;
static const float LORA_BANDWIDTH_KHZ    = 125.0f;
static const int   LORA_CODING_RATE      = 8;
static const uint8_t LORA_SYNC_WORD      = 0x12;
static const int   LORA_OUTPUT_POWER_DBM = 22;

// Cat dureaza o difuzare SOS si cat asteptam intre pachete.
// --- reteaua mesh ---------------------------------------------------------
// Cate retransmisii are voie sa faca un mesaj. Trei inseamna ca poate trece
// prin trei aparate intermediare - suficient pentru orice echipa realista, si
// destul de putin cat sa nu se transforme intr-o furtuna de retransmisii.
static const uint8_t MESH_HOPS_DEFAULT = 3;

// SOS-ul primeste un salt in plus: e singurul mesaj pentru care merita sa
// platesti timp de emisie ca sa mearga mai departe.
static const uint8_t MESH_HOPS_SOS     = 4;

// Intarzierea dinaintea unei confirmari. Daca trei aparate din echipa aud
// acelasi mesaj si raspund in aceeasi milisecunda, nu se aude niciunul.
static const unsigned long ACK_DELAY_MIN_MS    = 250;
static const unsigned long ACK_DELAY_SPREAD_MS = 900;

// --- bugetul legal de emisie ----------------------------------------------
// In Europa, pe 868 MHz, majoritatea sub-benzilor permit 1% factor de
// utilizare: 36 de secunde de emisie pe ora. Pastram 30 ca margine.
// La SF11 un mesaj sta in aer aproape o secunda, deci limita se atinge mult
// mai repede decat pare - de aceea aparatul o urmareste singur, in loc sa
// depinda de bunul simt al utilizatorului.
static const uint32_t DUTY_CYCLE_BUDGET_MS = 30000;

// --- SOS ------------------------------------------------------------------
// Rafala initiala. Dupa ea, SOS-ul nu se opreste: se repeta la intervale care
// cresc, pana il anulezi. Un SOS de 10 secunde care prinde momentul in care
// nimeni nu asculta e un SOS pierdut.
static const unsigned long SOS_BROADCAST_MS   = 10000;

// Intervalele dintre reluari, in secunde: des la inceput, apoi tot mai rar, ca
// bateria sa tina ore intregi. Ultima valoare se repeta la nesfarsit.
static const uint16_t SOS_REPEAT_SECONDS[] = { 60, 60, 120, 120, 300, 300, 600, 900 };
static const int SOS_REPEAT_STEPS = sizeof(SOS_REPEAT_SECONDS) / sizeof(SOS_REPEAT_SECONDS[0]);
static const unsigned long SOS_PACKET_GAP_MS  = 100;

// ---------------------------------------------------------------------------
// Interfata si stocare
// ---------------------------------------------------------------------------

// E-ink-ul acumuleaza ghosting la refresh partial; o data la atat facem unul complet.
static const unsigned long FULL_REFRESH_INTERVAL_MS = 900000;   // 15 minute

// Cat de des scriem o linie de telemetrie in CSV.
static const unsigned long TELEMETRY_LOG_INTERVAL_MS = 60000;

// Cat de des verificam daca s-a introdus sau scos cardul.
static const unsigned long SD_HOTPLUG_CHECK_MS = 5000;

// Cat de des trimitem telemetrie catre telefon.
static const unsigned long BLE_TELEMETRY_INTERVAL_MS = 5000;

// Pauza intre notificarile BLE consecutive, ca sa nu depasim coada stivei.
static const unsigned long BLE_NOTIFY_GAP_MS = 15;

// --- acces la cardul SD de pe telefon ---------------------------------------
// Cat de mare e o bucata de fisier trimisa printr-o notificare. Dupa base64
// creste cu o treime (160 -> 216 octeti), plus prefixul si cei 3 octeti de
// antet ATT: incape in MTU-ul de 247 cerut la pornire. Daca telefonul nu
// accepta MTU mai mare, stiva imparte singura pachetul.
static const int SD_XFER_CHUNK_BYTES     = 160;

// Bucati trimise la o trecere prin loop(). Mai multe = transfer mai rapid, dar
// loop() sta mai mult si butoanele raspund mai greu. 4 tine apasarile sub ~80ms.
static const int SD_XFER_CHUNKS_PER_LOOP = 4;

// Plafon la listare, ca un card plin sa nu inunde telefonul cu notificari.
static const int SD_LIST_MAX_FILES       = 60;

// MTU cerut la initializarea BLE. Fara el raman 20 de octeti utili per
// notificare si un traseu de 40 KB s-ar descarca in minute, nu in secunde.
static const int BLE_REQUESTED_MTU       = 247;

// --- incarcarea unei rute de pe telefon ---
// Bucata trimisa de telefon, INAINTE de base64. Dupa codare devine 160 de
// caractere; cu prefixul "CMD_PUTD:" ajunge la 169, sub cei 192 ai sertarului
// de receptie BLE (BLE_RX_BUF_SIZE). Nu creste valoarea fara sa cresti si
// sertarul, altfel bucatile ajung taiate si fisierul iese corupt.
static const int SD_UPLOAD_CHUNK_BYTES = 120;

// Plafon la ce accepta aparatul. O ruta desenata are cativa kilobytes; limita
// exista ca o apasare gresita sa nu umple cardul.
static const uint32_t SD_UPLOAD_MAX_BYTES = 262144;

// Cate puncte din traseul curent tinem in memoria RTC (supravietuiesc somnului).
static const int MAX_BREADCRUMBS = 350;

// Cate puncte dintr-un overlay KML tinem in RAM. Peste asta, traseul se
// esantioneaza uniform - mai bine un traseu complet cu mai putine puncte decat
// unul taiat la jumatate.
static const int MAX_KML_CACHE_POINTS = 600;

// Cati coechipieri urmarim simultan.
static const int MAX_TEAMMATES = 5;

// Cate mesaje pastram in istoricul de pe ecran.
static const int MAX_LORA_MSGS = 5;

// Cate fisiere KML listam in managerul de rute.
static const int MAX_KML_FILES = 12;

// Limite de lungime pentru textul introdus de utilizator.
static const unsigned int MAX_MESSAGE_LEN = 30;
static const unsigned int MAX_NAME_LEN    = 16;
static const unsigned int MAX_TEAM_LEN    = 16;

// ---------------------------------------------------------------------------
// Butoane
// ---------------------------------------------------------------------------
static const unsigned long BTN_COMBO_HOLD_MS   = 1000;   // A+B, B+C, A+C
static const unsigned long BTN_LONG_PRESS_MS   = 800;    // apasare lunga A / C
static const unsigned long BTN_DELETE_HOLD_MS  = 600;    // B tinut = stergere
static const unsigned long BTN_DELETE_REPEAT_MS = 150;   // ritmul de stergere
static const unsigned long BTN_KML_EXIT_HOLD_MS = 5000;  // A tinut in lista KML
static const TickType_t    BTN_POLL_PERIOD_MS  = 15;
static const TickType_t    SENSOR_POLL_PERIOD_MS = 100;

// ---------------------------------------------------------------------------
// Compatibilitate cu formatul vechi de pachet
//
// Cat timp e 1, aparatul CITESTE mesaje in formatul AES-CBC de dinaintea
// corecturilor, ca sa poti actualiza unitatile pe rand. Nu TRIMITE niciodata in
// formatul vechi. Dupa ce toate aparatele au firmware nou, pune-l pe 0 - CBC
// nu autentifica nimic, deci fiecare zi in care ramane deschis e o zi in care
// cineva iti poate injecta un mesaj modificat.
// ---------------------------------------------------------------------------
#define GLYPH_ACCEPT_LEGACY_CBC 1
