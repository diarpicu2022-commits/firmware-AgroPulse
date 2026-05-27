#include "display.h"
#include "comm.h"
#include <WiFi.h>
#include <time.h>

static U8G2_SSD1306_128X64_NONAME_F_HW_I2C oled(U8G2_R0, U8X8_PIN_NONE);

// ── Iconos 8×8 (formato XBM) ──────────────────────────────────
static const uint8_t ICO_TEMP[]   = {0x0C, 0x12, 0x12, 0x1E, 0x1E, 0x3F, 0x3F, 0x1E};
static const uint8_t ICO_GOTA[]   = {0x04, 0x0E, 0x1F, 0x3F, 0x3F, 0x3F, 0x1E, 0x00};
static const uint8_t ICO_PLANTA[] = {0x08, 0x1C, 0x3E, 0x3E, 0x08, 0x08, 0x1C, 0x00};

void displayInit() {
    // Recuperación preventiva del bus I2C antes de Wire.begin():
    // Si la sesión anterior terminó con el bus atascado (SDA en LOW),
    // Wire.begin() falla silenciosamente y el SSD1306 muestra el GRAM previo
    // (el splash) para siempre. Los 9 pulsos de SCL desbloquean al slave
    // y la condición STOP libera el bus antes de inicializar Wire.
    pinMode(PIN_SCL, OUTPUT);
    pinMode(PIN_SDA, OUTPUT);
    digitalWrite(PIN_SDA, HIGH);
    for (int i = 0; i < 9; i++) {
        digitalWrite(PIN_SCL, LOW);  delayMicroseconds(10);
        digitalWrite(PIN_SCL, HIGH); delayMicroseconds(10);
    }
    digitalWrite(PIN_SDA, LOW);  delayMicroseconds(10);
    digitalWrite(PIN_SCL, HIGH); delayMicroseconds(10);
    digitalWrite(PIN_SDA, HIGH);
    delay(20);

    Wire.begin(PIN_SDA, PIN_SCL);
    oled.begin();
    oled.setContrast(220);
    Serial.println("[Display] OLED iniciado OK.");

    oled.clearBuffer();
    oled.setFont(u8g2_font_9x18B_tr);
    oled.drawStr(5, 22, "AgroPulse");
    oled.setFont(u8g2_font_6x10_tr);
    oled.drawStr(10, 38, "Firmware v" FIRMWARE_VERSION);
    oled.drawLine(0, 42, 127, 42);
    oled.setFont(u8g2_font_5x7_tr);
    oled.drawStr(10, 54, "ESP32  Iniciando...");
    oled.sendBuffer();
    delay(2500);
}

// Fast clear: only valid when I2C is known-healthy (before WiFi radio starts).
void displayClear() {
    oled.clearBuffer();
    oled.sendBuffer();
}

// Re-initialize I2C bus and OLED controller after WiFi radio activity.
//
// Wire.end() is intentionally NOT called here.  On this ESP32 silicon revision
// Wire.end() leaves the I2C peripheral in a state that Wire.begin() cannot
// recover, breaking all subsequent I2C transactions permanently.  The ESP32
// Arduino Wire.begin() with the same pins does log "Bus already started" and
// skips the hardware reinit, but the peripheral is still functional — it only
// needs the pin-level bus recovery (9 SCL pulses + STOP) to unlock the slave,
// followed by oled.begin() to resend the SSD1306 initialization sequence.
void displayReinit() {
    // Paso 1: recuperación de bus a nivel de pin (IEEE I2C spec §3.1.16).
    // Poner SDA en HIGH con pines en OUTPUT, pulsar SCL×9 para desbloquear
    // al slave si está reteniendo SDA en LOW. Condición STOP al final.
    pinMode(PIN_SCL, OUTPUT);
    pinMode(PIN_SDA, OUTPUT);
    digitalWrite(PIN_SDA, HIGH);
    digitalWrite(PIN_SCL, HIGH);
    for (int i = 0; i < 9; i++) {
        digitalWrite(PIN_SCL, LOW);  delayMicroseconds(15);
        digitalWrite(PIN_SCL, HIGH); delayMicroseconds(15);
    }
    // Condición STOP: SDA sube mientras SCL está HIGH
    digitalWrite(PIN_SDA, LOW);  delayMicroseconds(15);
    digitalWrite(PIN_SCL, HIGH); delayMicroseconds(15);
    digitalWrite(PIN_SDA, HIGH); delayMicroseconds(15);

    // Paso 2: devolver pines a INPUT_PULLUP ANTES de que Wire tome control.
    // Si quedan como OUTPUT el periférico I2C del ESP32 puede no reconocerlos.
    pinMode(PIN_SCL, INPUT_PULLUP);
    pinMode(PIN_SDA, INPUT_PULLUP);
    delay(30);

    // Paso 3: Wire.begin() — logs "already started" warning but the peripheral
    // IS functional; the call is harmless and ensures frequency is correct.
    Wire.begin(PIN_SDA, PIN_SCL);
    delay(100);

    // Paso 4: reinicializar controlador SSD1306 (envía secuencia de init I2C)
    // y borrar pantalla dos veces para garantizar frame limpio.
    oled.begin();
    oled.setContrast(220);
    delay(50);
    oled.clearBuffer();
    oled.sendBuffer();
    delay(30);
    oled.clearBuffer();
    oled.sendBuffer();
}

