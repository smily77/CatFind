// MDV2Protocol.h — MapDiscoveryV2: UDP-Protokoll Karte <-> Head
//
// Gemeinsam für PanTiltHead_WM_V2 und PanTiltKarte_WM_V2 (beide binden diese
// Datei als "../Common/MDV2Protocol.h" ein). Binär, packed, Little-Endian.
//
// Grundsatz: Jeder Auftrag trägt eine Auftragsnummer (seq). Jede Antwort nennt
// die Nummer, auf die sie sich bezieht. Die Karte wertet nur Antworten zum
// aktuell offenen Auftrag aus; verspätete Antworten fallen so nie in eine
// Messung hinein.
//
// Abläufe
//   SET      Karte schickt den Sollzustand (Pan, Tilt, Laser, Marker). Der Head
//            übernimmt ihn sofort und antwortet mit ST_DONE. Läuft gerade eine
//            Messung, übernimmt er Laser/Marker sofort, die Position erst nach
//            der Messung, und antwortet mit ST_BUSY. Die Karte schickt SET bei
//            jeder Änderung und spätestens jede Sekunde (Lebenszeichen).
//   MEASURE  Karte schickt Zielposition + Messauftrag. Head antwortet sofort
//            ST_ACCEPTED, fährt an (optional Anti-Backlash), wartet bis Pan am
//            Ziel steht, misst und antwortet ST_DONE oder ST_FAILED mit Ist-
//            Winkeln. Wiederholt die Karte denselben Auftrag (gleiche seq),
//            misst der Head NICHT neu: läuft er noch -> ST_BUSY, ist er fertig
//            -> dieselbe Endantwort nochmals.
//
// Ports: Head lauscht auf 4210, Karte auf 4211. Der Head antwortet an Absender-
// IP und -Port. Die Karte sendet per Broadcast, bis sich ein Head meldet, und
// danach per Unicast an dessen IP (Broadcast hat im WLAN keine Wiederholung).
#pragma once
#include <stdint.h>

#define MDV2_MAGIC        0x3256444Du   // "MDV2" als Bytes 'M','D','V','2'
#define MDV2_PORT_HEAD    4210
#define MDV2_PORT_KARTE   4211

enum : uint8_t {
    MDV2_CMD_SET     = 1,
    MDV2_CMD_MEASURE = 2,
};

// Flags in CmdMsg.flags
enum : uint8_t {
    MDV2_F_LASER    = 0x01,   // Laser des Entfernungsmessers an (Zielhilfe)
    MDV2_F_MARKER   = 0x02,   // LaserMarker an
    MDV2_F_APPROACH = 0x04,   // MEASURE: Anti-Backlash-Anfahrt (von unten)
};

// Status in ReplyMsg.status
enum : uint8_t {
    MDV2_ST_DONE     = 0,   // SET übernommen / Messung gültig
    MDV2_ST_ACCEPTED = 1,   // MEASURE angenommen, läuft
    MDV2_ST_BUSY     = 2,   // Messung läuft noch
    MDV2_ST_FAILED   = 3,   // Messung ohne gültigen Wert
    MDV2_ST_NOSENSOR = 4,   // kein Entfernungsmesser erkannt
};

// Sensortyp in ReplyMsg.sensor (gleich wie RangefinderType der Library)
enum : uint8_t {
    MDV2_SENSOR_NONE    = 0,
    MDV2_SENSOR_SEN0366 = 1,
    MDV2_SENSOR_X40     = 2,
};

struct __attribute__((packed)) MDV2CmdMsg {     // Karte -> Head, 16 Bytes
    uint32_t magic;
    uint16_t seq;
    uint8_t  cmd;       // MDV2_CMD_*
    uint8_t  flags;     // MDV2_F_*
    float    panDeg;    // Soll Pan  (-170..+170)
    float    tiltDeg;   // Soll Tilt (-30..+30)
};

struct __attribute__((packed)) MDV2ReplyMsg {   // Head -> Karte, 24 Bytes
    uint32_t magic;
    uint16_t seq;          // Auftragsnummer, auf die sich die Antwort bezieht
    uint8_t  cmd;          // MDV2_CMD_* des Auftrags
    uint8_t  status;       // MDV2_ST_*
    float    distanceM;    // Schrägdistanz in Metern (nur bei ST_DONE einer Messung)
    float    panActDeg;    // Ist-Pan laut Servo (bei Messung: Winkel der Messung)
    float    tiltActDeg;   // Ist-Tilt laut Servo (Sollwert, falls Servo stumm)
    uint8_t  flags;        // Ist-Zustand Laser/Marker (MDV2_F_LASER/MARKER)
    uint8_t  sensor;       // MDV2_SENSOR_*
    uint8_t  tiltOk;       // 1 = Tilt-Ist vom Servo gelesen, 0 = Sollwert
    uint8_t  reserved;
};

static_assert(sizeof(MDV2CmdMsg) == 16, "MDV2CmdMsg muss 16 Bytes haben");
static_assert(sizeof(MDV2ReplyMsg) == 24, "MDV2ReplyMsg muss 24 Bytes haben");
