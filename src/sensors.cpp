#include "sensors.h"
#include <Preferences.h>

// ── Globals ────────────────────────────────────────────────────
Sensor sensores[MAX_SENSORES];
int    NUM_SENSORES = 0;

// DHT objects en heap (1 instancia por protocolo, se crean según config)
static DHT* s_dht22 = nullptr;
static DHT* s_dht11 = nullptr;
static unsigned long s_dht11InitTime = 0;  // guard: skip reads for 2s after begin()

// Caché — se refresca una vez por ciclo en sensorsRead()
static float s_t22 = NAN, s_h22 = NAN;
static float s_t11 = NAN, s_h11 = NAN;

// ── Helpers ────────────────────────────────────────────────────
static void setSensorMeta(int i) {
    const char* t = sensores[i].tipo;
    bool isTemp = (strncmp(t, "TEMPERATURE", 11) == 0);
    if      (isTemp)                         { strcpy(sensores[i].unidad, "C");   strcpy(sensores[i].icono, "TEMP");   }
    else if (strcmp(t, "HUMIDITY")          == 0 ||
             strcmp(t, "HUMIDITY_EXTERNAL") == 0) { strcpy(sensores[i].unidad, "%");   strcpy(sensores[i].icono, "GOTA");   }
    else if (strcmp(t, "SOIL_MOISTURE")== 0) { strcpy(sensores[i].unidad, "%");   strcpy(sensores[i].icono, "PLANTA"); }
    else if (strcmp(t, "LIGHT")        == 0) { strcpy(sensores[i].unidad, "lx");  strcpy(sensores[i].icono, "TEMP");   }
    else if (strcmp(t, "CO2")          == 0) { strcpy(sensores[i].unidad, "ppm"); strcpy(sensores[i].icono, "GOTA");   }
    else                                     { strcpy(sensores[i].unidad, "");    strcpy(sensores[i].icono, "TEMP");   }
}

// Instancia objetos DHT con el GPIO configurado (uno por protocolo)
static void initDHTObjects() {
    delete s_dht22; s_dht22 = nullptr;
    delete s_dht11; s_dht11 = nullptr;

    for (int i = 0; i < NUM_SENSORES; i++) {
        if (strcmp(sensores[i].protocol, "DHT22") == 0 && s_dht22 == nullptr) {
            int pin = sensores[i].gpioPin > 0 ? sensores[i].gpioPin : PIN_DHT22;
            s_dht22 = new DHT(pin, DHT22);
            s_dht22->begin();
            Serial.printf("[Sensors] DHT22 en GPIO %d\n", pin);
        }
        if (strcmp(sensores[i].protocol, "DHT11") == 0 && s_dht11 == nullptr) {
            int pin = sensores[i].gpioPin > 0 ? sensores[i].gpioPin : PIN_DHT11;
            s_dht11 = new DHT(pin, DHT11);
            s_dht11->begin();
            s_dht11InitTime = millis();
            Serial.printf("[Sensors] DHT11 en GPIO %d\n", pin);
        }
    }
    // Fallback: siempre tener DHT22 disponible
    if (s_dht22 == nullptr) {
        s_dht22 = new DHT(PIN_DHT22, DHT22);
        s_dht22->begin();
    }
    // Fallback: siempre tener DHT11 disponible
    if (s_dht11 == nullptr) {
        s_dht11 = new DHT(PIN_DHT11, DHT11);
        s_dht11->begin();
        s_dht11InitTime = millis();
        Serial.printf("[Sensors] DHT11 fallback en GPIO %d\n", PIN_DHT11);
    }
}

static void sensorsLoadDefaults() {
    NUM_SENSORES = 5;
    const struct { const char* nm; const char* ty; const char* pr; int gp; } D[5] = {
        {"Temp DHT22", "TEMPERATURE_INTERNAL", "DHT22", PIN_DHT22},
        {"Hum  DHT22", "HUMIDITY",             "DHT22", PIN_DHT22},
        {"Temp DHT11", "TEMPERATURE_EXTERNAL", "DHT11", PIN_DHT11},
        {"Hum  DHT11", "HUMIDITY_EXTERNAL",   "DHT11", PIN_DHT11},
        {"Hum  Suelo", "SOIL_MOISTURE",        "ADC",   PIN_SUELO},
    };
    for (int i = 0; i < 5; i++) {
        strncpy(sensores[i].nombre,   D[i].nm, 31);
        strncpy(sensores[i].tipo,     D[i].ty, 23);
        strncpy(sensores[i].protocol, D[i].pr, 15);
        sensores[i].gpioPin   = D[i].gp;
        sensores[i].backendId = 0;
        sensores[i].conectado = false;
        sensores[i].valor     = 0.0f;
        setSensorMeta(i);
    }
    Serial.println("[Sensors] Config por defecto (5 sensores, LM35 desactivado).");
}

