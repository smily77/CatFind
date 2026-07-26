// Cat Cam: Seeed XIAO Vision AI Camera = XIAO ESP32-C3 + Grove Vision AI V2
// (Himax WiseEye2, eigenes KI-Modell via SenseCraft) + OV5647 (62 Grad FOV).
// Der C3 spricht das Vision-Modul per I2C (SSCMA-Protokoll) an.
// WLAN nur ueber die IPEX-Antenne (wie beim XIAO S3: ohne Antenne taub).
// Der XIAO ESP32-C3 hat KEINE User-LED.
#define ID CatCam

#define periodeForHB 10000

// --- Einstellungen & Steuerung (settingsPayload / xComProc initSettings) ---------------
// stgCamAi: KI-Erkennung an/aus (Katze im Bild -> catObserved + Foto). Persistiert.
// Aktion actPhoto: sofort ein Foto machen und zum VPS hochladen (Test/Doku).
// stgActive (Ruhemodus) unterstuetzt, Default AN, NICHT persistiert (nach Reboot aktiv):
// inaktiv = keine KI/keine Fotos, nur HB.
#define STG_SUPPORTED ((1u<<stgCamAi)|(1u<<stgActive))
#define STG_DEFAULT   ((1u<<stgCamAi)|(1u<<stgActive))
#define STG_PERSIST   (1u<<stgCamAi)   // stgActive bewusst nicht persistiert
#define STG_ACTIONS   (1u<<actPhoto)
