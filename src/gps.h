#ifndef AGROPULSE_GPS_H
#define AGROPULSE_GPS_H

/*
 * gps.h — Módulo GPS (NEO-6M / NEO-8M)
 *
 * Wiring:
 *   GPS TX → GPIO 16 (ESP32 RX2)
 *   GPS RX → GPIO 17 (ESP32 TX2)
 *   GPS VCC → 3.3V
 *   GPS GND → GND
 *
 * Solo se activa en dos puntos: setup() si ya vinculado, y commVincular() tras éxito.
 * No corre en el loop principal.
 *
 * Requires: mikalhart/TinyGPSPlus in lib_deps (platformio.ini)
 */

#include <Arduino.h>

void gpsInit();
// Inicia Serial2 a 9600 baud en GPIO 16 (RX) / 17 (TX)

bool gpsReadBlocking(double& lat, double& lng, uint32_t timeoutMs = 10000);
// Lee sentencias NMEA hasta obtener fix válido o agotar timeoutMs.
// Retorna true y escribe lat/lng si hay fix. Retorna false por timeout.
// Si retorna false, lat y lng NO son modificados.
// Nunca bloquea más de timeoutMs ms.

#endif // AGROPULSE_GPS_H