void displayMensaje(const char* l1, const char* l2, const char* l3, const char* l4) {
    oled.clearBuffer();
    oled.setFont(u8g2_font_6x10_tr);
    if (l1 && strlen(l1)) oled.drawStr(0, 12, l1);
    if (l2 && strlen(l2)) oled.drawStr(0, 26, l2);
    if (l3 && strlen(l3)) oled.drawStr(0, 40, l3);
    if (l4 && strlen(l4)) oled.drawStr(0, 54, l4);
    oled.sendBuffer();
}

// ── Helpers de dibujo ─────────────────────────────────────────

static void dibujarIcono(int x, int y, const char* tipo) {
    if      (strcmp(tipo, "TEMP") == 0)   oled.drawXBM(x, y, 8, 8, ICO_TEMP);
    else if (strcmp(tipo, "GOTA") == 0)   oled.drawXBM(x, y, 8, 8, ICO_GOTA);
    else                                   oled.drawXBM(x, y, 8, 8, ICO_PLANTA);
}

static void dibujarBarra(int x, int y, int ancho, int alto, float pct) {
    oled.drawFrame(x, y, ancho, alto);
    int relleno = (int)(pct / 100.0f * (float)(ancho - 2));
    if (relleno > 0) oled.drawBox(x + 1, y + 1, relleno, alto - 2);
}

// ── Pantalla principal (2 páginas, 3 sensores cada una) ───────
static void dibujarHome(int pagina) {
    oled.setFont(u8g2_font_6x10_tr);
    String titulo = commGetNombre();
    if (titulo.length() > 14) titulo = titulo.substring(0, 14);
    oled.drawStr(0, 10, titulo.c_str());

    struct tm ti;
    if (getLocalTime(&ti, 0)) {
        char hora[6];
        sprintf(hora, "%02d:%02d", ti.tm_hour, ti.tm_min);
        oled.drawStr(93, 10, hora);
    }
    if (!WiFi.isConnected()) {
        oled.setFont(u8g2_font_4x6_tr);
        oled.drawStr(86, 10, "noWiFi");
    }
    oled.drawLine(0, 12, 127, 12);

    // 3 sensores por página: página 0 → índices 0-2, página 1 → índices 3-5
    oled.setFont(u8g2_font_6x10_tr);
    int inicio = pagina * 3;
    int fin    = min(inicio + 3, (int)NUM_SENSORES);
    int y      = 25;
    bool alguno = false;

    for (int i = inicio; i < fin; i++) {
        dibujarIcono(0, y - 8, sensores[i].icono);
        char buf[22];
        if (!sensores[i].conectado) {
            sprintf(buf, "%-9s  ---", sensores[i].nombre);
            oled.drawStr(10, y, buf);
        } else {
            alguno = true;
            bool esHumedad = (strcmp(sensores[i].icono, "GOTA") == 0 ||
                              strcmp(sensores[i].tipo,  "SOIL_MOISTURE") == 0);
            if (esHumedad) {
                sprintf(buf, "%-9s%3.0f%%", sensores[i].nombre, sensores[i].valor);
                oled.drawStr(10, y, buf);
                dibujarBarra(95, y - 7, 32, 8, sensores[i].valor);
            } else {
                sprintf(buf, "%-9s%5.1f%s", sensores[i].nombre, sensores[i].valor, sensores[i].unidad);
                oled.drawStr(10, y, buf);
            }
        }
        y += 13;
    }
    if (!alguno && inicio == 0) {
        oled.drawStr(15, 40, "Detectando...");
    }

    // Indicador de página + controles
    oled.setFont(u8g2_font_4x6_tr);
    char footer[26];
    sprintf(footer, "[%d/2]ARR/ABA  SEL  ATR", pagina + 1);
    oled.drawStr(0, 63, footer);
}

