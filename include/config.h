#ifndef AGROPULSE_CONFIG_H
#define AGROPULSE_CONFIG_H

/*
 * AgroPulse Firmware v3.0 — Configuración de hardware
 * Todos los pines, umbrales y parámetros centralizados.
 * Cambiar aquí, nunca en el código de lógica.
 */

// ── Identificación del dispositivo ────────────────────────────
#define DEVICE_ID        "ESP32-AP-001"
#define FIRMWARE_VERSION "3.0.0"

// ── Pines sensores ─────────────────────────────────────────────
// GPIO 34/35 son ADC solo-entrada, sin pull-up interno
#define PIN_DHT22        4    // DHT22 — temperatura/humedad ambiente 1
#define PIN_DHT11        5    // DHT11 — temperatura/humedad ambiente 2
#define PIN_SUELO       34    // FC-28 humedad suelo (DO digital — LOW=húmedo, HIGH=seco)
#define PIN_CORRIENTE   35    // ACS712 — sensor de corriente (ADC, solo-entrada)

// ── ACS712 — calibración ───────────────────────────────────────
// Alimentar el módulo ACS712 a 3.3 V para que VIOUT nunca supere
// el límite del ADC del ESP32.  Con VCC=3.3V: Voffset = VCC/2 = 1.65 V.
// Sensibilidad según modelo (a 3.3 V ≈ sensibilidad_5V × 3.3/5):
//   5A  → ~122 mV/A  → 0.122
//  20A  → ~66  mV/A  → 0.066  ← valor por defecto
//  30A  → ~44  mV/A  → 0.044
#define ACS712_VREF         1.65f   // V — voltaje de salida con I=0 A (VCC/2)
#define ACS712_SENSITIVITY  0.066f  // V/A — cambiar según modelo (ver tabla arriba)
#define ACS712_SAMPLES         64   // muestras a promediar por lectura (~13 ms)
#define ACS712_VOLTAGE      12.0f   // V — voltaje de alimentación del sistema (para P=V×I)

// ── Pines actuadores ───────────────────────────────────────────
// Relés HW-383: activos en LOW (LOW = bobina energizada)
#define PIN_RELE_BOMBA  26    // HW-383 canal 1 — motobomba de riego
#define PIN_RELE_MOTOR  27    // HW-383 canal 2 — motor/ventilador
#define PIN_SERVO       15    // Señal PWM servomotor
#define PIN_LED1        18    // LED indicador 1
#define PIN_LED2        19    // LED indicador 2
#define PIN_LED_STATUS   2    // LED integrado del ESP32

// ── Pines botones (INPUT_PULLUP — presión = LOW) ───────────────
// GPIO25 (UP) es ADC2_CH8: WiFi inyecta spikes de ruido en ADC2 durante
// transmisión. El debounce clásico fallaba porque cada spike reseteaba
// tCambio. Solución por software: debounce con sampleCount (ver main.cpp)
// que es inmune a spikes breves. No se requiere cambio de hardware.
// GPIO32/33 son ADC1 y no tienen este problema (SELECT/BACK funcionan bien).
#define PIN_BTN_UP      25    // ADC2_CH8 — debounce por sampleCount lo maneja
#define PIN_BTN_DOWN    23    // VSPI_MOSI — también protegido por sampleCount
#define PIN_BTN_SELECT  32
#define PIN_BTN_BACK    33

// ── OLED I2C (SSD1306 128×64) ─────────────────────────────────
#define PIN_SDA         21
#define PIN_SCL         22

// ── Calibración ADC ───────────────────────────────────────────
#define ADC_MIN        150    // valor mínimo para considerar sensor presente
#define ADC_MAX       3900    // valor máximo para considerar sensor presente

// ── Intervalos de temporización ───────────────────────────────
#define INTERVALO_LECTURA_MS   10000   // releer sensores cada 10 s
#define INTERVALO_ENVIO_MS     30000   // enviar al backend cada 30 s
#define INTERVALO_UI_MS          300   // refresco OLED cada 300 ms
#define DEBOUNCE_MS               50   // debounce de botones (50ms → tolera ruido ADC2/WiFi)
#define DHT_MAX_INTENTOS           3   // reintentos DHT antes de marcar desconectado
#define TIMEOUT_HTTP_MS         3000   // timeout peticiones HTTP

// ── NTP (UTC-5 Colombia) ──────────────────────────────────────
#define NTP_SERVIDOR  "pool.ntp.org"
#define NTP_GMT_OFF   (-5L * 3600L)
#define NTP_DST_OFF   0

// ── Umbrales de control automático (valores por defecto) ──────
#define DEFAULT_PUMP_ON_THRESHOLD    30.0f   // activar bomba si suelo < 30 %
#define DEFAULT_PUMP_OFF_THRESHOLD   60.0f   // apagar  bomba si suelo > 60 %
#define DEFAULT_FAN_ON_THRESHOLD     28.0f   // activar motor si temp  > 28 °C
#define DEFAULT_FAN_OFF_THRESHOLD    24.0f   // apagar  motor si temp  < 24 °C
#define DEFAULT_HEATER_ON_THRESHOLD  12.0f   // (reservado — sin relay de calefactor)
#define DEFAULT_HEATER_OFF_THRESHOLD 18.0f

// ── OTA (Over-The-Air update) ─────────────────────────────────
#define OTA_HTTP_TIMEOUT_MS      5000   // timeout para version check y descarga de firma
#define OTA_DOWNLOAD_TIMEOUT_MS 60000   // timeout para descargar el binario completo (~1 MB)

// ── Serial ────────────────────────────────────────────────────
#define SERIAL_BAUD_RATE 115200

// ── Debug condicional ─────────────────────────────────────────
#ifdef AGROPULSE_DEBUG
  #define DBG(fmt, ...) Serial.printf("[DBG] " fmt "\n", ##__VA_ARGS__)
#else
  #define DBG(fmt, ...)
#endif

#endif // AGROPULSE_CONFIG_H
