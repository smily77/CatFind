//1 - Wlan initialisierung und Multicast routinen
//2 - Unicast routinen
//3 - it definitions array
//4 - OTA
//5 - Time
//6 - Pixel & PrintOut
//7 - Koordinatenumrechnug
//8 - Measurement ammendet
//9 - LD06 Lidar
//10 - Button für ucData
//11 - Union fur mcDataStruct HB & HB proc & periodeForHB & aut eintragen der IP & armFire für uc und sendUc prüft ob IP = 0
//1.01 - Udp Text, schalter
//1.x - 2 HostName in device dB
//6_3 - Fixer Header + variabler Payload:
//      * msgHeader (version, sender, msgCode, payloadLen, timeStamp) wird beim Senden automatisch gefüllt
//      * pro Nachrichtenart ein eigener Payload-Struct (posPayload, hbPayload, pa2HbPayload, radarHbPayload, markerHbPayload, cmdPayload)
//      * der alte Union mcDataStruct und ucDataStruct entfallen - Kommandos laufen als commandMsg im selben Format
//      * Broadcast (Multicast) und Unicast verwenden dasselbe Nachrichtenformat (broadcastMsg / unicastMsg)
//      * Pakete werden über Version + payloadLen validiert, getPayload() liest typsicher aus
//      * Simulator in der device dB (Sim)

#include <Streaming.h>
#include <WiFi.h>
#include <AsyncUDP.h>
#include <ArduinoOTA.h>
#include "time.h"
#include <Preferences.h>
#include <LittleFS.h>
#include <Credentials.h>

// Relative Koordinatengruppen: ein Aktor und die fest mit ihm verbundenen
// Sensoren bilden eine Gruppe und teilen sich dasselbe lokale (sensor-/aktor-
// bezogene) Koordinatensystem. group 0 = keiner Gruppe zugeordnet.
#define groupNone  0
#define groupPA2   1   // PA2i + CompactDome + LD06
#define groupPA1_1 2   // PA1_1 + MiniDome
#define testGroup  3   // zum Testen

// Erfassungsbereich eines Sensors, NOMINELL aus dem Datenblatt (weder exakt noch
// konstant — weiter weg kann der Winkel abnehmen, kleine Katzen tragen weniger weit).
// Trotzdem wichtig fuers Erkennungsmodell: Eintritt/Austritt am RAND des
// Erfassungsbereichs unterscheidet die Katze (laeuft hinein/hinaus) vom Vogel
// (taucht mitten im Feld auf/ab). Winkel in GRAD relativ zur Blickrichtung
// (lokale +y-Achse, vgl. toPol: phi = atan2(x,y)), links negativ, rechts positiv.
// covRangeMm = 0 -> Geraet ist kein Sensor (kein Erfassungsbereich).
// HLK-Radare: -60/+60 Grad, 7 m. 360-Grad-Lidare: -180/+180 Grad, 12 m.
// Der VPS parst diese Tabelle direkt aus dem Repo (dashboard.py parse_xcomdef) und
// legt die Sektoren ueber die per poseReport gemeldeten Welt-Posen in die Karte.
struct stationDefinitions {
  byte   type;
  byte   IP;
  byte   MAC;
  byte   group;   // relative Koordinatengruppe (groupNone/groupPA2/groupPA1_1/testGroup)
  String Name;
  int16_t  covLeftDeg;    // linker Erfassungswinkel (Grad, negativ)
  int16_t  covRightDeg;   // rechter Erfassungswinkel (Grad, positiv)
  uint16_t covRangeMm;    // Reichweite (mm); 0 = kein Sensor
};
//Device
#define Manager 0
#define Dome 1
#define MiniDome 2  // Ohne Pixel
#define CompactDome 3
#define PA2i 4
#define Disp7 5
#define CYD 6
#define LD06 7
#define Schalter 8
#define Disp5 9
#define Core2 10
#define Tab5 11
#define CYD35Z 12
#define Wave7z 13
#define Sim 14
#define LaserMarker 15
#define PA1_1 16
#define LidarC1 17
#define CatIdent 18   // Cat Identifier: Erkennungsmodell in Echtzeit (catmodel-Port), sendet catDetected
//Types
#define MananagementDevice 1
#define HLK 2
#define PowerActor 3
#define Screen 4
#define Controller 5
#define Lidar 6
#define onOffSchalter 7
#define Marker 8
#define Detector 9   // Erkennungsgeraet: konsumiert catObserved, produziert catDetected
// Anzahl Eintraege in device[] - ueberall statt hartem "18"/"19" verwenden
#define deviceCount 19
//  {MananagementDevice,180,0x01},
stationDefinitions device[19] = {
  {MananagementDevice,180,0x01,groupNone, "Manager_Dev", 0,0,0},        //Manager
  {HLK,0,0,testGroup,  "Dome",         -60, 60, 7000},                  //Dome
  {HLK,0,0,groupPA1_1, "Mini_Dome",    -60, 60, 7000},                  //MiniDome  -> Gruppe PA1_1
  {HLK,0,0,groupPA2,   "Compact_Dome", -60, 60, 7000},                  //CompactDome - auf PA M5PicoDome -> Gruppe PA2
  {PowerActor,181,0x02,groupPA2, "PowerActor1", 0,0,0},                 //PA1 (PA2i) -> Gruppe PA2
  {Screen,0,0,groupNone, "Disp_7", 0,0,0},                              //Display 7 Inch
  {Controller,0,0,groupNone, "CYD", 0,0,0},                             //CYD Controller
  {Lidar,0,0,groupPA2, "LD6",         -180, 180, 12000},                //LD06 -> Gruppe PA2
  {onOffSchalter,0,0,groupNone, "Button", 0,0,0},                       //Schalter - achtung nicht Unique
  {Screen,0,0,groupNone, "Disp_5", 0,0,0},                              //Display 5 Inch
  {Screen,0,0,groupNone, "Core2", 0,0,0},                               //Core2
  {Screen,0,0,groupNone, "Tab5", 0,0,0},                                //Tab5
  {Screen,0,0,groupNone, "CYD35Zoll", 0,0,0},                           //CYD35Zoll
  {Screen,0,0,groupNone, "Wavetec_7inch", 0,0,0},                       //Wavetec
  {MananagementDevice,0,0,groupNone, "Simulator", 0,0,0},               //Simulator (Cardputer)
  {Marker,182,0x03,groupNone, "Laser_Marker", 0,0,0},                   //LaserMarker (ESP32-C3, feste IP .182)
  {PowerActor,183,0x04,groupPA1_1, "PowerActor1_1", 0,0,0},             //PA1_1 (älterer PA mit Stepper/PCF8574/A4988, feste IP .183) -> Gruppe PA1_1
  {Lidar,0,0,groupNone, "LidarC1",    -180, 180, 12000},                //LidarC1 (RPLidar C1, welt-fähig via VPS-Lokalisierung, DHCP)
  {Detector,184,0x05,groupNone, "Cat_Identifier", 0,0,0}                //CatIdent: Echtzeit-Katzenerkennung auf dem Bus (feste IP .184), Modellparameter vom VPS
};

