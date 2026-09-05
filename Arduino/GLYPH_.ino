#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <sys/time.h> 
#include <math.h>
#include <new>
#include "driver/gpio.h" 
#include "esp_sleep.h"
#include <Preferences.h> 
#include <WiFi.h>  
#include "mbedtls/aes.h"
#include "mbedtls/md.h"    

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

#define Module Modulino_Module_Fix
#include <Modulino.h>
#undef Module

#include <Button2.h> 
#include <RadioLib.h> 
#include <SD.h>
#include <GxEPD.h>
#include <GxDEPG0213BN/GxDEPG0213BN.h> 
#include <GxIO/GxIO_SPI/GxIO_SPI.h>
#include <U8g2_for_Adafruit_GFX.h>
#include <SparkFun_u-blox_GNSS_Arduino_Library.h>
#include <Adafruit_SHT4x.h>
#include <Adafruit_LSM6DSOX.h>
#include <Adafruit_LIS3MDL.h>

#include "glyph_types.h"

const String OS_VERSION = "10.08.26"; 

#define PIN_BAT_ADC        1   
#define BTN_PIN            0   
#define RADIO_POW_PIN      35   

#define SDCARD_MOSI        11
#define SDCARD_SCLK        14
#define SDCARD_MISO        2
#define SDCARD_CS          13

#define EDP_CS             15
#define EDP_DC             16
#define EDP_RSET           47
#define EDP_BUSY           48

#define RADIO_SCLK         5
#define RADIO_MISO         3
#define RADIO_MOSI         6
#define RADIO_CS           7
#define RADIO_DIO1         33
#define RADIO_RST          8
#define RADIO_BUSY         34

#define I2C_SDA            43
#define I2C_SCL            44

bool qwiic_ok = false;
bool buttons_ok = false;
bool sht_ok = false;
bool imu_ok = false;
bool mag_ok = false;
bool gps_ok = false;
bool sdDetected = false;

// Ce merge si ce nu. Definit aici fiindca sketch-ul principal e concatenat
// primul de builder-ul Arduino, deci simbolul trebuie sa existe de la inceput.
DeviceHealth health;

TaskHandle_t buttonTaskHandle;
TaskHandle_t sensorTaskHandle;
SemaphoreHandle_t i2cMutex;

volatile bool raw_A = false, raw_B = false, raw_C = false;
volatile bool latched_pA = false, latched_pB = false, latched_pC = false;
volatile bool new_gps_data = false;

volatile bool virt_pA = false;
volatile bool virt_pB = false;
volatile bool virt_pC = false;
volatile bool virt_longA = false;
volatile bool virt_longB = false;
volatile bool virt_longC = false;
volatile bool virt_comboAB = false;
volatile bool virt_comboAC = false;
volatile bool virt_comboBC = false;

RTC_DATA_ATTR SystemState currentState = PAGE_INIT_LANG; 

bool requestUIUpdate = false; 
bool fullRefreshNeeded = true; 
unsigned long lastFullRefreshTime = 0; 
RTC_DATA_ATTR int mapZoomLevel = 1; 
bool simActive = false;

String currentKmlOverlay = ""; 
String kmlFiles[MAX_KML_FILES];
int kmlFileCount = 0;
int kmlSelectionIndex = 0;

RTC_DATA_ATTR bool isRecording = false; 
RTC_DATA_ATTR char currentRecordDate[32] = ""; 
RTC_DATA_ATTR bool secureMode = true; 
RTC_DATA_ATTR int currentScreenRotation = 3; 
RTC_DATA_ATTR double sessionTotalDistance = 0.0; 

RTC_DATA_ATTR float currentTemp = 0.0;
RTC_DATA_ATTR float currentHum = 0.0;
RTC_DATA_ATTR float currentAx = 0.0;
RTC_DATA_ATTR float currentAy = 0.0;
RTC_DATA_ATTR float currentAz = 0.0;


RTC_DATA_ATTR float currentHeading = 0.0; 
RTC_DATA_ATTR float currentSpeed = 0.0;

RTC_DATA_ATTR bool gpsTimeValid = false;
RTC_DATA_ATTR uint16_t gpsYear = 0;
RTC_DATA_ATTR uint8_t gpsMonth = 0, gpsDay = 0, gpsHour = 0, gpsMinute = 0, gpsSecond = 0;

float kmlCacheMinLat = 90, kmlCacheMaxLat = -90, kmlCacheMinLon = 180, kmlCacheMaxLon = -180;
float kmlCacheDistance = 0.0;
bool kmlCacheValid = false;

const String KML_CLOSING = "    </coordinates></LineString></Placemark>\n  </Document>\n</kml>\n";