// ── Lista de actuadores (ventana deslizante de 3) ─────────────
static void dibujarActuadores(int cursorMenu) {
    oled.setFont(u8g2_font_6x10_tr);
    oled.drawStr(0, 10, "Actuadores");
    oled.drawLine(0, 12, 127, 12);

    // Ventana de 3 ítems centrada en el cursor
    int inicio = max(0, min(cursorMenu - 1, NUM_ACTUADORES - 3));

    for (int i = inicio; i < NUM_ACTUADORES && i < inicio + 3; i++) {
        int  y   = 25 + (i - inicio) * 13;
        bool sel = (i == cursorMenu);
        if (sel) { oled.drawRBox(0, y - 9, 118, 12, 2); oled.setDrawColor(0); }
        char buf[22];
        if (actuadores[i].esServo)
            sprintf(buf, "%-8s  %3ddeg", actuadores[i].nombre, actuadores[i].angulo);
        else
            sprintf(buf, "%-8s  %s", actuadores[i].nombre, actuadores[i].estado ? " ON" : "OFF");
        oled.drawStr(3, y, buf);
        if (sel) oled.setDrawColor(1);
    }

    // Indicadores de scroll (columna derecha)
    oled.setFont(u8g2_font_4x6_tr);
    if (inicio > 0)                    oled.drawStr(121, 22, "^");
    if (inicio + 3 < NUM_ACTUADORES)   oled.drawStr(121, 57, "v");

    oled.drawStr(0, 63, "ARR/ABA  SEL:ctrl  ATR:home");
}

// ── Control de actuador individual ───────────────────────────
static void dibujarCtrlActuador(int cursorActuador, int cursorAngulo) {
    Actuador& a = actuadores[cursorActuador];
    oled.setFont(u8g2_font_7x13B_tr);
    oled.drawStr(0, 14, a.nombre);
    oled.drawLine(0, 16, 127, 16);

    if (a.esServo) {
        oled.setFont(u8g2_font_6x10_tr);
        oled.drawStr(0, 30, "Angulo:");
        for (int i = 0; i < NUM_ANGULOS; i++) {
            int  x = 4 + i * 24;
            char buf[5];
            sprintf(buf, "%3d", ANGULOS_SERVO[i]);
            if (i == cursorAngulo) {
                oled.drawRBox(x - 2, 32, 22, 14, 2);
                oled.setDrawColor(0);
                oled.drawStr(x, 43, buf);
                oled.setDrawColor(1);
            } else {
                oled.drawRFrame(x - 2, 32, 22, 14, 2);
                oled.drawStr(x, 43, buf);
            }
        }
        oled.setFont(u8g2_font_4x6_tr);
        char actBuf[20];
        sprintf(actBuf, "Actual: %d deg", a.angulo);
        oled.drawStr(0, 56, actBuf);
        oled.drawStr(0, 63, "ARR/ABA:selec  SEL:aplicar");
    } else {
        oled.setFont(u8g2_font_9x18B_tr);
        const char* etiqueta = a.estado ? "ENCENDIDO" : "APAGADO";
        int w = oled.getStrWidth(etiqueta);
        oled.drawStr((128 - w) / 2, 48, etiqueta);
        if (a.estado) oled.drawDisc(12, 38, 9);
        else          oled.drawCircle(12, 38, 9);
        oled.setFont(u8g2_font_4x6_tr);
        oled.drawStr(0, 63, "SEL/ARR/ABA:toggle  ATR:volver");
    }
}

// ── Menú configuración (ventana deslizante de 3) ─────────────
static void dibujarConfig(int cursorMenu) {
    // 0=WiFi Info, 1=Config WiFi (portal), 2=Cambiar ID, 3=Vincular
    const char* opciones[] = {
        " WiFi Info",
        " Config WiFi",
        " Cambiar ID GH",
        " Vincular"
    };
    oled.setFont(u8g2_font_6x10_tr);
    oled.drawStr(0, 10, "Configuracion");
    oled.drawLine(0, 12, 127, 12);

    // Ventana de 3 ítems centrada en el cursor (igual que actuadores)
    int inicio = max(0, min(cursorMenu - 1, 4 - 3));
    for (int i = inicio; i < 4 && i < inicio + 3; i++) {
        int  y   = 25 + (i - inicio) * 13;
        bool sel = (i == cursorMenu);
        if (sel) { oled.drawRBox(0, y - 9, 118, 12, 2); oled.setDrawColor(0); }
        oled.drawStr(4, y, opciones[i]);
        if (sel) oled.setDrawColor(1);
    }

    // Indicadores de scroll
    oled.setFont(u8g2_font_4x6_tr);
    if (inicio > 0)       oled.drawStr(121, 22, "^");
    if (inicio + 3 < 4)   oled.drawStr(121, 57, "v");
    oled.drawStr(0, 63, "ARR/ABA:nav  SEL:entrar  ATR:home");
}