// call -> device[ident].type

//---------------------------------------------------------------------------------------
// Protokoll 6.3: jede Nachricht = msgHeader + payloadLen Bytes Payload
//---------------------------------------------------------------------------------------
#define XCOM_VERSION 0x63

//action - msgCode
#define HB          1
#define catObserved 2
#define measurement 3
#define catHit      4
#define commandMsg  5
#define poseRequest 6   // Anfrage an ein Geraet: "melde deine Welt-Pose" (ohne Payload)
#define poseReport  7   // Antwort/Annonce: worldPosePayload (validWorldPose + Welt-Pose)
#define mapRequest  8   // Anfrage an den Manager: "sende Karte vom Typ X" (mapReqPayload)
#define mapInfo     9   // Antwort: Metadaten der Karte (mapInfoPayload)
#define mapChunk   10   // ein Datenstueck der Karte (mapChunkMeta + Datenbytes, variabel)
#define settingsRequest 11   // "melde deine Einstellungen" (ohne Payload; Broadcast=alle, Unicast=eines)
#define settingsReport  12   // Antwort/Annonce: settingsPayload (supported/values/actions)
#define catDetected     13   // Erkennungsmodell hat eine KATZE bestaetigt (catDetectedPayload) -
                             // ausgeloest BEVOR die Katze den Erfassungsbereich verlaesst, damit
                             // Aktoren noch reagieren koennen. (Modell heute auf dem VPS, spaeter ESP32.)

// Karten-Typen (fuer mapRequest/mapInfo/mapChunk)
#define mapNoShot   1   // Schusszonen-/No-Shot-Karte (innerhalb = Feuern erlaubt)
#define mapRasen    2   // RasenKarte (Rasen-Umriss, Welt-mm): Sensoren mit gueltiger Welt-Pose
                        // melden nur Ziele INNERHALB (filtert Nachbargrundstueck/Strasse).
                        // Quelle ist Map/RasenKarte.csv (Meter) -> Seed im Manager in mm.