int lastRenderedMinute = -1;
RTC_DATA_ATTR double lastRenderedLat = 0.0;
RTC_DATA_ATTR double lastRenderedLon = 0.0;
RTC_DATA_ATTR double lastLoggedLat = 0.0; 
RTC_DATA_ATTR double lastLoggedLon = 0.0;

Teammate teammates[MAX_TEAMMATES];
int teammateCount = 0;

RTC_DATA_ATTR unsigned long sessionActivityTime = 0;
RTC_DATA_ATTR bool inDeepSleepMode = false;
unsigned long lastSensorCheck = 0;
volatile unsigned long lastPhysicalMovement = 0; 

Preferences prefs;
RTC_DATA_ATTR int currentLang = 1; 
RTC_DATA_ATTR float currentFreq = 868.0; 
RTC_DATA_ATTR int timeOffset = 2; 
RTC_DATA_ATTR bool useAmPmFormat = false; 
String myName = ""; 
String myTeam = "ALPHA"; 

RTC_DATA_ATTR PowerMode currentPowerMode = NORMAL_MODE;
int mainMenuItem = 0; 
int menuSelection = 0; 
int tempLangSelection = 0; 
int tempTimeSelection = 2; 

// FIX: exista 11 limbi, dar meniurile permiteau doar indicii 0..9,
// deci a 11-a limba (Zhongwen) era inaccesibila din interfata.
const int LANG_COUNT = 11;
const char* langNames[11] = { "Romana", "English", "Espanol", "Francais", "Deutsch", "Italiano", "Portugues", "Polski", "Nederlands", "Turkce", "Zhongwen" };
const char* tr_map[11] = { "HARTA TACTICA", "TACTICAL MAP", "MAPA TACTICO", "CARTE TACTIQUE", "TAKTIK-KARTE", "MAPPA TATTICA", "MAPA TATICO", "MAPA TAKTYCZNA", "KAART", "HARITA", "ZHAN SHU DI TU" };
const char* tr_clock[11] = { "CEAS & SENZORI", "CLOCK & SENSORS", "RELOJ Y SENS.", "HORLOGE & CAPT.", "UHR & SENSOREN", "OROLOGIO & SENS.", "RELOGIO & SENS.", "ZEGAR & CZUJNIKI", "KLOK & SENS.", "SAAT & SENS.", "SHIZHONG & SENS" };
const char* tr_radio[11] = { "MESAGERIE", "MESSENGER", "MENSAJERO", "MESSAGERIE", "NACHRICHTEN", "MESSAGGERIA", "MENSAGEIRO", "KOMUNIKATOR", "MESSENGER", "MESAJLASMA", "TONG XIN" };
const char* tr_team[11] = { "ECHIPA", "TEAM", "EQUIPO", "EQUIPE", "TEAM", "SQUADRA", "EQUIPE", "DRUZYNA", "TEAM", "TAKIM", "TUAN DUI" };
const char* tr_power[11] = { "MOD SISTEM", "SYSTEM MODE", "SISTEMA", "SYSTEME", "SYSTEM", "SISTEMA", "SISTEMA", "SYSTEM", "SYSTEEM", "SISTEM", "XI TONG" };
const char* tr_lang[11] = { "LIMBA", "LANGUAGE", "IDIOMA", "LANGUE", "SPRACHE", "LINGUA", "IDIOMA", "JEZYK", "TAAL", "DIL", "YU YAN" };
const char* tr_gps[11] = { "LIPSA SEMNAL GPS", "NO GPS SIGNAL", "SIN SENAL GPS", "PAS DE SIGNAL GPS", "KEIN GPS-SIGNAL", "NESSUN SEGNALE GPS", "SEM SINAL GPS", "BRAK SYGNALU GPS", "GEEN GPS-SIGNAAL", "GPS SINYALI YOK", "WU GPS XIN HAO" };
const char* tr_sel_region[11] = { "SELECTEAZA REGIUNEA:", "SELECT REGION:", "SELECCIONE REGION:", "CHOISIR REGION:", "REGION WAEHLEN:", "SELEZIONA REGIONE:", "SELECIONAR REGIAO:", "WYBIERZ REGION:", "KIES REGIO:", "BOLGE SECIN:", "XUAN ZE QU YU:" };
const char* tr_set_username[11] = { "SETATI USERNAME", "SET USERNAME", "ASIGNAR USERNAME", "DEFINIR USERNAME", "USERNAME EINGEBEN", "IMPOSTA USERNAME", "DEFINIR USERNAME", "USTAW USERNAME", "STEL USERNAME IN", "USERNAME AYARLA", "SHE ZHI YONG HU" };
const char* tr_use_phone_only[11] = { "FOLOSESTE TELEFONUL", "USE YOUR PHONE", "USA TU TELEFONO", "UTILISEZ TELEPHONE", "NUTZE DEIN HANDY", "USA IL TELEFONO", "USE O TELEFONE", "UZYJ TELEFONU", "GEBRUIK TELEFOON", "TELEFONU KULLAN", "SHI YONG SHOU JI" };
const char* tr_or_serial[11] = { "SAU WEB SERIAL", "OR WEB SERIAL", "O WEB SERIAL", "OU WEB SERIAL", "ODER WEB SERIAL", "O WEB SERIAL", "OU WEB SERIAL", "LUB WEB SERIAL", "OF WEB SERIAL", "VEYA WEB SERIAL", "HUO WEB SERIAL" };
const char* tr_set_utc[11] = { "SETATI FUS ORAR", "SET UTC OFFSET", "AJUSTAR UTC", "REGLER UTC", "UTC OFFSET EINGEBEN", "IMPOSTA UTC", "AJUSTAR UTC", "USTAW UTC", "STEL UTC IN", "UTC AYARLA", "SHE ZHI UTC" };
const char* tr_utc_hint[11] = { "A: - | C: + | B: SAVE", "A: - | C: + | B: SAVE", "A: - | C: + | B: GUARDA", "A: - | C: + | B: SAUVER", "A:- | C:+ | B:SPEICHERN", "A: - | C: + | B: SALVA", "A: - | C: + | B: SALVAR", "A: - | C: + | B: ZAPISZ", "A: - | C: + | B: BEWAAR", "A: - | C: + | B: KAYDET", "A: - | C: + | B: BAO CUN" };
const char* tr_welcome[11] = { "BUN VENIT", "WELCOME", "BIENVENIDO", "BIENVENUE", "WILLKOMMEN", "BENVENUTO", "BEM-VINDO", "WITAJ", "WELKOM", "HOSGELDINIZ", "HUAN YING" };
const char* tr_lang_hint1[11] = { "A: SUS/INAPOI | C: JOS", "A: UP/BACK | C: DOWN", "A: ATRAS | C: BAJAR", "A: RETOUR | C: DEFILER", "A: ZURUCK | C: SCROLL", "A: INDIETRO | C: SCORRI", "A: VOLTAR | C: ROLAR", "A: WSTECZ | C: W DOL", "A: TERUG | C: OMLAAG", "A: GERI | C: ASAGI", "A: SHANG | C: XIA" };
const char* tr_lang_hint2[11] = { "B: SELECTIE", "B: SELECT", "B: ELEGIR", "B: CHOISIR", "B: AUSWAHL", "B: SELEZIONA", "B: SELECIONAR", "B: WYBRAC", "B: KIEZEN", "B: SECMEK", "B: XUAN ZE" };
const char* tr_compose[11] = { "COMPUNE", "COMPOSE", "REDACTAR", "COMPOSER", "VERFASSEN", "COMPONI", "COMPOR", "UTWORZ", "OPSTELLEN", "YAZ", "BIAN XIE" };
const char* tr_team_name[11] = { "NUME ECHIPA", "TEAM NAME", "NOMBRE EQUIPO", "NOM EQUIPE", "TEAMNAME", "NOME SQUADRA", "NOME EQUIPE", "NAZWA DRUZYNY", "TEAMNAAM", "TAKIM ADI", "TUAN DUI MING" };
const char* tr_team_dash[11] = { "RADAR ECHIPA", "TEAM RADAR", "RADAR EQUIPO", "RADAR EQUIPE", "TEAM RADAR", "RADAR SQUADRA", "RADAR EQUIPE", "RADAR DRUZYNY", "TEAM RADAR", "TAKIM RADARI", "TUAN DUI LEI DA" };
const char* tr_change_name[11] = { "B: SCHIMBA | A: INAPOI", "B: CHANGE | A: BACK", "B: CAMBIAR | A: ATRAS", "B: CHANGER | A: RETOUR", "B: ANDERN | A: ZURUCK", "B: CAMBIA | A: INDIETRO", "B: MUDAR | A: VOLTAR", "B: ZMIEN | A: WSTECZ", "B: WIJZIGEN | A: TERUG", "B: DEGISTIR | A: GERI", "B: GENG GAI | A: FAN HUI" };
const char* tr_no_team[11] = { "FARA DATE ECHIPA", "NO TEAM DATA", "SIN DATOS EQUIPO", "AUCUNE DONNEE", "KEINE TEAMDATEN", "NESSUN DATO", "SEM DADOS EQUIPE", "BRAK DANYCH", "GEEN TEAMDATA", "TAKIM VERISI YOK", "WU TUAN DUI SHU JU" };
const char* tr_secure_rx[11] = { "SECURE", "SECURE", "SEGURO", "SECURISE", "SICHER", "SICURO", "SEGURO", "BEZPIECZNY", "VEILIG", "GUVENLI", "AN QUAN" };
const char* tr_public_rx[11] = { "PUBLIC", "PUBLIC", "PUBLICO", "PUBLIC", "OFFENTLICH", "PUBBLICO", "PUBLICO", "PUBLICZNY", "OPENBAAR", "GENEL", "GONG KAI" };
const char* tr_no_msg[11] = { "NICIUN MESAJ", "NO MESSAGES YET", "SIN MENSAJES", "AUCUN MESSAGE", "KEINE NACHRICHTEN", "NESSUN MESSAGGIO", "SEM MENSAGENS", "BRAK WIADOMOSCI", "GEEN BERICHTEN", "MESAJ YOK", "ZAN WU XIAO XI" };
const char* tr_mode_sec[11] = { "C: PRIVAT", "C: SECURE", "C: SEGURO", "C: PRIVE", "C: PRIVAT", "C: PRIVATO", "C: PRIVADO", "C: TAJNE", "C: PRIVE", "C: GIZLI", "C: SI MI" };
const char* tr_mode_pub[11] = { "C: PUBLIC", "C: PUBLIC", "C: PUBLICO", "C: PUBLIC", "C: OFFENTLICH", "C: PUBBLICO", "C: PUBLICO", "C: PUBLICZNY", "C: OPENBAAR", "C: GENEL", "C: GONG KAI" };
const char* tr_write[11] = { "B: SCRIE", "B: WRITE", "B: ESCRIBIR", "B: ECRIRE", "B: SCHREIBEN", "B: SCRIVI", "B: ESCREVER", "B: PISZ", "B: SCHRIJF", "B: YAZ", "B: XIE XIAO XI" };
const char* tr_sensors_title[11] = { "SENZORI", "SENSORS", "SENSORES", "CAPTEURS", "SENSOREN", "SENSORI", "SENSORES", "CZUJNIKI", "SENSOREN", "SENSORLER", "CHUAN GAN QI" };
const char* tr_route_start[11] = { "TRASEU INCEPUT", "ROUTE STARTED", "RUTA INICIADA", "ROUTE DEMARREE", "ROUTE GESTARTET", "PERCORSO INIZIATO", "ROTA INICIADA", "TRASA ROZPOCZETA", "ROUTE GESTART", "ROTA BASLADI", "LU XIAN KAI SHI" };
const char* tr_route_stop[11] = { "TRASEU OPRIT", "ROUTE STOPPED", "RUTA DETENIDA", "ROUTE ARRETEE", "ROUTE GESTOPPT", "PERCORSO FERMATO", "ROTA PARADA", "TRASA ZATRZYMANA", "ROUTE GESTOPT", "ROTA DURDURULDU", "LU XIAN TING ZHI" };
const char* tr_rebooting[11] = { "REPORNIRE...", "REBOOTING...", "REINICIANDO...", "REDEMARRAGE...", "NEUSTART...", "RIAVVIO...", "REINICIANDO...", "PONOWNE URUCH...", "HERSTARTEN...", "YENIDEN BASLIYOR...", "CHONG QI ZHONG..." };
const char* tr_batt_empty[11] = { "BATERIE DESCARCATA", "BATTERY EMPTY", "BATERIA VACIA", "BATTERIE VIDE", "AKKU LEER", "BATTERIA SCARICA", "BATERIA VAZIA", "BATERIA ROZLADOWANA", "BATTERIJ LEEG", "PIL BITTI", "DIAN CHI MEI DIAN" };
const char* tr_batt_low[11] = { "BATERIE SCAZUTA", "LOW BATTERY", "BATERIA BAJA", "BATTERIE FAIBLE", "AKKU SCHWACH", "BATTERIA BASSA", "BATERIA FRACA", "NISKI POZIOM BATERII", "BATTERIJ BIJNA LEEG", "PIL AZ", "DIAN LIANG DI" };
const char* tr_saving[11] = { "SE SALVEAZA...", "SAVING...", "GUARDANDO...", "SAUVEGARDE...", "SPEICHERN...", "SALVATAGGIO...", "SALVANDO...", "ZAPISYWANIE...", "OPSLAAN...", "KAYDEDILIYOR...", "BAO CUN ZHONG..." };
const char* tr_standby[11] = { "STANDBY", "STANDBY", "EN ESPERA", "VEILLE", "STANDBY", "STANDBY", "EM ESPERA", "CZUWANIE", "STANDBY", "BEKLEME", "DAI JI" };
const char* tr_use_phone[11] = { "Tel. Keyboard Activ", "Phone keyboard active", "Teclado tel. activo", "Clavier tel. actif", "Handy-Tastatur aktiv", "Tastiera tel. attiva", "Teclado tel. ativo", "Klawiatura tel. akt.", "Telefoon toetsenbord", "Telefon klavyesi aktif", "Shou ji jian pan" };

