#ifndef AGROPULSE_COMM_H
#define AGROPULSE_COMM_H

/*
 * comm.h — Módulo de comunicaciones
 *
 * Responsabilidades:
 *   - Conexión WiFi sin credenciales hardcodeadas (WiFiManager)
 *   - Persistencia de configuración en flash NVS (Preferences)
 *   - Comunicación HTTPS con el backend AgroPulse (ArduinoJson)
 *   - Vinculación del dispositivo con el invernadero en el backend
 *   - Procesamiento de comandos por Serial
 *
 * Patrón Bridge (analogía): commSendReadings() abstrae el protocolo;
 * si WiFi falla, cae automáticamente a Serial (estrategia fallback).
 */

#include <Arduino.h>
#include <WiFi.h>
#include "sensors.h"
#include "actuators.h"
#include "../include/config.h"

// ── Estado de la conexión ──────────────────────────────────────
struct CommStatus {
    bool   wifiConnected;
    String backendUrl;
    int    greenhouseId;
    unsigned long lastSuccessfulSend;
    int    failedAttempts;

    CommStatus() :
        wifiConnected(false),
        greenhouseId(0),
        lastSuccessfulSend(0),
        failedAttempts(0) {}
};

// ── Interfaz pública ───────────────────────────────────────────
void commInit();
void commStartTask();             // inicia tarea HTTP en Core 0 (llamar tras commInit)
void commQueueReport(int idx);    // encola reporte de actuador — no bloquea el loop
bool commConnectWifi();

bool commSendReadings();              // POST /api/readings por cada sensor conectado
bool commSyncActuators();             // GET  /api/actuators → aplica estado remoto
bool commReportActuator(int idx);     // PUT/POST /api/actuators → reporta cambio local
bool commReportSensor(int idx);       // PUT/POST /api/sensors  → registra sensor en backend
bool commVincular();                  // GET  /api/greenhouses/{codigo}
bool commDownloadConfig();            // GET  /api/device/config/{ghId} → guarda en NVS
bool commSendLocation(double lat, double lng);
// PUT /api/greenhouses/{ghId} con {"latitude": lat, "longitude": lng}
// Retorna true si HTTP 200/201. Falla silenciosamente si no hay WiFi o ghId == 0.

void commProcessSerialCommand(const String& cmd,
                              ActuatorThresholds& thresholds,
                              bool& autoMode);

CommStatus    commGetStatus();
const String& commGetNombre();
const String& commGetCodigo();
int           commGetGhId();
const String& commGetUrl();

void commSetGhId(int id);       // Guarda un nuevo GH ID directamente en NVS
void commReiniciarWifi();       // Borra credenciales WiFi y reinicia → abre portal

#endif // AGROPULSE_COMM_H
