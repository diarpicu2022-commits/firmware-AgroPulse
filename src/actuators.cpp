#include "actuators.h"
#include <Preferences.h>

// ── Globals ────────────────────────────────────────────────────
Actuador actuadores[MAX_ACTUADORES];
int      NUM_ACTUADORES = 0;

static Servo         s_servo;
static ActuatorState s_autoState;

const int ANGULOS_SERVO[] = {0, 45, 90, 135, 180};
const int NUM_ANGULOS     = 5;

// ── Fallback: hardware real del proyecto ──────────────────────
// Motor (relé HW-383), Servo, Motobomba (relé HW-383), LED Strip
static void actuatorsLoadDefaults() {
    NUM_ACTUADORES = 4;
    // activeLow=true: GPIO conectado directamente al módulo HW-383.
    // HW-383: IN=LOW → relé ON, IN=HIGH → relé OFF (lógica active-low).
    const struct {
        const char* nm; const char* ty; int pin; bool srv; bool rel; bool aLow;
    } D[4] = {
        {"Motobomba", "PUMP",  PIN_RELE_BOMBA, false, true,  true},
        {"Motor",     "MOTOR", PIN_RELE_MOTOR, false, true,  true},
        {"Servo",     "SERVO", PIN_SERVO,      true,  false, false},
        {"LED Strip", "LED",   PIN_LED1,       false, true,  true},
    };
    for (int i = 0; i < 4; i++) {
        strncpy(actuadores[i].nombre, D[i].nm, 31);
        strncpy(actuadores[i].tipo,   D[i].ty, 15);
        actuadores[i].pin       = D[i].pin;
        actuadores[i].esServo   = D[i].srv;
        actuadores[i].esRele    = D[i].rel;
        actuadores[i].activeLow = D[i].aLow;
        actuadores[i].estado    = false;
        actuadores[i].angulo    = 90;
        actuadores[i].backendId = 0;
    }
    Serial.println("[Actuators] Config por defecto (4 actuadores: Motobomba/Motor/Servo/LED).");
}

// ── actuatorsLoadDynamic() ────────────────────────────────────
void actuatorsLoadDynamic() {
    Preferences p;
    p.begin("agr_a", true);
    int cnt = p.getInt("cnt", 0);

    if (cnt <= 0) {
        p.end();
        actuatorsLoadDefaults();
        return;
    }

    NUM_ACTUADORES = min(cnt, (int)MAX_ACTUADORES);
    for (int i = 0; i < NUM_ACTUADORES; i++) {
        char k[8];
        sprintf(k, "id_%d", i); actuadores[i].backendId = p.getInt(k, 0);
        sprintf(k, "nm_%d", i); strncpy(actuadores[i].nombre, p.getString(k, "Actuador").c_str(), 31);
        sprintf(k, "ty_%d", i);
        String tipo = p.getString(k, "");
        // Si el tipo sigue vacío o genérico, infiere por nombre
        if (tipo.isEmpty() || tipo == "RELAY") {
            String nomLow = String(actuadores[i].nombre); nomLow.toLowerCase();
            if      (nomLow.indexOf("bomb") >= 0 || nomLow.indexOf("pump") >= 0)  tipo = "PUMP";
            else if (nomLow.indexOf("motor") >= 0)                                 tipo = "MOTOR";
            else if (nomLow.indexOf("fan") >= 0 || nomLow.indexOf("ventil") >= 0) tipo = "FAN";
            else if (nomLow.indexOf("servo") >= 0)                                 tipo = "SERVO";
            else if (nomLow.indexOf("led") >= 0)                                   tipo = "LED";
            else if (nomLow.indexOf("heat") >= 0 || nomLow.indexOf("calef") >= 0) tipo = "HEATER";
            else if (tipo.isEmpty())                                                tipo = "RELAY";
        }
        strncpy(actuadores[i].tipo, tipo.c_str(), 15);

        sprintf(k, "gp_%d", i); actuadores[i].pin       = p.getInt(k, 0);
        sprintf(k, "al_%d", i);
        const char* t = actuadores[i].tipo;
        // Default true para tipos de relé con conexión directa HW-383
        bool defaultAL = (strcmp(t,"PUMP")==0||strcmp(t,"MOTOR")==0||strcmp(t,"FAN")==0||
                          strcmp(t,"RELAY")==0||strcmp(t,"EXTRACTOR")==0||
                          strcmp(t,"WATER_PUMP")==0||strcmp(t,"HEAT_GENERATOR")==0||
                          strcmp(t,"DOOR")==0||strcmp(t,"LED")==0);
        actuadores[i].activeLow = p.getBool(k, defaultAL);
        actuadores[i].esServo = (strcmp(t, "SERVO") == 0);
        actuadores[i].esRele  = (strcmp(t, "RELAY") == 0 || strcmp(t, "PUMP")  == 0 ||
                                 strcmp(t, "FAN")   == 0 || strcmp(t, "MOTOR") == 0);
        // Si el backend no envió gpioPin, asignar pin por defecto según tipo
        if (actuadores[i].pin <= 0) {
            if      (strcmp(t, "PUMP") == 0)                               actuadores[i].pin = PIN_RELE_BOMBA;
            else if (strcmp(t, "MOTOR") == 0 || strcmp(t, "FAN") == 0)    actuadores[i].pin = PIN_RELE_MOTOR;
            else if (strcmp(t, "SERVO") == 0)                              actuadores[i].pin = PIN_SERVO;
            else if (strcmp(t, "LED")   == 0)                              actuadores[i].pin = PIN_LED1;
            if (actuadores[i].pin > 0)
                Serial.printf("  [Config] Act[%d] pin=0 -> fallback GPIO %d por tipo %s\n",
                              i, actuadores[i].pin, t);
        }
        actuadores[i].estado  = false;
        actuadores[i].angulo  = 90;
    }
    p.end();
    Serial.printf("[Actuators] Config cargada: %d actuadores\n", NUM_ACTUADORES);
}

