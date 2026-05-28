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
    else if (strcmp(t, "CURRENT")      == 0) { strcpy(sensores[i].unidad, "A");   strcpy(sensores[i].icono, "RAYO");   }
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
            // Wakeup sequence: send 3 start pulses so the sensor completes
            // any pending measurement from before the ESP32 soft-reset and
            // returns to idle.  Without this the sensor stays stuck and never
            // responds to read() calls until a full power cycle.
            for (int w = 0; w < 3; w++) {
                pinMode(pin, OUTPUT);
                digitalWrite(pin, LOW);
                delay(20);
                pinMode(pin, INPUT_PULLUP);
                delay(260);
            }
            Serial.printf("[Sensors] DHT11 wakeup OK (GPIO %d)\n", pin);
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
    NUM_SENSORES = 6;
    const struct { const char* nm; const char* ty; const char* pr; int gp; } D[6] = {
        {"Temp DHT22", "TEMPERATURE_INTERNAL", "DHT22", PIN_DHT22},
        {"Hum  DHT22", "HUMIDITY",             "DHT22", PIN_DHT22},
        {"Temp DHT11", "TEMPERATURE_EXTERNAL", "DHT11", PIN_DHT11},
        {"Hum  DHT11", "HUMIDITY_EXTERNAL",    "DHT11", PIN_DHT11},
        {"Hum  Suelo", "SOIL_MOISTURE",        "DIGITAL", PIN_SUELO},
        {"Corriente",  "CURRENT",              "ADC",   PIN_CORRIENTE},
    };
    for (int i = 0; i < 6; i++) {
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
    // Single read attempt per cycle — no blocking delays.
    // If a read fails (NaN), the cached value stays NaN until the next
    // sensorsRead() call (10 s later), which naturally retries.
    // vTaskDelay inside the main-loop task freezes buttons and OLED for
    // seconds and was the root cause of unresponsive button behavior.
    if (s_dht22) {
        float t = s_dht22->readTemperature();
        float h = s_dht22->readHumidity();
        s_t22 = (!isnan(t) && t > -40.0f && t < 80.0f)   ? t : NAN;
        s_h22 = (!isnan(h) && h >= 0.0f  && h <= 100.0f) ? h : NAN;
    }
    if (s_dht11) {
        if ((millis() - s_dht11InitTime) < 3000) {
            s_t11 = NAN; s_h11 = NAN;
        } else {
            // DHT11 uses bit-banging; WiFi interrupt handlers on Core 1 corrupt
            // the pulse-width timing, causing CRC errors → NaN.  Disabling
            // interrupts on Core 1 for the ~4 ms read window prevents this.
            // The WiFi stack continues unaffected on Core 0.
            noInterrupts();
            float t = s_dht11->readTemperature();
            float h = s_dht11->readHumidity();
            interrupts();
            s_t11 = (!isnan(t) && t >= 0.0f && t < 60.0f)   ? t : NAN;
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
            if (strcmp(ty, "CURRENT") == 0) {
                // ACS712: promedia ACS712_SAMPLES lecturas para reducir ruido ADC.
                long sum = 0;
                for (int s = 0; s < ACS712_SAMPLES; s++) {
                    sum += analogRead(sensores[i].gpioPin);
                    delayMicroseconds(200);
                }
                float avgRaw = (float)(sum / ACS712_SAMPLES);
                float voltage = avgRaw * (3.3f / 4095.0f);
                float amps = (voltage - ACS712_VREF) / ACS712_SENSITIVITY;
                val = (amps < 0.0f) ? 0.0f : amps;
                ok  = true;
            } else {
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
            }
        } else if (strcmp(pr, "DIGITAL") == 0) {
            int level = digitalRead(sensores[i].gpioPin);
            if (strcmp(ty, "SOIL_MOISTURE") == 0) {
                // FC-28 DO: LOW = suelo húmedo (por encima del umbral del pot.)
                //           HIGH = suelo seco  (por debajo del umbral)
                val = (level == LOW) ? 100.0f : 0.0f;
            } else {
                val = (float)level;
            }
            ok = true;
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
