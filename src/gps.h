#ifndef AGROPULSE_GPS_H
#define AGROPULSE_GPS_H

/*
 * gps.h — Geolocalización por IP (ip-api.com)
 *
 * No requiere módulo GPS físico.  Usa la IP pública del ESP32
 * para obtener coordenadas aproximadas a nivel de ciudad.
 * Precisión: ~1–50 km dependiendo del ISP.
 *
 * Servicio: http://ip-api.com/json  (gratuito, sin API key, 45 req/min)
 * GPIO 16 y 17 quedan libres (Serial2 ya no se usa).
 */

#include <Arduino.h>

void gpsInit();
// Sin operación (no hay hardware que inicializar).

bool gpsReadBlocking(double& lat, double& lng, uint32_t timeoutMs = 10000);
// Consulta ip-api.com y escribe lat/lng si la respuesta es válida.
// timeoutMs se usa como timeout HTTP (máx 10 s).
// Retorna true y escribe lat/lng si tuvo éxito. Retorna false en error.

#endif // AGROPULSE_GPS_H