struct __attribute__((packed)) msgHeader {
  uint8_t version;     // XCOM_VERSION - Empfänger verwirft fremde Versionen
  uint8_t sender;      // Geräte-ID (device dB Index)
  uint8_t msgCode;
  uint8_t payloadLen;  // Länge des Payloads in Bytes (0 erlaubt)
  int64_t timeStamp;   // time_t des Senders
};

constexpr uint8_t maxPayloadLen = 64;

// Empfangs-/Sende-Container: Header + Payload-Puffer (auf der Leitung nur payloadLen Bytes)
struct xMsg {
  msgHeader header;
  uint8_t   payload[maxPayloadLen];
};

//----------------------------------- Payloads -------------------------------------------
// catObserved / measurement / catHit
// x/y/radius/angle = RELATIVE Koordinaten (sensor-/aktor-lokal, siehe Koordinaten-
// systeme). worldX/worldY = WELT-Koordinaten (immer kartesisch, mm) - nur gueltig,
// wenn worldValid==1 (dann hatte der Sender beim Senden eine validWorldPose).
struct __attribute__((packed)) posPayload {
  int32_t x;            // relativ, kartesisch (mm)
  int32_t y;
  float   radius;       // relativ, polar (PA-Einheiten)
  float   angle;
  int32_t targetSpeed;
  int32_t res;
  int32_t worldX;       // Welt-Koordinate X (mm) - 0 wenn !worldValid
  int32_t worldY;       // Welt-Koordinate Y (mm) - 0 wenn !worldValid
  uint8_t worldValid;   // 1 = worldX/worldY gueltig (Sender hatte validWorldPose)
  uint8_t sensor;
};

// HB Basisdaten - stehen bei allen HB-Varianten am Anfang
struct __attribute__((packed)) hbPayload {
  uint8_t  ip;          // letztes Oktett
  uint32_t HBperiode;
};

// HB des PowerActors (Basis + Limits)
struct __attribute__((packed)) pa2HbPayload {
  hbPayload hb;
  uint8_t   readyToFire;
  uint8_t   limitsActive;
  float     leftLimit;
  float     rightLimit;
  float     farLimit;
  float     nearLimit;
};

// HB der Radarsensoren (Basis + Totzone)
struct __attribute__((packed)) radarHbPayload {
  hbPayload hb;
  float     deadZoneDist;   // nicht "deadZone": kollidiert mit #define in hwDef
};

// HB des LaserMarkers (Basis + aktueller Zustand aller Ausgaenge)
// Wird zyklisch gebroadcastet und nach jedem Kommando sofort -> der aktuelle
// Geraetezustand ist damit jederzeit von aussen ablesbar.
struct __attribute__((packed)) markerHbPayload {
  hbPayload hb;
  uint8_t   mainLaser;   // 0 = aus, 1 = an
  uint8_t   subLaser;    // 0 = aus, 1 = an
  uint8_t   aux;         // 0 = aus, 1 = an
  uint8_t   r;           // Pixel-Rot   0..255
  uint8_t   g;           // Pixel-Gruen 0..255
  uint8_t   b;           // Pixel-Blau  0..255
};

// catDetected: das Erkennungsmodell (Referenz catmodel.py auf dem VPS, Ziel-
// Implementierung ESP32) hat einen Track als Katze bestaetigt. Wird ausgeloest,
// sobald der Score die Bestaetigungsschwelle erreicht — also waehrend die Katze
// noch im Erfassungsbereich ist, nicht erst nach Track-Ende.
#define catDetFlagStationary 0x01   // Katze sitzt gerade (koten? Ziel nicht verlieren)
#define catDetFlagFusion     0x02   // mehrere Sensoren sehen dasselbe Objekt
struct __attribute__((packed)) catDetectedPayload {
  int32_t  worldX;      // Welt-Position der Katze bei Ausloesung (mm)
  int32_t  worldY;
  uint8_t  score;       // Modell-Score 0..100 (>= Bestaetigungsschwelle)
  uint8_t  flags;       // catDetFlag*-Bits
  uint32_t trackMs;     // bisherige Track-Dauer bei Ausloesung (ms)
  int32_t  netMm;       // bisherige Netto-Verschiebung des Tracks (mm)
};

// commandMsg (ersetzt ucDataStruct) - typischerweise per Unicast
struct __attribute__((packed)) cmdPayload {
  uint8_t cmd;
  int32_t info;
};

