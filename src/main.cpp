/*
 * ════════════════════════════════════════════════════════════════
 *  AgroPulse Firmware v3.0 — ESP32
 *  Plataforma: ESP32-WROOM-32D / Arduino Framework / PlatformIO
 *
 *  Arquitectura modular (SRP aplicado por módulo):
 *    sensors.h   — Auto-detección y lectura: DHT22, DHT11, FC-28, LM35
 *    actuators.h — Control: relés (activo LOW), servo, LEDs
 *    comm.h      — WiFiManager (sin credenciales hardcodeadas), NVS,
 *                  HTTPS + ArduinoJson al backend AgroPulse
 *    display.h   — OLED U8g2 con iconos, barras de progreso y menú
 *    config.h    — Toda la configuración de hardware centralizada
 *
 *  Este archivo solo orquesta: no implementa lógica de negocio.
 *  Responsabilidades de main:
 *    - Debounce no bloqueante de 4 botones
 *    - Máquina de estados del menú interactivo (6 estados)
 *    - Loop principal no bloqueante con 3 intervalos independientes
 * ════════════════════════════════════════════════════════════════
 */

#include <Arduino.h>
#include "sensors.h"
#include "actuators.h"
#include "comm.h"
#include "display.h"
#include "gps.h"
#include "ota.h"
#include "../include/config.h"

// ── Estado del menú ───────────────────────────────────────────
static EstadoMenu estadoActual   = MENU_HOME;
static int        cursorMenu     = 0;
static int        cursorActuador = 0;
static int        cursorAngulo   = 2;  // índice en ANGULOS_SERVO (90° por defecto)
static int        g_editGhId     = 0;  // valor en edición para MENU_SET_GHID

// ── Estado del sistema ────────────────────────────────────────
static bool              g_autoMode     = false;
static ActuatorThresholds g_thresholds;
static ActuatorState      g_autoState;
static unsigned long      g_tLectura    = 0;
static unsigned long      g_tUI         = 0;
static int               g_homePage     = 0;   // 0 = página 1, 1 = página 2

// ── Debounce de botones ───────────────────────────────────────
// UP/DOWN tienen auto-repeat al mantener: 400 ms de espera, luego cada 120 ms
#define BTN_REPEAT_DELAY_MS  400
#define BTN_REPEAT_RATE_MS   120

struct Boton {
    int           pin;
    bool          estadoFiltrado;
    bool          estadoAnterior;
    unsigned long tCambio;
    bool          disparado;   // true solo el ciclo en que se detectó la presión
    bool          conRepeat;   // true = emite disparos mientras se mantiene
    unsigned long tPrimerDisparo;
    unsigned long tUltimoRepeat;
};

#define BTN_UP     0
#define BTN_DOWN   1
#define BTN_SELECT 2
#define BTN_BACK   3

static Boton botones[4] = {
    {PIN_BTN_UP,     false, false, 0, false, true,  0, 0},
    {PIN_BTN_DOWN,   false, false, 0, false, true,  0, 0},
    {PIN_BTN_SELECT, false, false, 0, false, false, 0, 0},
    {PIN_BTN_BACK,   false, false, 0, false, false, 0, 0},
};

static void leerBotones() {
    unsigned long ahora = millis();
    for (int i = 0; i < 4; i++) {
        bool lectura = (digitalRead(botones[i].pin) == LOW);
        botones[i].disparado = false;

        if (lectura != botones[i].estadoAnterior) botones[i].tCambio = ahora;

        if ((ahora - botones[i].tCambio) >= DEBOUNCE_MS) {
            if (lectura != botones[i].estadoFiltrado) {
                botones[i].estadoFiltrado = lectura;
                if (lectura) {
                    botones[i].disparado       = true;
                    botones[i].tPrimerDisparo  = ahora;
                    botones[i].tUltimoRepeat   = ahora;
                }
            }
            // Auto-repeat mientras se mantiene presionado (solo botones con conRepeat)
            if (botones[i].conRepeat && botones[i].estadoFiltrado && !botones[i].disparado) {
                if ((ahora - botones[i].tPrimerDisparo) >= BTN_REPEAT_DELAY_MS &&
                    (ahora - botones[i].tUltimoRepeat)  >= BTN_REPEAT_RATE_MS) {
                    botones[i].disparado     = true;
                    botones[i].tUltimoRepeat = ahora;
                }
            }
        }

        botones[i].estadoAnterior = lectura;
    }
}