const char* tr_calib_title[11] = { "CALIBRARE BUSOLA", "COMPASS CALIB", "CALIBRAR BRUJULA", "CALIBRER BOUSSOLE", "KOMPASS KALIB", "CALIBRA BUSSOLA", "CALIBRAR BUSSOLA", "KALIBRACJA KOMPASU", "KOMPAS KALIB.", "PUSULA KALIB.", "ZHI NAN ZHEN XIAO ZHUN" };
const char* tr_calib_msg[11] = { "ROTITI APARATUL 360", "SPIN DEVICE 360", "GIRE EL DISP. 360", "TOURNEZ A 360", "UM 360 GRAD DREHEN", "RUOTA DI 360 GRADI", "GIRE O DISP. 360", "OBROC O 360", "DRAAI 360 GRADEN", "360 DERECE DONDUR", "XUAN ZHUAN 360" };
const char* tr_calib_ok[11] = { "CALIBRAT! APASA B", "CALIBRATED! PRESS B", "LISTO! PULSE B", "TERMINE! PRESSEZ B", "FERTIG! DRUCKE B", "PRONTO! PREMI B", "PRONTO! APERTE B", "GOTOWE! WCISNIJ B", "KLAAR! DRUK B", "HAZIR! B BASIN", "XIAO ZHUN WAN CHENG! AN B" };

