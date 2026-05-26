#ifndef AGROPULSE_DISPLAY_H
#define AGROPULSE_DISPLAY_H

/*
 * display.h — Módulo de pantalla OLED (U8g2)
 *
 * SRP: solo actualiza la pantalla. No toma decisiones de negocio.
 * Usa la librería U8g2 para mayor control gráfico que Adafruit SSD1306:
 * permite iconos XBM, barras de progreso y fuentes variables.
 */

#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>
#include "sensors.h"
#include "actuators.h"
#include "../include/config.h"

// ── Estados del menú (también usados en main.cpp) ─────────────
enum EstadoMenu {
    MENU_HOME,
    MENU_ACTUADORES,
    MENU_CTRL_ACTUADOR,
    MENU_CONFIG,
    MENU_WIFI_INFO,
    MENU_VINCULAR,
    MENU_WIFI_PORTAL,   // confirmación antes de re-lanzar portal WiFiManager
    MENU_SET_GHID,      // edición numérica del ID del invernadero con ▲▼
};

// ── Interfaz pública ───────────────────────────────────────────
void displayInit();
void displayReinit();   // Re-init I2C + OLED after WiFi radio startup
void displayMensaje(const char* l1, const char* l2 = "",
                    const char* l3 = "", const char* l4 = "");
void displayRender(EstadoMenu estado, int cursorMenu,
                   int cursorActuador, int cursorAngulo,
                   int homePage, int editGhId = 0);

#endif // AGROPULSE_DISPLAY_H
