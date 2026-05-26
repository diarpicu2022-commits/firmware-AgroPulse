#ifndef AGROPULSE_SENSORS_H
#define AGROPULSE_SENSORS_H

#include <Arduino.h>
#include <DHT.h>
#include "../include/config.h"

#define MAX_SENSORES 8

// Campos mutables: se cargan desde NVS o fallback hardcodeado
struct Sensor {
    char  nombre[32];
    char  tipo[24];      // TEMPERATURE | HUMIDITY | SOIL_MOISTURE | LIGHT | CO2 | PRESSURE
    char  protocol[16];  // DHT22 | DHT11 | ADC | ANALOG | DIGITAL
    int   gpioPin;
    int   backendId;     // ID asignado por el backend (0 = sin configurar)
    bool  conectado;
    float valor;
    char  unidad[8];
    char  icono[8];      // TEMP | GOTA | PLANTA
};

extern Sensor sensores[MAX_SENSORES];
extern int    NUM_SENSORES;          // cantidad activa en tiempo de ejecución

struct SensorReadings {
    float tempInterior;
    float tempExterior;
    float humidityAir;
    float soilMoisture;
    bool  valid;
    unsigned long timestamp;

    SensorReadings() :
        tempInterior(0), tempExterior(0),
        humidityAir(0), soilMoisture(0),
        valid(false), timestamp(0) {}
};

void           sensorsInit();
void           sensorsLoadDynamic(); // carga NVS → array (fallback a defaults si vacío)
SensorReadings sensorsRead();
void           sensorsPrintSerial(const SensorReadings& r);

#endif // AGROPULSE_SENSORS_H