const char* tr_prompt_name[11] = { "Utilizator:", "User:", "Usuario:", "Utilisateur:", "Benutzer:", "Utente:", "Usuario:", "Uzytkownik:", "Gebruiker:", "Kullanici:", "Yonghu:" };
const char* tr_prompt_time[11] = { "UTC (+2/-5):", "UTC (+2/-5):", "UTC (+2/-5):", "UTC (+2/-5):", "UTC (+2/-5):", "UTC (+2/-5):", "UTC (+2/-5):", "UTC (+2/-5):", "UTC (+2/-5):", "UTC (+2/-5):", "UTC (+2/-5):" };
const char* tr_prompt_msg[11] = { "Mesaj:", "Message:", "Mensaje:", "Message:", "Nachricht:", "Messaggio:", "Mensagem:", "Wiadomosc:", "Bericht:", "Mesaj:", "Xiao xi:" };
const char* tr_prompt_team[11] = { "Echipa:", "Team:", "Equipo:", "Equipe:", "Team:", "Squadra:", "Equipe:", "Druzyna:", "Team:", "Takim:", "Tuan dui:" };

String msgDraft = "";
String teamDraft = "";
const char* kbChars = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.?!@<"; 
int kbCursor = 0;

String loraHistory[MAX_LORA_MSGS];
int loraMsgCount = 0;
bool loraListening = false;