// ── Máquina de estados del menú ───────────────────────────────
static void procesarMenu() {
    bool arr = botones[BTN_UP].disparado;
    bool aba = botones[BTN_DOWN].disparado;
    bool sel = botones[BTN_SELECT].disparado;
    bool atr = botones[BTN_BACK].disparado;

    switch (estadoActual) {

        case MENU_HOME:
            if (arr || aba) g_homePage = 1 - g_homePage;  // alterna entre página 1 y 2
            if (sel) { estadoActual = MENU_ACTUADORES; cursorMenu = 0; }
            if (atr) { estadoActual = MENU_CONFIG;     cursorMenu = 0; }
            break;

        case MENU_ACTUADORES:
            if (arr) cursorMenu = max(0, cursorMenu - 1);
            if (aba) cursorMenu = min(NUM_ACTUADORES - 1, cursorMenu + 1);
            if (sel) {
                cursorActuador = cursorMenu;
                estadoActual   = MENU_CTRL_ACTUADOR;
                // Pre-cargar el índice del ángulo actual del servo
                if (actuadores[cursorActuador].esServo) {
                    for (int i = 0; i < NUM_ANGULOS; i++) {
                        if (ANGULOS_SERVO[i] == actuadores[cursorActuador].angulo)
                            cursorAngulo = i;
                    }
                }
            }
            if (atr) { estadoActual = MENU_HOME; cursorMenu = 0; }
            break;

        case MENU_CTRL_ACTUADOR: {
            Actuador& a = actuadores[cursorActuador];
            if (a.esServo) {
                if (arr) cursorAngulo = max(0, cursorAngulo - 1);
                if (aba) cursorAngulo = min(NUM_ANGULOS - 1, cursorAngulo + 1);
                if (sel) {
                    a.angulo = ANGULOS_SERVO[cursorAngulo];
                    actuatorsApply(cursorActuador);
                    commQueueReport(cursorActuador);  // no bloqueante
                }
            } else {
                if (arr || aba || sel) {
                    a.estado = !a.estado;
                    actuatorsApply(cursorActuador);
                    commQueueReport(cursorActuador);  // no bloqueante
                }
            }
            if (atr) { estadoActual = MENU_ACTUADORES; cursorMenu = cursorActuador; }
            break;
        }

        case MENU_CONFIG:
            // 4 opciones: 0=WiFiInfo 1=ConfigWifi 2=CambiarID 3=Vincular
            if (arr) cursorMenu = max(0, cursorMenu - 1);
            if (aba) cursorMenu = min(3, cursorMenu + 1);
            if (sel) {
                if (cursorMenu == 0) {
                    estadoActual = MENU_WIFI_INFO;
                } else if (cursorMenu == 1) {
                    estadoActual = MENU_WIFI_PORTAL;
                } else if (cursorMenu == 2) {
                    g_editGhId   = commGetGhId();   // pre-carga el ID actual
                    estadoActual = MENU_SET_GHID;
                } else if (cursorMenu == 3) {
                    estadoActual = MENU_VINCULAR;
                }
            }
            if (atr) { estadoActual = MENU_HOME; cursorMenu = 0; }
            break;

        case MENU_WIFI_INFO:
            if (atr || sel) { estadoActual = MENU_CONFIG; cursorMenu = 0; }
            break;

        case MENU_WIFI_PORTAL:
            // SEL confirma → lanza portal (borra creds y reinicia)
            if (sel) {
                displayMensaje("Reiniciando...", "Abre WiFi:", "AgroPulse-Setup", "Pass: invernadero");
                delay(1500);
                commReiniciarWifi();   // no retorna (ESP.restart())
            }
            if (atr) { estadoActual = MENU_CONFIG; cursorMenu = 1; }
            break;

        case MENU_SET_GHID:
            // Edición numérica: ARR +1, ABA -1 (límites 0-999)
            if (arr) g_editGhId = min(g_editGhId + 1, 999);
            if (aba) g_editGhId = max(g_editGhId - 1, 0);
            if (sel) {
                commSetGhId(g_editGhId);
                displayMensaje("ID guardado:", ("ID: " + String(g_editGhId)).c_str(),
                               "Descargando config...", "");
                delay(800);
                // Intenta descargar config inmediatamente con el nuevo ID
                if (commGetStatus().wifiConnected && g_editGhId > 0) {
                    commDownloadConfig();
                }
                estadoActual = MENU_CONFIG;
                cursorMenu   = 2;
            }
            if (atr) { estadoActual = MENU_CONFIG; cursorMenu = 2; }
            break;

        case MENU_VINCULAR:
            if (sel) {
                displayMensaje("Vinculando...", "", "", "");
                if (commVincular()) {
                    displayMensaje("Vinculado!", commGetNombre().c_str(),
                                   ("ID: " + String(commGetGhId())).c_str(), "");
                } else {
                    displayMensaje("Error", "Verifica ID", "y URL del backend", "");
                }
                delay(2000);
                estadoActual = MENU_CONFIG;
                cursorMenu   = 3;
            }
            if (atr) { estadoActual = MENU_CONFIG; cursorMenu = 3; }
            break;
    }
}