// poseReport: Welt-Pose eines Geraets (Antwort auf poseRequest / Annonce).
// Welt-Koordinaten sind immer kartesisch (mm); heading in PA-Einheiten (0..4096),
// 0 = lokale Achsen sind welt-ausgerichtet.
struct __attribute__((packed)) worldPosePayload {
  uint8_t validWorldPose;  // 1 = worldX/worldY/heading gueltig
  int32_t worldX;          // mm
  int32_t worldY;          // mm
  float   heading;         // PA-Einheiten 0..4096
  int8_t  mirror;          // +1 = normal, -1 = gespiegelt (Drehsinn des Sensors)
};

//------------------------- Karten-Verteilung (Manager -> Geraete) -----------------------
// mapRequest: ein Geraet fordert eine Karte beim Manager an
struct __attribute__((packed)) mapReqPayload {
  uint8_t mapType;         // gewuenschter Kartentyp (mapNoShot, ...)
};

// mapInfo: Manager meldet die Metadaten der Karte (Antwort auf mapRequest)
struct __attribute__((packed)) mapInfoPayload {
  uint8_t  mapType;
  uint16_t version;        // aus dem CSV-Header ("... vN ...")
  uint32_t fileCrc;        // CRC32 ueber die gesamte Datei (Identitaet + Integritaet)
  uint32_t totalLen;       // Dateilaenge in Bytes
  uint16_t chunkSize;      // Datenbytes pro mapChunk (ausser evtl. letztem)
  uint16_t chunkCount;     // Anzahl mapChunks
};

// mapChunk: feste Metadaten + variable Datenbytes.
// Auf der Leitung: payloadLen = sizeof(mapChunkMeta) + dataLen
struct __attribute__((packed)) mapChunkMeta {
  uint8_t  mapType;
  uint16_t version;
  uint16_t chunkIndex;     // 0..chunkCount-1, in Reihenfolge
  uint16_t dataLen;        // Anzahl folgender Datenbytes
};

// Datenbytes pro Chunk so gewaehlt, dass mapChunkMeta + Daten in den Payload passen
constexpr uint16_t mapChunkBytes = 48;   // 7 (Meta) + 48 = 55 <= maxPayloadLen (64)

//------------------------- Einstellungen & Steuerung der Geraete ------------------------
// Jedes Geraet fuehrt visuelles Feedback (LED-Pixel/Anzeige) und automatische Aufgaben aus.
// Diese lassen sich generisch ueber Display-Touch oder das VPS-Webinterface ein-/ausschalten
// bzw. ausloesen. Ein Geraet meldet per settingsReport eine Bitmaske, WELCHE Settings/Aktionen
// es hat (supported/actions) und den aktuellen Zustand (values). Welche Bits ein Geraet
// unterstuetzt, legt seine hwDef.h ueber STG_SUPPORTED/STG_DEFAULT/STG_PERSIST/STG_ACTIONS fest.
//
// Setting-Indizes (Bit-Position in settingsPayload.supported/values). Die Anzeige-Settings
// beziehen sich NUR auf die LED-/Anzeige-Ausgabe, nicht auf die Funktion selbst.
#define stgHbLed        0   // HB-Anzeige an/aus (beim Manager: HB EMPFANGEN)          [persistiert]
#define stgCatLed       1   // catObserved-Anzeige an/aus (Manager: catObserved EMPFANGEN) [persistiert]
#define stgAutoCopyPose 2   // Auto: Welt-Pose eines Gruppenmitglieds uebernehmen        [persistiert]
#define stgAutoCalib    3   // Radar: automatische Co-Observation-Kalibrierung          [persistiert]
#define stgLidarMotor   4   // Lidar: Motor an/aus (Default an, NICHT persistiert)
#define stgCatDetLed    5   // catDetected-Anzeige an/aus (Manager: rotes Blinken beim EMPFANG) [persistiert]
#define STG_COUNT       6

// Ausloesbare Aktionen (Bit-Position in settingsPayload.actions).
#define actCopyPose     0   // Welt-Pose jetzt aus der Gruppe kopieren  -> cmdCopyPose
#define actCalibrate    1   // Kalibrierung jetzt ausloesen             -> cmdCalibrate
#define actClearPose    2   // gespeicherte Welt-Pose loeschen          -> cmdClearPose
#define actReloadParams 3   // CatIdent: Modell-Parameter neu vom VPS laden -> cmdReloadParams
#define ACT_COUNT       4

// settingsReport-Payload: was das Geraet kann und wie es aktuell eingestellt ist.
struct __attribute__((packed)) settingsPayload {
  uint16_t supported;   // Bitmaske der vorhandenen Settings (stg*)
  uint16_t values;      // aktueller on/off-Zustand je Setting
  uint16_t actions;     // Bitmaske der ausloesbaren Aktionen (act*)
};