bool promptNeedsResend = false;
bool introMode = false;
unsigned long phoneConnectedTime = 0;

BLEServer *pServer = NULL;
BLECharacteristic * pTxCharacteristic;
bool deviceConnected = false;
bool oldDeviceConnected = false;

// FIX: inainte era un String Arduino scris in interiorul unei sectiuni critice
// (portENTER_CRITICAL). Atribuirea unui String face malloc/free, iar malloc cu
// intreruperile dezactivate poate bloca sau da crash pe ESP32. Acum e un buffer
// static, deci in sectiunea critica se face doar un memcpy.
#define BLE_RX_BUF_SIZE 192
volatile char bleRxBuffer[BLE_RX_BUF_SIZE] = {0};
volatile bool bleDataReceived = false;
portMUX_TYPE bleMux = portMUX_INITIALIZER_UNLOCKED;

RTC_DATA_ATTR GeoPoint breadcrumbs[MAX_BREADCRUMBS];     
RTC_DATA_ATTR int breadcrumbIdx = 0;

SPIClass SDSPI(HSPI); 
GxIO_Class io(SDSPI, EDP_CS, EDP_DC, EDP_RSET);
GxEPD_Class display(io, EDP_RSET, EDP_BUSY);
U8G2_FOR_ADAFRUIT_GFX u8g2Fonts;

