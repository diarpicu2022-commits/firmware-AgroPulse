#include "gps.h"
#include <TinyGPS++.h>

static TinyGPSPlus s_gps;

void gpsInit() {
    static bool s_initialized = false;
    if (s_initialized) return;
    s_initialized = true;
    // GPIO 16 = RX2, GPIO 17 = TX2 del ESP32
    Serial2.begin(9600, SERIAL_8N1, 16, 17);
    Serial.println("[GPS] Serial2 iniciado en GPIO 16/17 a 9600 baud.");
}

bool gpsReadBlocking(double& lat, double& lng, uint32_t timeoutMs) {
    uint32_t start = millis();
    Serial.printf("[GPS] Esperando fix (timeout: %u ms)...\n", timeoutMs);

    while (millis() - start < timeoutMs) {
        while (Serial2.available() > 0) {
            s_gps.encode(Serial2.read());
        }
        if (s_gps.location.isValid() && s_gps.location.isUpdated()) {
            lat = s_gps.location.lat();
            lng = s_gps.location.lng();
            Serial.printf("[GPS] Fix obtenido: lat=%.6f lng=%.6f\n", lat, lng);
            return true;
        }
        delay(10);
    }

    Serial.println("[GPS] Timeout — sin fix GPS.");
    return false;
}