// Pines reservados para botones — ningún actuador puede tocarlos
static bool esPinBoton(int pin) {
    return pin == PIN_BTN_UP   || pin == PIN_BTN_DOWN ||
           pin == PIN_BTN_SELECT || pin == PIN_BTN_BACK;
}

// ── actuatorsSetupGPIO() ──────────────────────────────────────
void actuatorsSetupGPIO() {
    for (int i = 0; i < NUM_ACTUADORES; i++) {
        if (actuadores[i].pin <= 0) continue;

        // Protección: si el backend asignó un pin de botón, lo invalida
        if (esPinBoton(actuadores[i].pin)) {
            Serial.printf("[Actuators] WARN: GPIO %d es pin de botón — actuador '%s' deshabilitado\n",
                          actuadores[i].pin, actuadores[i].nombre);
            actuadores[i].pin = 0;
            continue;
        }

        if (actuadores[i].esServo) {
            s_servo.attach(actuadores[i].pin, 500, 2400);
            s_servo.write(90);
        } else {
            // gpio_reset_pin desconecta el GPIO de cualquier periférico
            // (SPI/LEDC/PWM) que pudiera haberlo tomado, p.ej. GPIO18=VSPI_CLK.
            // Sin esto, el ESP32 puede dejar GPIO18 bajo control SPI y hacer
            // que el relay LED parpadee al ritmo del reloj SPI.
            gpio_reset_pin((gpio_num_t)actuadores[i].pin);
            pinMode(actuadores[i].pin, OUTPUT);
            digitalWrite(actuadores[i].pin, actuadores[i].activeLow ? HIGH : LOW);
        }
    }

    // Garantía final: restaurar INPUT_PULLUP en todos los botones
    // (por si algún driver o librería los tocó antes)
    pinMode(PIN_BTN_UP,     INPUT_PULLUP);
    pinMode(PIN_BTN_DOWN,   INPUT_PULLUP);
    pinMode(PIN_BTN_SELECT, INPUT_PULLUP);
    pinMode(PIN_BTN_BACK,   INPUT_PULLUP);
}

// ── actuatorsInit() ───────────────────────────────────────────
void actuatorsInit() {
    actuatorsLoadDynamic();
    actuatorsSetupGPIO();
    Serial.println("[Actuators] Modulo inicializado.");
}