Adafruit_SHT4x sht40 = Adafruit_SHT4x();
Adafruit_LSM6DSOX imu; 
Adafruit_LIS3MDL mag;
SFE_UBLOX_GNSS myGNSS; 
ModulinoButtons mButtons; 
SX1262 radio = new Module(RADIO_CS, RADIO_DIO1, RADIO_RST, RADIO_BUSY);

float batVoltage = 0.0;
RTC_DATA_ATTR double lastLat = 0, lastLon = 0; 
RTC_DATA_ATTR float lastAlt = 0;
RTC_DATA_ATTR byte lastSIV = 0;
RTC_DATA_ATTR float lastAccuracy = 0.0; 
bool hasGpsFix = false; 

void initBLE();
void checkSerialInput();
void handleButtons();
void evaluateSleep();
void checkBLEInput();
void sendTelemetryBLE();

// Cardul SD, vazut de pe telefon (glyph_files.ino)
void sendSDListing();
void startFileTransfer(const String &rawName);
void abortFileTransfer(const char *reason);
void serviceFileTransfer();
bool fileTransferBusy();
void updateUI();
void sendInputPrompt(String msg);
void logGPS(double lat, double lon, float alt);
void pingActivity();
String decryptMsg(String input);
void logLoraMessage(String msg, bool secure);
void checkSDHotplug();
void logSystemData(String type);
void buttonTask(void * pvParameters);
void sensorTask(void * pvParameters);
int getBatteryPercent();
void sampleBattery();
bool batteryLow();
void checkBatteryCutoff();
void stopRecordingSafely();
void showShutdownNotice(const char* line);
String healthBadge();
bool inSetupPage();
double calculateDistance(double lat1, double lon1, double lat2, double lon2);
void enterDeepSleep8Min(bool showUI);
void cancelSosBroadcast(bool byUser);
void acquireSD();
String cryptMsg(String input);
String decryptMsg(String input);
void getAESKey(byte* key);
void applyRadioProfile();
int beginRadio(float freq);
void applyPowerMode(PowerMode mode);
void upsertTeammate(const String &name, double lat, double lon, uint32_t counter);
GlyphMessage parsePacket(const String &raw);
String buildPacket(uint8_t msgType, const String &payload, bool secure);
bool isReplay(const String &sender, uint32_t counter, bool authentic);
String decryptLegacyCBC(String input);
void serviceSosBroadcast();
void startSosBroadcast();
bool sosActive();
void pushLoraHistory(String screenMsg);
void setTeamName(String name);
void invalidateTeamKey();
void setGpsPosition(double lat, double lon, float alt);
void getGpsPosition(double &lat, double &lon, float &alt);
void getGpsPosition(double &lat, double &lon);
void getLocalDateTime(int &year, int &month, int &day, int &hour, int &minute, int &second);
String formatLocalTime();
String formatLocalDateFile();
String formatLocalTimeSec();

