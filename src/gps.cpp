#include "gps.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>

void gpsInit() {
    // Sin hardware GPS — geolocalización por IP, nada que inicializar.
}

bool gpsReadBlocking(double& lat, double& lng, uint32_t timeoutMs) {
    HTTPClient http;
    // Campos: status, lat, lon, city, country (minimiza tamaño de respuesta)
    http.begin("http://ip-api.com/json?fields=status,lat,lon,city,country");
    http.setTimeout((int)timeoutMs);

    int code = http.GET();
    if (code != 200) {
        Serial.printf("[GPS] IP geoloc fallo: HTTP %d\n", code);
        http.end();
        return false;
    }

    String body = http.getString();
    http.end();

    StaticJsonDocument<256> doc;
    DeserializationError err = deserializeJson(doc, body);
    if (err) {
        Serial.printf("[GPS] IP geoloc: JSON invalido (%s)\n", err.c_str());
        return false;
    }
    if (strcmp(doc["status"] | "", "success") != 0) {
        Serial.println("[GPS] IP geoloc: servicio reporto fallo");
        return false;
    }

    lat = doc["lat"].as<double>();
    lng = doc["lon"].as<double>();
    const char* city    = doc["city"]    | "?";
    const char* country = doc["country"] | "?";
    Serial.printf("[GPS] IP geoloc OK: %.4f, %.4f (%s, %s)\n", lat, lng, city, country);
    return true;
}
