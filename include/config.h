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
#define PIN_SUELO       34    // FC-28 humedad suelo (ADC)
#define PIN_LM35        35    // LM35  temperatura analógica (ADC)

// ── Pines actuadores ───────────────────────────────────────────
// Relés HW-383: activos en LOW (LOW = bobina energizada)
#define PIN_RELE_BOMBA  26    // HW-383 canal 1 — motobomba de riego
#define PIN_RELE_MOTOR  27    // HW-383 canal 2 — motor/ventilador
#define PIN_SERVO       15    // Señal PWM servomotor
#define PIN_LED1        18    // LED indicador 1
#define PIN_LED2        19    // LED indicador 2
#define PIN_LED_STATUS   2    // LED integrado del ESP32

// ── Pines botones (INPUT_PULLUP — presión = LOW) ───────────────
// GPIO12-15 son pines JTAG (MTDI/MTCK/MTMS/MTDO) del ESP32.
// El subsistema JTAG interfiere con su uso como entradas digitales normales,
// especialmente GPIO13 (MTCK) y GPIO14 (MTMS) con WiFi activo.
// GPIO25 y GPIO23 son pines completamente limpios: sin JTAG, sin strapping,
// sin ADC2, sin conflicto con flash ni PSRAM.
#define PIN_BTN_UP      25    // era 13 — GPIO13 es JTAG MTCK, interferencia con WiFi
#define PIN_BTN_DOWN    23    // era 14 — GPIO14 es JTAG MTMS, interferencia con WiFi
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
#define DEBOUNCE_MS               20   // debounce de botones
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

// ── Serial ────────────────────────────────────────────────────
#define SERIAL_BAUD_RATE 115200

// ── Debug condicional ─────────────────────────────────────────
#ifdef AGROPULSE_DEBUG
  #define DBG(fmt, ...) Serial.printf("[DBG] " fmt "\n", ##__VA_ARGS__)
#else
  #define DBG(fmt, ...)
#endif

#endif // AGROPULSE_CONFIG_H