// ── Confirmación portal WiFi ──────────────────────────────────
static void dibujarWifiPortal() {
    oled.setFont(u8g2_font_6x10_tr);
    oled.drawStr(0, 10, "Config WiFi");
    oled.drawLine(0, 12, 127, 12);
    oled.setFont(u8g2_font_5x7_tr);
    oled.drawStr(0, 27, "Conecta tu celular a:");
    oled.drawStr(0, 38, "WiFi: AgroPulse-Setup");
    oled.drawStr(0, 49, "Pass: invernadero");
    oled.drawStr(0, 60, "y edita URL + ID GH");
    oled.setFont(u8g2_font_4x6_tr);
    oled.drawStr(0, 63, "SEL:activar  ATR:cancelar");
}

// ── Editor de ID del invernadero ──────────────────────────────
static void dibujarSetGhId(int editGhId) {
    oled.setFont(u8g2_font_6x10_tr);
    oled.drawStr(0, 10, "ID Invernadero");
    oled.drawLine(0, 12, 127, 12);

    // Valor actual grande y centrado
    oled.setFont(u8g2_font_10x20_tr);
    char buf[8];
    sprintf(buf, "%d", editGhId);
    int w = oled.getStrWidth(buf);
    oled.drawStr((128 - w) / 2, 44, buf);

    oled.setFont(u8g2_font_5x7_tr);
    oled.drawStr(12, 56, "ARR +1   ABA -1");
    oled.setFont(u8g2_font_4x6_tr);
    oled.drawStr(0, 63, "SEL:guardar  ATR:cancelar");
}

// ── Estado WiFi ───────────────────────────────────────────────
static void dibujarWifi() {
    oled.setFont(u8g2_font_6x10_tr);
    oled.drawStr(0, 10, "Estado WiFi");
    oled.drawLine(0, 12, 127, 12);
    if (WiFi.isConnected()) {
        oled.drawStr(0, 26, "CONECTADO");
        String ssid = "Red: " + WiFi.SSID();
        oled.drawStr(0, 39, ssid.c_str());
        oled.drawStr(0, 52, WiFi.localIP().toString().c_str());
    } else {
        oled.drawStr(0, 28, "SIN CONEXION");
        oled.setFont(u8g2_font_5x7_tr);
        oled.drawStr(0, 42, "Abre: AgroPulse-Setup");
        oled.drawStr(0, 52, "Pass: invernadero");
    }
    oled.setFont(u8g2_font_4x6_tr);
    oled.drawStr(0, 63, "ATR: volver");
}

// ── Pantalla vinculación ──────────────────────────────────────
static void dibujarVincular() {
    oled.setFont(u8g2_font_6x10_tr);
    oled.drawStr(0, 10, "Vincular Backend");
    oled.drawLine(0, 12, 127, 12);
    oled.setFont(u8g2_font_5x7_tr);

    String ghIdStr   = "ID: " + (commGetCodigo().isEmpty() ? "--" : commGetCodigo());
    String vinculado = "Vinculado: " + (commGetGhId() > 0 ? String(commGetGhId()) : "No");
    oled.drawStr(0, 26, ghIdStr.c_str());
    oled.drawStr(0, 38, vinculado.c_str());

    if (!commGetUrl().isEmpty()) {
        // Muestra la URL sin "https://"
        int start = commGetUrl().startsWith("https://") ? 8 : 0;
        String urlCorta = commGetUrl().substring(start, min((int)commGetUrl().length(), start + 22));
        oled.drawStr(0, 50, urlCorta.c_str());
    } else {
        oled.drawStr(0, 50, "Sin URL configurada");
    }
    oled.setFont(u8g2_font_4x6_tr);
    oled.drawStr(0, 63, "SEL:vincular  ATR:volver");
}

// ── Dispatcher principal ──────────────────────────────────────
void displayRender(EstadoMenu estado, int cursorMenu,
                   int cursorActuador, int cursorAngulo,
                   int homePage, int editGhId) {
    oled.clearBuffer();
    switch (estado) {
        case MENU_HOME:          dibujarHome(homePage);                             break;
        case MENU_ACTUADORES:    dibujarActuadores(cursorMenu);                     break;
        case MENU_CTRL_ACTUADOR: dibujarCtrlActuador(cursorActuador, cursorAngulo); break;
        case MENU_CONFIG:        dibujarConfig(cursorMenu);                         break;
        case MENU_WIFI_INFO:     dibujarWifi();                                     break;
        case MENU_VINCULAR:      dibujarVincular();                                 break;
        case MENU_WIFI_PORTAL:   dibujarWifiPortal();                               break;
        case MENU_SET_GHID:      dibujarSetGhId(editGhId);                          break;
    }
    oled.sendBuffer();
}