// ── actuatorsApply() ─────────────────────────────────────────
void actuatorsApply(int idx) {
    if (idx < 0 || idx >= NUM_ACTUADORES) return;
    Actuador& a = actuadores[idx];
    if (a.pin <= 0) return;

    if (a.esServo) {
        s_servo.write(a.angulo);
        Serial.printf("[Actuator] Servo -> %d deg\n", a.angulo);
    } else {
        // activeLow (HW-383): LOW=ON, HIGH=OFF.  Normal: HIGH=ON, LOW=OFF
        digitalWrite(a.pin, (a.activeLow ? !a.estado : a.estado) ? HIGH : LOW);
        Serial.printf("[Actuator] %s -> %s%s\n",
            a.nombre, a.estado ? "ON" : "OFF",
            a.activeLow ? " (aLow)" : "");
    }

    // Actualizar estado de control automático por tipo
    if (strcmp(a.tipo, "PUMP")  == 0) s_autoState.pump = a.estado;
    if (strcmp(a.tipo, "FAN")   == 0 ||
        strcmp(a.tipo, "MOTOR") == 0) s_autoState.fan  = a.estado;
}

// ── actuatorsAutoControl() ────────────────────────────────────
void actuatorsAutoControl(const SensorReadings& readings,
                          const ActuatorThresholds& thresholds,
                          ActuatorState& state) {
    if (!readings.valid) return;

    for (int i = 0; i < NUM_ACTUADORES; i++) {
        if (actuadores[i].esServo) continue;
        const char* t = actuadores[i].tipo;

        if (strcmp(t, "PUMP") == 0) {
            if (!state.pump && readings.soilMoisture < thresholds.pumpOnThreshold) {
                actuadores[i].estado = true;
                actuatorsApply(i);
                state.pump = true;
                Serial.println("[Auto] Bomba ACTIVADA — suelo seco");
            } else if (state.pump && readings.soilMoisture > thresholds.pumpOffThreshold) {
                actuadores[i].estado = false;
                actuatorsApply(i);
                state.pump = false;
                Serial.println("[Auto] Bomba DESACTIVADA — suelo húmedo");
            }
        }
        if (strcmp(t, "FAN") == 0 || strcmp(t, "MOTOR") == 0) {
            if (!state.fan && readings.tempInterior > thresholds.fanOnThreshold) {
                actuadores[i].estado = true;
                actuatorsApply(i);
                state.fan = true;
                Serial.printf("[Auto] %s ACTIVADO — temperatura alta\n", actuadores[i].nombre);
            } else if (state.fan && readings.tempInterior < thresholds.fanOffThreshold) {
                actuadores[i].estado = false;
                actuatorsApply(i);
                state.fan = false;
                Serial.printf("[Auto] %s DESACTIVADO — temperatura normal\n", actuadores[i].nombre);
            }
        }
    }
}

// ── actuatorsReset() ─────────────────────────────────────────
void actuatorsReset() {
    for (int i = 0; i < NUM_ACTUADORES; i++) {
        actuadores[i].estado = false;
        actuadores[i].angulo = 90;
        if (actuadores[i].pin <= 0) continue;
        if (actuadores[i].esServo) {
            s_servo.write(90);
        } else {
            digitalWrite(actuadores[i].pin, actuadores[i].activeLow ? HIGH : LOW);
        }
    }
    s_autoState = ActuatorState();
    Serial.println("[Actuators] Reset completo.");
}

ActuatorState actuatorsGetState() { return s_autoState; }

void actuatorsPrintStatus() {
    Serial.println("[Actuators] ─────────────────────────");
    for (int i = 0; i < NUM_ACTUADORES; i++) {
        if (actuadores[i].esServo) {
            Serial.printf("  %-8s: %d deg (id=%d)\n",
                actuadores[i].nombre, actuadores[i].angulo, actuadores[i].backendId);
        } else {
            Serial.printf("  %-8s: %s  gpio=%d  aLow=%d  (id=%d)\n",
                actuadores[i].nombre,
                actuadores[i].estado ? "ON " : "OFF",
                actuadores[i].pin,
                actuadores[i].activeLow,
                actuadores[i].backendId);
        }
    }
}