void setup() {
    Serial.begin(115200); 
    Serial.println("\n[GLYPH OS] Boot initialization...");
    WiFi.mode(WIFI_OFF);    
    
    inDeepSleepMode = false;

    i2cMutex = xSemaphoreCreateMutex();

    initBLE();

    pinMode(EDP_CS, OUTPUT); digitalWrite(EDP_CS, HIGH);
    pinMode(SDCARD_CS, OUTPUT); digitalWrite(SDCARD_CS, HIGH);
    pinMode(RADIO_CS, OUTPUT); digitalWrite(RADIO_CS, HIGH);
    pinMode(RADIO_POW_PIN, OUTPUT); digitalWrite(RADIO_POW_PIN, HIGH); 
    pinMode(RADIO_DIO1, INPUT); 
    
    Wire.setPins(I2C_SDA, I2C_SCL); 
    Wire.begin(); 
    Wire.setClock(400000);
    for (byte i = 8; i < 120; i++) {
        Wire.beginTransmission(i);
        if (Wire.endTransmission() == 0) {
            qwiic_ok = true;
            break;
        }
    }

    prefs.begin("glyph_os", false); 

    if (qwiic_ok) {
        Modulino.begin(); 
        mButtons.begin();
        buttons_ok = true;
        
        sht_ok = sht40.begin(); 
        if(sht_ok) {
            sht40.setPrecision(SHT4X_HIGH_PRECISION);
            sht40.setHeater(SHT4X_NO_HEATER);
        }
        
        imu_ok = imu.begin_I2C(); 
    } else {
        buttons_ok = false;
        sht_ok = false;
        imu_ok = false;
        mag_ok = false;
    }

    gps_ok = myGNSS.begin();
    if(gps_ok) {
        myGNSS.setI2COutput(COM_TYPE_UBX);
        // FIX: autoPVT ON -> modulul impinge singur NAV-PVT si getPVT() nu mai
        // blocheaza task-ul asteptand raspunsul (cauza principala a scroll-ului greoi).
        myGNSS.setAutoPVT(true);
    }
    
    SPI.begin(RADIO_SCLK, RADIO_MISO, RADIO_MOSI); 
    SDSPI.begin(SDCARD_SCLK, SDCARD_MISO, SDCARD_MOSI, SDCARD_CS);
    
    display.init(0); 
    display.setRotation(currentScreenRotation); 
    u8g2Fonts.begin(display);
    
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    
    if (cause == ESP_SLEEP_WAKEUP_TIMER) {
        if (currentPowerMode == ECO_MODE && gps_ok && myGNSS.getGnssFixOk()) {
            logGPS((double)myGNSS.getLatitude()/1e7, (double)myGNSS.getLongitude()/1e7, myGNSS.getAltitudeMSL()/1e3);
        }

        if (!quickCheckActivity()) {
            esp_sleep_enable_timer_wakeup(5 * 60 * 1000000ULL);
            esp_sleep_enable_ext1_wakeup(1ULL << BTN_PIN, ESP_EXT1_WAKEUP_ALL_LOW);
            esp_deep_sleep_start(); 
        }
        currentState = PAGE_MENU_MAIN;
    }
    
    if (cause == ESP_SLEEP_WAKEUP_EXT1) {
        currentState = PAGE_MENU_MAIN;
    }
    esp_sleep_enable_ext1_wakeup(1ULL << BTN_PIN, ESP_EXT1_WAKEUP_ALL_LOW);
    
    // Media peste mai multe citiri, cu calibrarea din eFuse.
    // Inainte: o singura analogRead() cruda, zgomotoasa si neliniara.
    sampleBattery();
    pingActivity();
    
    String savedVersion = prefs.getString("version", "");
    currentLang = prefs.getInt("lang", 1); 
    currentFreq = prefs.getFloat("freq", 868.0); 
    timeOffset = prefs.getInt("gmt", 2); 
    useAmPmFormat = prefs.getBool("ampm", false); 
    tempTimeSelection = timeOffset;
    myName = prefs.getString("name", ""); 
    myTeam = prefs.getString("team", "ALPHA"); 
    
    if(currentPowerMode != NORMAL_MODE) setCpuFrequencyMhz(CPU_MHZ_SAVING);
    else setCpuFrequencyMhz(CPU_MHZ_NORMAL);

    int radioState = beginRadio(currentFreq);
    if (radioState != RADIOLIB_ERR_NONE) {
        Serial.printf("[GLYPH OS] RADIO INIT FAILED, code %d\n", radioState);
    } else {
        radio.standby();
    }

    // Adunam ce merge si ce nu, o singura data, ca sa putem arata utilizatorului
    // un aparat "degradat" in loc sa-l lasam sa creada ca totul e in regula.
    health.gpsOk     = gps_ok;
    health.imuOk     = imu_ok;
    health.shtOk     = sht_ok;
    health.buttonsOk = buttons_ok;
    checkSDHotplug();
    health.sdOk = sdDetected;

    if (savedVersion != OS_VERSION) { 
        currentLang = 1; 
        tempLangSelection = 1; 
        currentState = PAGE_INIT_LANG; 
    } 
    else if (myName == "") { 
        currentState = PAGE_INIT_NAME; 
        sendInputPrompt(tr_prompt_name[currentLang]); 
    } 
    else {
        if(currentState == PAGE_INIT_LANG || currentState == PAGE_INIT_FREQ || currentState == PAGE_INIT_NAME || currentState == PAGE_INIT_TIME ) {
            currentState = PAGE_MENU_MAIN; 
        }
        applyPowerMode(currentPowerMode);
    }

    // FIX: doua task-uri separate. Butoanele nu mai stau in spatele GPS-ului.
    xTaskCreatePinnedToCore(buttonTask, "BTN_Task", 4096, NULL, 4, &buttonTaskHandle, 0);
    xTaskCreatePinnedToCore(sensorTask, "SENS_Task", 8192, NULL, 1, &sensorTaskHandle, 0);

    updateUI();
}