// ── sensorsLoadDynamic() ───────────────────────────────────────
void sensorsLoadDynamic() {
    Preferences p;
    p.begin("agr_s", true);
    int cnt = p.getInt("cnt", 0);

    if (cnt <= 0) {
        p.end();
        sensorsLoadDefaults();
        initDHTObjects();
        return;
    }

    NUM_SENSORES = min(cnt, (int)MAX_SENSORES);
    for (int i = 0; i < NUM_SENSORES; i++) {
        char k[8];
        sprintf(k, "id_%d", i); sensores[i].backendId = p.getInt(k, 0);
        sprintf(k, "nm_%d", i); strncpy(sensores[i].nombre,   p.getString(k, "Sensor").c_str(),      31);
        sprintf(k, "ty_%d", i); strncpy(sensores[i].tipo,     p.getString(k, "TEMPERATURE").c_str(), 23);
        sprintf(k, "pr_%d", i); strncpy(sensores[i].protocol, p.getString(k, "DHT22").c_str(),       15);
        sprintf(k, "gp_%d", i); sensores[i].gpioPin = p.getInt(k, 0);
        sensores[i].conectado = false;
        sensores[i].valor     = 0.0f;
        setSensorMeta(i);
    }
    p.end();

    // Auto-completar: cada DHT debe tener un sensor HUMIDITY además de TEMPERATURE.
    // Si el backend solo registró temperatura, se agrega la humedad automáticamente.
    int origCount = NUM_SENSORES;
    for (int i = 0; i < origCount && NUM_SENSORES < MAX_SENSORES; i++) {
        const char* pr = sensores[i].protocol;
        if (strcmp(pr, "DHT22") != 0 && strcmp(pr, "DHT11") != 0) continue;
        if (strncmp(sensores[i].tipo, "TEMPERATURE", 11) != 0) continue;
        bool hasHumidity = false;
        for (int j = 0; j < NUM_SENSORES; j++) {
            if (strcmp(sensores[j].protocol, pr) == 0 &&
                (strcmp(sensores[j].tipo, "HUMIDITY") == 0 ||
                 strcmp(sensores[j].tipo, "HUMIDITY_EXTERNAL") == 0)) {
                hasHumidity = true; break;
            }
        }
        if (!hasHumidity) {
            int j = NUM_SENSORES++;
            snprintf(sensores[j].nombre, 32, "Hum %s", strcmp(pr, "DHT22") == 0 ? "DHT22" : "DHT11");
            // DHT11 humidity is exterior (HUMIDITY_EXTERNAL), DHT22 is interior (HUMIDITY)
            const char* humType = (strcmp(pr, "DHT11") == 0) ? "HUMIDITY_EXTERNAL" : "HUMIDITY";
            strncpy(sensores[j].tipo,     humType, 23);
            strncpy(sensores[j].protocol, pr, 15);
            sensores[j].gpioPin   = sensores[i].gpioPin;
            sensores[j].backendId = 0;
            sensores[j].conectado = false;
            sensores[j].valor     = 0.0f;
            setSensorMeta(j);
            Serial.printf("[Sensors] Auto-agregado: Hum %s en GPIO %d\n", pr, sensores[j].gpioPin);
        }
    }

    Serial.printf("[Sensors] Config cargada: %d sensores\n", NUM_SENSORES);
    initDHTObjects();
}

// ── sensorsInit() ─────────────────────────────────────────────
void sensorsInit() {
    sensorsLoadDynamic();
    Serial.println("[Sensors] Modulo inicializado.");
}

