#ifndef AGROPULSE_ACTUATORS_H
#define AGROPULSE_ACTUATORS_H

#include <Arduino.h>
#include <ESP32Servo.h>
#include "../include/config.h"
#include "sensors.h"

#define MAX_ACTUADORES 8

struct Actuador {
    char  nombre[32];
    char  tipo[16];    // PUMP | FAN | LED | SERVO | RELAY | MOTOR
    int   pin;
    bool  estado;
    int   angulo;
    bool  esServo;
    bool  esRele;
    bool  activeLow;   // true = LOW activa la bobina (HW-383)
    int   backendId;   // ID asignado por el backend (0 = sin configurar)
};

extern Actuador actuadores[MAX_ACTUADORES];
extern int      NUM_ACTUADORES;      // cantidad activa en tiempo de ejecución

extern const int ANGULOS_SERVO[];
extern const int NUM_ANGULOS;

struct ActuatorState {
    bool pump;
    bool fan;
    bool heater;
    bool door;
    ActuatorState() : pump(false), fan(false), heater(false), door(false) {}
};

struct ActuatorThresholds {
    float pumpOnThreshold;
    float pumpOffThreshold;
    float fanOnThreshold;
    float fanOffThreshold;
    float heaterOnThreshold;
    float heaterOffThreshold;

    ActuatorThresholds() :
        pumpOnThreshold(DEFAULT_PUMP_ON_THRESHOLD),
        pumpOffThreshold(DEFAULT_PUMP_OFF_THRESHOLD),
        fanOnThreshold(DEFAULT_FAN_ON_THRESHOLD),
        fanOffThreshold(DEFAULT_FAN_OFF_THRESHOLD),
        heaterOnThreshold(DEFAULT_HEATER_ON_THRESHOLD),
        heaterOffThreshold(DEFAULT_HEATER_OFF_THRESHOLD) {}
};

void          actuatorsInit();
void          actuatorsLoadDynamic();  // carga NVS → array
void          actuatorsSetupGPIO();    // aplica pinMode/attach según array actual
void          actuatorsApply(int idx);
void          actuatorsAutoControl(const SensorReadings& readings,
                                   const ActuatorThresholds& thresholds,
                                   ActuatorState& state);
void          actuatorsReset();
ActuatorState actuatorsGetState();
void          actuatorsPrintStatus();

#endif // AGROPULSE_ACTUATORS_H