void loop() {
    checkSerialInput(); 
    handleButtons(); 
    serviceSosBroadcast();   // difuzarea SOS avanseaza cate un pachet pe tura
    evaluateSleep(); 
    checkBLEInput(); 
    sendTelemetryBLE(); 

    // Trimite urmatoarele cateva bucati dintr-un fisier cerut de telefon, daca
    // exista un transfer in curs. Se intoarce imediat cand nu e nimic de facut.
    serviceFileTransfer(); 

    if (promptNeedsResend && deviceConnected && !introMode) {
        promptNeedsResend = false;
        if (currentState == PAGE_INIT_NAME) notifyPhone(">> " + String(tr_prompt_name[currentLang]));
        else if (currentState == PAGE_INIT_TIME) notifyPhone(">> " + String(tr_prompt_time[currentLang]));
        else if (currentState == PAGE_KEYBOARD) notifyPhone(">> " + String(tr_prompt_msg[currentLang]));
        else if (currentState == PAGE_TEAM_NAME_EDIT) notifyPhone(">> " + String(tr_prompt_team[currentLang]));
    }

    if (loraListening && currentPowerMode != STEALTH_MODE && digitalRead(RADIO_DIO1) == HIGH) {
        String raw;
        int state = radio.readData(raw);

        if (state == RADIOLIB_ERR_NONE && raw.length() > 0) {
            GlyphMessage msg = parsePacket(raw);

            // Filtrul de mod: in modul securizat aratam doar mesaje autentificate,
            // in modul public doar pe cele necriptate.
            bool modeMatches = (secureMode == msg.authentic);

            if (msg.valid && modeMatches && !isReplay(msg.sender, msg.counter, msg.authentic)) {

                // RADARUL DE ECHIPA acceptă doar mesaje care au trecut prin
                // decriptare GCM reusita - adica doar de la cine are cheia echipei.
                //
                // Inainte, coordonatele din ORICE mesaj populau tabelul, inclusiv
                // in modul public unde nu exista criptare deloc. Oricine in raza de
                // 10 km putea injecta cinci "coechipieri" cu pozitii inventate si,
                // fiindca tabelul are cinci sloturi cu evacuarea celui mai vechi,
                // iti putea scoate oamenii reali de pe ecran.
                if (msg.authentic && msg.hasCoords && msg.sender.length() > 0) {
                    upsertTeammate(msg.sender, msg.lat, msg.lon, msg.counter);
                }

                String prefix = (msg.type == MSG_SOS) ? "SOS " : "";
                String screenMsg = "[" + formatLocalTime() + "] " + prefix + msg.body;
                pushLoraHistory(screenMsg);

                logLoraMessage(screenMsg, msg.authentic);
                notifyPhone(screenMsg);
                requestUIUpdate = true;
                fullRefreshNeeded = false;
            }
        }
        radio.startReceive();
    }

    if (millis() - lastSensorCheck > 1000) {
        lastSensorCheck = millis(); 
        
        static unsigned long lastSdCheck = 0;
        if (millis() - lastSdCheck > SD_HOTPLUG_CHECK_MS) {
            lastSdCheck = millis(); 
            bool wasSdDetected = sdDetected; 
            checkSDHotplug(); 
            health.sdOk = sdDetected;
            if (wasSdDetected != sdDetected) { 
                requestUIUpdate = true; 
                fullRefreshNeeded = false; 
            } 
        }

        if (new_gps_data || simActive) {
            new_gps_data = false;

            double gLat, gLon; float gAlt;
            getGpsPosition(gLat, gLon, gAlt);
            logGPS(gLat, gLon, gAlt);

            if (fabs(gLat - lastRenderedLat) > UI_POSITION_REDRAW_DEG ||
                fabs(gLon - lastRenderedLon) > UI_POSITION_REDRAW_DEG) {
                lastRenderedLat = gLat;
                lastRenderedLon = gLon;
                requestUIUpdate = true;
            }
        }
        
        if (currentState != STANDBY_MODE && gpsTimeValid) { 
            int currentMin = gpsMinute; 
            if (lastRenderedMinute == -1) {
                lastRenderedMinute = currentMin; 
            } else if (currentMin != lastRenderedMinute) { 
                lastRenderedMinute = currentMin; 
                requestUIUpdate = true; 
            } 
        }
        sampleBattery();
        checkBatteryCutoff();

        static unsigned long lastSensorLog = 0;
        if (sdDetected && (millis() - lastSensorLog > TELEMETRY_LOG_INTERVAL_MS)) {
            lastSensorLog = millis(); 
            logSystemData("Auto_Log"); 
        }
    }
    
    if (requestUIUpdate && currentState != STANDBY_MODE) { 
        requestUIUpdate = false; 
        updateUI(); 
    }
    
    vTaskDelay(10 / portTICK_PERIOD_MS);
}

// Mutata in core_helpers.ino ca formatLocalTime(), care roteste corect si data.