// ════════════════════════════════════════════════════════════════
//  SETUP
// ════════════════════════════════════════════════════════════════
void setup() {
    Serial.begin(SERIAL_BAUD_RATE);
    delay(500);
    Serial.println("\n=== AgroPulse Firmware v" FIRMWARE_VERSION " ===");
    Serial.printf("Device: %s\n\n", DEVICE_ID);

    // LED de estado integrado
    pinMode(PIN_LED_STATUS, OUTPUT);
    digitalWrite(PIN_LED_STATUS, HIGH);

    // Inicializar módulos (orden: display primero para feedback visual)
    displayInit();
    sensorsInit();
    actuatorsInit();   // puede llamar actuatorsSetupGPIO() con config de NVS
    commInit();

    // Botones ANTES de WiFi: sin INPUT_PULLUP, PIN_BTN_BACK flota y puede leer
    // LOW aleatoriamente dentro de commConnectWifi(), cerrando el portal antes
    // de que el usuario ingrese credenciales. Los pines de botones (25,23,32,33)
    // no se solapan con los de actuadores (26,27,15,18,19), así que inicializar
    // aquí es seguro. Se re-aplica al final por si commDownloadConfig() los toca.
    for (int i = 0; i < 4; i++) {
        pinMode(botones[i].pin, INPUT_PULLUP);
    }

    // commInit() es ligero (solo NVS + mutex) — no arranca el radio WiFi.
    // El I2C está sano aquí: borrar el splash con un clear rápido es suficiente.
    displayClear();

    // Conexión WiFi con portal de configuración (WiFiManager)
    displayMensaje("Conectando WiFi...", "", "Abre: AgroPulse-Setup", "si no hay red guardada");
    commConnectWifi();
    // commConnectWifi() arranca el transceiver 2.4 GHz, que puede corromper el
    // periférico I2C del ESP32. Esperar 1 s y hacer la recuperación completa de bus.
    delay(1000);
    displayReinit();
    // GPS init ANTES de commStartTask(): Serial2 debe estar listo antes
    // de que la tarea HTTP arranque en Core 0.
    gpsInit();

    // OTA: verifica si hay firmware nuevo ANTES de descargar config.
    // Si hay actualización válida, otaCheck() reinicia el ESP32 y no retorna.
    // Requiere dispositivo ya vinculado (ghId > 0) para autenticarse.
    if (commGetStatus().wifiConnected && commGetGhId() > 0) {
        displayMensaje("Verificando", "actualizacion...", "", "");
        otaCheck();
    }

    // Descarga configuración dinámica (sensores/actuadores) desde el backend.
    // Se hace ANTES de commStartTask() para que solo Core 1 use s_cliente aquí;
    // evita la carrera que causaba LoadProhibited (EXCVADDR 0x10) al arrancar.
    if (commGetStatus().wifiConnected && commGetGhId() > 0) {
        displayMensaje("Descargando", "configuracion...", "", "");
        if (commDownloadConfig()) {
            displayMensaje("Config OK!",
                (String(NUM_SENSORES)   + " sensores").c_str(),
                (String(NUM_ACTUADORES) + " actuadores").c_str(), "");
            delay(1500);
        }

        // Reportar ubicación GPS al arrancar (solo si ya vinculado)
        displayMensaje("Buscando GPS...", "Espera max 10s", "", "");
        double lat, lng;
        if (gpsReadBlocking(lat, lng)) {
            commSendLocation(lat, lng);
            displayMensaje("GPS OK!", ("Lat: " + String(lat, 4)).c_str(),
                           ("Lng: " + String(lng, 4)).c_str(), "");
            delay(1000);
        }
        // Si no hay fix, continúa normalmente sin error visible
    }

    // Recuperación final de I2C: las peticiones HTTP de OTA y descarga de config
    // pueden re-corromper el bus. Este reinit garantiza que loop() arranque
    // con el display funcionando, sin importar lo que ocurrió en setup.
    displayReinit();

    // HTTP task arranca AL FINAL: en este punto setup() ya terminó todos
    // sus requests, así que httpTask (Core 0) y loop() (Core 1) no compiten.
    // El mutex en comm.cpp protege accesos concurrentes desde el menú.
    commStartTask();

    // Re-aplicar INPUT_PULLUP en botones por si commDownloadConfig() o
    // actuatorsSetupGPIO() modificaron algún GPIO compartido.
    for (int i = 0; i < 4; i++) {
        pinMode(botones[i].pin, INPUT_PULLUP);
    }

    // Primera lectura de sensores
    SensorReadings r = sensorsRead();
    sensorsPrintSerial(r);

    Serial.println("[Main] Sistema listo.\n");
    Serial.println("Comandos: status | vincular | reset | auto:on/off");
    Serial.println("  url:URL | codigo:X | nombre:TEXTO | help");
}