// ── sensorsRead() ─────────────────────────────────────────────
static void refreshDHTCache() {
    if (s_dht22) {
        float t = NAN, h = NAN;
        for (int r = 0; r < DHT_MAX_INTENTOS && (isnan(t) || isnan(h)); r++) {
            if (r > 0) { vTaskDelay(pdMS_TO_TICKS(500)); }
            t = s_dht22->readTemperature();
            h = s_dht22->readHumidity();
        }
        s_t22 = t; s_h22 = h;
    }
    if (s_dht11) {
        // DHT11 library caches for 2s — retrying at 50ms returns the same NaN.
        // Read once per sensor cycle (INTERVALO_LECTURA_MS = 10s, well past cache).
        // Guard: skip until 2s after begin() so the sensor is stable.
        if ((millis() - s_dht11InitTime) < 2000) {
            s_t11 = NAN; s_h11 = NAN;
        } else {
            float t = s_dht11->readTemperature();
            float h = s_dht11->readHumidity();
            s_t11 = (!isnan(t) && t >= 0.0f && t < 60.0f)  ? t : NAN;
            s_h11 = (!isnan(h) && h >= 0.0f && h <= 100.0f) ? h : NAN;
        }
    }
}

SensorReadings sensorsRead() {
    SensorReadings result;
    result.timestamp = millis();
    refreshDHTCache();

    for (int i = 0; i < NUM_SENSORES; i++) {
        const char* pr = sensores[i].protocol;
        const char* ty = sensores[i].tipo;
        float val = NAN;
        bool  ok  = false;

        bool isTemp = (strncmp(ty, "TEMPERATURE", 11) == 0);
        if (strcmp(pr, "DHT22") == 0) {
            val = (strcmp(ty, "HUMIDITY") == 0) ? s_h22 : s_t22;
            if (!isnan(val))
                ok = isTemp ? (val > -40.0f && val < 80.0f) : (val >= 0.0f && val <= 100.0f);
        } else if (strcmp(pr, "DHT11") == 0) {
            bool isHum11 = (strcmp(ty, "HUMIDITY") == 0 || strcmp(ty, "HUMIDITY_EXTERNAL") == 0);
            val = isHum11 ? s_h11 : s_t11;
            if (!isnan(val))
                ok = isTemp ? (val > 0.0f && val < 60.0f) : (val >= 0.0f && val <= 100.0f);
        } else if (strcmp(pr, "ADC") == 0 || strcmp(pr, "ANALOG") == 0) {
            int raw  = analogRead(sensores[i].gpioPin);
            bool inR = (raw > ADC_MIN && raw < ADC_MAX);
            if (strcmp(ty, "SOIL_MOISTURE") == 0) {
                val = inR ? (float)map(raw, 4095, 0, 0, 100) : NAN;
            } else if (isTemp) {
                float t = raw * (3.3f / 4095.0f) * 100.0f;
                val = (inR && t > 0.0f && t < 85.0f) ? t : NAN;
            } else {
                val = inR ? (float)raw : NAN;
            }
            ok = !isnan(val);
        } else if (strcmp(pr, "DIGITAL") == 0) {
            val = (float)digitalRead(sensores[i].gpioPin);
            ok  = true;
        }

        sensores[i].conectado = ok;
        sensores[i].valor     = ok ? val : 0.0f;
    }

    // Consolidar para control automático
    bool gotTemp = false;
    for (int i = 0; i < NUM_SENSORES; i++) {
        if (!sensores[i].conectado) continue;
        bool isTemp = (strncmp(sensores[i].tipo, "TEMPERATURE", 11) == 0);
        bool isExternal = (strcmp(sensores[i].tipo, "TEMPERATURE_EXTERNAL") == 0);
        if (isTemp) {
            if (!gotTemp && !isExternal) { result.tempInterior = sensores[i].valor; gotTemp = true; result.valid = true; }
            else                          result.tempExterior  = sensores[i].valor;
        } else if (strcmp(sensores[i].tipo, "HUMIDITY") == 0) {
            result.humidityAir  = sensores[i].valor;
        } else if (strcmp(sensores[i].tipo, "SOIL_MOISTURE") == 0) {
            result.soilMoisture = sensores[i].valor;
        }
    }
    return result;
}

void sensorsPrintSerial(const SensorReadings& r) {
    Serial.println("[Sensores] ─────────────────────────────");
    for (int i = 0; i < NUM_SENSORES; i++) {
        Serial.printf("  %-12s: %s  %.1f %s  (id=%d, gpio=%d)\n",
            sensores[i].nombre,
            sensores[i].conectado ? "OK " : "---",
            sensores[i].valor,
            sensores[i].unidad,
            sensores[i].backendId,
            sensores[i].gpioPin);
    }
    Serial.printf("[Sensors] INT:%.1fC | HUM:%.0f%% | SUELO:%.0f%% | %s\n",
        r.tempInterior, r.humidityAir, r.soilMoisture,
        r.valid ? "OK" : "ERROR");
}