//cmd Codes für cmdPayload
#define cmdRichtung                1   // info: absoluten PA-Winkel 0..4096 anfahren (Jog/Encoder-Test)
#define cmdLaser                   2
#define cmdAdjustAngle             3
#define cmdTaste                   4
#define cmdArmFire                 5
#define cmdSetLeftLimit            6
#define cmdSetRightLimit           7
#define cmdSetFarLimit             8
#define cmdSetNearLimit            9
#define cmdChangeLimitActivation  10
// LaserMarker-Kommandos
#define cmdMainLaser              11   // info: 0 = aus, 1 = an
#define cmdSubLaser               12   // info: 0 = aus, 1 = an
#define cmdAux                    13   // info: 0 = aus, 1 = an
#define cmdPixelColor             14   // info: 0x00RRGGBB (24-Bit Farbe)
#define cmdMarkerState            15   // info: ignoriert - erzwingt sofortigen HB
// PowerActor1_1-Relais (zusaetzlich zu den PA-Kommandos; water = Feuer, automatisch)
#define cmdWirelessPower          16   // info: 0 = aus, 1 = an (Strom fuer Turm-Sensoren)
#define cmdExtPower               17   // info: 0 = aus, 1 = an (externe Versorgung)
// Co-Observation-Kalibrierung (Welt-Pose eines nicht selbst-lokalisierenden Sensors
// per gleichzeitiger Beobachtung einer Person mit einem welt-posierten Sensor).
#define cmdCalibrate              18   // info: Fensterdauer in ms (0 = Default); startet Kalibriermodus
#define cmdClearPose              19   // info: ignoriert - loescht die gespeicherte Welt-Pose (NVS), validWorldPose=false
// Einstellungen & Steuerung (an jedes Geraet; Display-Touch / VPS-Webinterface)
#define cmdSetSetting             20   // info = (settingIdx<<1)|value : Setting setzen+persistieren, dann settingsReport
#define cmdCopyPose               21   // info: ignoriert - Aktion actCopyPose: Welt-Pose aus der Gruppe kopieren
#define cmdReloadParams           22   // info: ignoriert - CatIdent laedt die Modell-Parameter neu vom VPS (/aparams.csv)

//---------------------------------------------------------------------------------------
// Welt-Pose dieses Geraets (jedes Geraet besitzt diese Variablen, damit die
// gemeinsamen Prozeduren generisch arbeiten koennen). validWorldPose ist nach
// dem Booten immer false und wird erst durch eine Positionsbestimmung/Quickcheck
// (eigene, spaetere Aufgabe) auf true gesetzt.
struct worldPose {
  int32_t worldX;          // Welt-X (mm)
  int32_t worldY;          // Welt-Y (mm)
  float   heading;         // Ausrichtung in PA-Einheiten 0..4096 (0 = welt-ausgerichtet)
  int8_t  mirror;          // +1 = normal, -1 = gespiegelt (Drehsinn); fuer localToWorld/worldToLocal
  bool    validWorldPose;  // false nach Boot
};
worldPose myPose = { 0, 0, 0.0f, 1, false };

//---------------------------------------------------------------------------------------
// Einstellungen dieses Geraets. supported/actions kommen aus der hwDef (STG_*), values
// wird beim Boot aus dem NVS geladen bzw. auf STG_DEFAULT gesetzt (initSettings in xComProc).
struct deviceSettings {
  uint16_t supported;
  uint16_t values;
  uint16_t actions;
};
deviceSettings mySettings = { 0, 0, 0 };

//---------------------------------------------------------------------------------------
IPAddress multiCastIP (239,0,0,57);
constexpr uint16_t MC_PORT = 8266;
AsyncUDP udpMc;
volatile bool mcDataReceived = false;
xMsg lastMcMsg;

//------------------------------------------------------------------------------------
constexpr uint16_t MC_Text_PORT = 8300;
AsyncUDP udpText;
volatile bool udpTextReceived = false;
String udpTextMsg;
//------------------------------------------------------------------------------------

static const uint16_t UC_PORT = 23456;
AsyncUDP udpUc;
volatile bool ucDataReceived = false;
xMsg lastUcMsg;
volatile uint8_t lastUcSenderOctet = 0;   // letztes IP-Oktett des Unicast-Absenders

struct tm timeinfo;
time_t now;
const char* ntpServer1 = "pool.ntp.org";
const char* ntpServer2 = "time.nist.gov";
const long  gmtOffset_sec = 3600;
const int   daylightOffset_sec = 3600;
const char* time_zone = "CET-1CEST,M3.5.0,M10.5.0/3";  // TimeZone rule for Europe/Rome including daylight adjustment rules (optional)