// ════════════════════════════════════════════════════════════════
//  LOOP — completamente no bloqueante (3 intervalos independientes)
// ════════════════════════════════════════════════════════════════
void loop() {
    unsigned long ahora = millis();

    // 1. Debounce de botones (cada ciclo)
    leerBotones();

    // 2. Máquina de estados del menú (cada ciclo)
    procesarMenu();

    // 3. (Recuperación periódica de I2C eliminada: Wire.begin() en loop()
    //    causa Interrupt WDT cuando coincide con HTTP responses en Core 0.
    //    El display se estabiliza en setup() con múltiples displayReinit();
    //    si se cuelga en operación normal, un reinicio del ESP32 lo resuelve.)

    // 4. Lectura de sensores cada INTERVALO_LECTURA_MS
    if (ahora - g_tLectura >= INTERVALO_LECTURA_MS) {
        SensorReadings r = sensorsRead();
        sensorsPrintSerial(r);
        if (g_autoMode) {
            actuatorsAutoControl(r, g_thresholds, g_autoState);
        }
        g_tLectura = ahora;
    }

    // 5. Actualización del OLED cada INTERVALO_UI_MS
    if (ahora - g_tUI >= INTERVALO_UI_MS) {
        displayRender(estadoActual, cursorMenu, cursorActuador, cursorAngulo, g_homePage, g_editGhId);
        g_tUI = ahora;
    }

    // 6. Comandos de configuración por Serial
    if (Serial.available()) {
        String cmd = Serial.readStringUntil('\n');
        commProcessSerialCommand(cmd, g_thresholds, g_autoMode);
    }
}
