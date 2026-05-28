#include "comm.h"
#include "display.h"
#include "gps.h"
#include <WiFiManager.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <time.h>

static CommStatus         s_status;
static WiFiClientSecure   s_cliente;
static Preferences        s_prefs;
static bool               s_sensorsRegistered = false;
// Mutex: protege s_cliente ante acceso simultáneo desde Core 0 (httpTask) y Core 1 (menú)
static SemaphoreHandle_t  s_httpMutex = nullptr;

static String s_nombreInvernadero = "Mi Invernadero";
static String s_codigoVinculacion = "";

// ── Accesores públicos ─────────────────────────────────────────
const String& commGetNombre() { return s_nombreInvernadero; }
const String& commGetCodigo() { return s_codigoVinculacion; }
int           commGetGhId()   { return s_status.greenhouseId; }
const String& commGetUrl()    { return s_status.backendUrl; }

// ── NVS — configuración general (URL, ghId, nombre) ───────────
static void cargarConfig() {
    s_prefs.begin("agropulse", true);
    s_nombreInvernadero   = s_prefs.getString("nombre", "Mi Invernadero");
    s_status.backendUrl   = s_prefs.getString("url",    "");
    s_codigoVinculacion   = s_prefs.getString("codigo", "");
    s_status.greenhouseId = s_prefs.getInt("ghId",      0);
    s_prefs.end();
    Serial.printf("[Config] Nombre:%s | Backend:%s | GH ID:%d\n",
        s_nombreInvernadero.c_str(), s_status.backendUrl.c_str(),
        s_status.greenhouseId);
}

static void guardarConfig() {
    s_prefs.begin("agropulse", false);
    s_prefs.putString("nombre", s_nombreInvernadero);
    s_prefs.putString("url",    s_status.backendUrl);
    s_prefs.putString("codigo", s_codigoVinculacion);
    s_prefs.putInt("ghId",      s_status.greenhouseId);
    s_prefs.end();
    Serial.println("[Config] Guardado en NVS.");
}

// ── commInit() ────────────────────────────────────────────────
void commInit() {
    cargarConfig();
    s_cliente.setInsecure();
    s_httpMutex = xSemaphoreCreateMutex();
    Serial.println("[Comm] Modulo inicializado.");
}

// ── Macro interna: toma mutex antes de HTTP, libera al salir ──
// Timeout 8 s: suficiente para un request HTTPS lento, nunca bloquea
// el scheduler indefinidamente.
#define HTTP_LOCK()   if (!s_httpMutex || xSemaphoreTake(s_httpMutex, pdMS_TO_TICKS(8000)) != pdTRUE) { return false; }
#define HTTP_UNLOCK() xSemaphoreGive(s_httpMutex)

// ── commConnectWifi() — WiFiManager no bloqueante ─────────────
bool commConnectWifi() {
    // Desconectar cualquier intento previo y limpiar estado WiFi
    WiFi.disconnect(false);
    delay(100);

    WiFiManager wm;
    wm.setConnectTimeout(15);          // era 8 — algunos routers necesitan más tiempo
    wm.setConnectRetries(2);           // era 1 — un reintento extra ante ruido WiFi
    wm.setConfigPortalBlocking(false);
    wm.setConfigPortalTimeout(120);    // portal se apaga solo en 2 min si nadie conecta
    wm.setBreakAfterConfig(true);      // cierra el portal tras guardar credenciales,
                                       // aunque la conexión falle; evita portal zombie

    WiFiManagerParameter paramUrl("url", "URL Backend AgroPulse",
                                   s_status.backendUrl.c_str(), 80);
    WiFiManagerParameter paramCodigo("codigo", "ID Invernadero (opcional)",
                                      s_codigoVinculacion.c_str(), 10);
    wm.addParameter(&paramUrl);
    wm.addParameter(&paramCodigo);

    bool ok = wm.autoConnect("AgroPulse-Setup", "invernadero");

    if (!ok) {
        displayMensaje("Portal WiFi activo", "Red: AgroPulse-Setup", "Pass: invernadero", "BTN BACK = omitir");
        Serial.println("[WiFi] Portal activo. Presiona BTN_BACK para omitir.");
        unsigned long portalInicio = millis();
        while (!WiFi.isConnected()) {
            wm.process();
            // setBreakAfterConfig(true) cerrará el portal tras guardar credenciales;
            // este timeout es el respaldo si el portal queda abierto sin actividad.
            if (millis() - portalInicio > 120000UL) {
                wm.stopConfigPortal();
                Serial.println("[WiFi] Portal timeout — modo offline.");
                break;
            }
            if (digitalRead(PIN_BTN_BACK) == LOW) {
                delay(DEBOUNCE_MS);
                if (digitalRead(PIN_BTN_BACK) == LOW) {
                    wm.stopConfigPortal();
                    Serial.println("[WiFi] Portal omitido — modo offline.");
                    break;
                }
            }
            delay(10);
        }
        ok = WiFi.isConnected();

        // Feedback visual inmediato: el usuario sabe si conectó o quedó offline
        if (ok) {
            displayMensaje("WiFi Conectado!", WiFi.SSID().c_str(),
                           WiFi.localIP().toString().c_str(), "");
            delay(1500);
        } else {
            displayMensaje("Sin WiFi", "Modo offline", "Usa BTN BACK", "para omitir");
            delay(1200);
        }
    }

    if (ok) {
        s_status.wifiConnected = true;
        String urlP    = String(paramUrl.getValue());
        String codigoP = String(paramCodigo.getValue());
        if (urlP.length() > 4)    { s_status.backendUrl   = urlP;    guardarConfig(); }
        if (codigoP.length() > 0) { s_codigoVinculacion   = codigoP; guardarConfig(); }
        configTime(NTP_GMT_OFF, NTP_DST_OFF, NTP_SERVIDOR);
        Serial.printf("[WiFi] Conectado. IP: %s\n", WiFi.localIP().toString().c_str());
    } else {
        s_status.wifiConnected = false;
        Serial.println("[WiFi] Sin conexion — modo offline.");
    }
    return ok;
}

// ── commSendReadings() — POST /api/readings por sensor ────────
bool commSendReadings() {
    if (!s_status.wifiConnected || s_status.backendUrl.isEmpty() ||
        s_status.greenhouseId <= 0) {
        for (int i = 0; i < NUM_SENSORES; i++) {
            if (!sensores[i].conectado) continue;
            Serial.printf("[Serial] %s: %.1f %s\n",
                sensores[i].nombre, sensores[i].valor, sensores[i].unidad);
        }
        return false;
    }

    HTTP_LOCK();
    bool allOk = true;
    Serial.println("[HTTP] Enviando lecturas...");
    for (int i = 0; i < NUM_SENSORES; i++) {
        if (!sensores[i].conectado) continue;

        StaticJsonDocument<256> doc;
        doc["sensorId"]     = sensores[i].backendId > 0 ? sensores[i].backendId : (i + 1);
        doc["greenhouseId"] = s_status.greenhouseId;
        doc["value"]        = round(sensores[i].valor * 10.0f) / 10.0f;
        doc["sensorType"]   = sensores[i].tipo;
        doc["source"]       = DEVICE_ID;

        String payload;
        serializeJson(doc, payload);

        HTTPClient http;
        http.begin(s_cliente, s_status.backendUrl + "/api/readings");
        http.addHeader("Content-Type", "application/json");
        http.setTimeout(TIMEOUT_HTTP_MS);
        int code = http.POST(payload);
        Serial.printf("  %s (id=%d) -> HTTP %d\n",
            sensores[i].nombre, sensores[i].backendId, code);
        http.end();
        if (code != 200 && code != 201) { allOk = false; s_status.failedAttempts++; }
    }
    HTTP_UNLOCK();
    if (allOk) { s_status.lastSuccessfulSend = millis(); s_status.failedAttempts = 0; }
    return allOk;
}

// ── commSyncActuators() — GET /api/actuators?gh=X → aplica ───
bool commSyncActuators() {
    if (!s_status.wifiConnected || s_status.backendUrl.isEmpty() ||
        s_status.greenhouseId <= 0) return false;

    HTTP_LOCK();
    HTTPClient http;
    String url = s_status.backendUrl + "/api/actuators?greenhouseId=" +
                 String(s_status.greenhouseId);
    http.begin(s_cliente, url);
    http.setTimeout(TIMEOUT_HTTP_MS);
    int code = http.GET();

    if (code == 200) {
        String resp = http.getString();
        DynamicJsonDocument doc(2048);
        if (!deserializeJson(doc, resp)) {
            JsonArray lista = doc["actuators"].as<JsonArray>();
            for (JsonObject obj : lista) {
                int    objId  = obj["id"] | 0;
                String nombre = obj["name"] | "";
                bool   nuevo  = (String(obj["status"] | "OFF") == "ON");
                for (int i = 0; i < NUM_ACTUADORES; i++) {
                    if (actuadores[i].esServo) continue;
                    // Preferencia: match por backendId; fallback por nombre
                    bool match = (objId > 0 && actuadores[i].backendId == objId) ||
                                 (actuadores[i].backendId == 0 && nombre == actuadores[i].nombre);
                    if (match && actuadores[i].estado != nuevo) {
                        actuadores[i].estado = nuevo;
                        actuatorsApply(i);
                        break;
                    }
                }
            }
        }
    }
    http.end();
    HTTP_UNLOCK();
    return (code == 200);
}

// ── commReportActuator() — PUT si tiene id, POST si es nuevo ──
bool commReportActuator(int idx) {
    if (!s_status.wifiConnected || s_status.backendUrl.isEmpty() ||
        s_status.greenhouseId <= 0 || idx < 0 || idx >= NUM_ACTUADORES)
        return false;

    Actuador& a = actuadores[idx];
    StaticJsonDocument<256> doc;
    doc["name"]         = a.nombre;
    doc["status"]       = (a.esServo || a.estado) ? "ON" : "OFF";
    doc["type"]         = a.tipo;
    doc["greenhouseId"] = s_status.greenhouseId;
    doc["active"]       = true;
    doc["gpioPin"]      = a.pin;
    doc["activeLow"]    = a.activeLow;

    String payload;
    serializeJson(doc, payload);

    HTTP_LOCK();
    HTTPClient http;
    int code;
    if (a.backendId > 0) {
        http.begin(s_cliente, s_status.backendUrl + "/api/actuators/" + String(a.backendId));
        http.addHeader("Content-Type", "application/json");
        http.setTimeout(TIMEOUT_HTTP_MS);
        code = http.PUT(payload);
    } else {
        http.begin(s_cliente, s_status.backendUrl + "/api/actuators");
        http.addHeader("Content-Type", "application/json");
        http.setTimeout(TIMEOUT_HTTP_MS);
        code = http.POST(payload);
        if (code == 200 || code == 201) {
            String resp = http.getString();
            StaticJsonDocument<256> rdoc;
            if (!deserializeJson(rdoc, resp)) {
                int newId = rdoc["id"] | 0;
                if (newId > 0) {
                    a.backendId = newId;
                    Preferences p;
                    p.begin("agr_a", false);
                    char k[8];
                    sprintf(k, "id_%d", idx);
                    p.putInt(k, newId);
                    p.end();
                    Serial.printf("[Actuator] BackendId guardado: %s -> id=%d\n", a.nombre, newId);
                }
            }
        }
    }
    Serial.printf("[HTTP] Actuador %s (id=%d) -> HTTP %d\n", a.nombre, a.backendId, code);
    http.end();
    HTTP_UNLOCK();
    return (code == 200 || code == 201);
}

// ── commReportSensor() — POST si nuevo, PUT si ya existe ──────
bool commReportSensor(int idx) {
    if (!s_status.wifiConnected || s_status.backendUrl.isEmpty() ||
        s_status.greenhouseId <= 0 || idx < 0 || idx >= NUM_SENSORES)
        return false;

    Sensor& s = sensores[idx];
    StaticJsonDocument<256> doc;
    doc["name"]         = s.nombre;
    doc["type"]         = s.tipo;
    doc["protocol"]     = s.protocol;
    doc["gpioPin"]      = s.gpioPin;
    doc["greenhouseId"] = s_status.greenhouseId;
    doc["active"]       = true;
    doc["deviceSource"] = DEVICE_ID;

    String payload;
    serializeJson(doc, payload);

    HTTP_LOCK();
    HTTPClient http;
    int code;
    if (s.backendId > 0) {
        http.begin(s_cliente, s_status.backendUrl + "/api/sensors/" + String(s.backendId));
        http.addHeader("Content-Type", "application/json");
        http.setTimeout(TIMEOUT_HTTP_MS);
        code = http.PUT(payload);
    } else {
        http.begin(s_cliente, s_status.backendUrl + "/api/sensors");
        http.addHeader("Content-Type", "application/json");
        http.setTimeout(TIMEOUT_HTTP_MS);
        code = http.POST(payload);
        if (code == 200 || code == 201) {
            String resp = http.getString();
            StaticJsonDocument<256> rdoc;
            if (!deserializeJson(rdoc, resp)) {
                int newId = rdoc["id"] | 0;
                if (newId > 0) {
                    s.backendId = newId;
                    Preferences p;
                    p.begin("agr_s", false);
                    char k[8];
                    sprintf(k, "id_%d", idx);
                    p.putInt(k, newId);
                    p.end();
                    Serial.printf("[Sensor] BackendId guardado: %s -> id=%d\n", s.nombre, newId);
                }
            }
        }
    }
    Serial.printf("[HTTP] Sensor %s (id=%d) -> HTTP %d\n", s.nombre, s.backendId, code);
    http.end();
    HTTP_UNLOCK();
    return (code == 200 || code == 201);
}

// ── commDownloadConfig() — GET /api/device/config/{ghId} ─────
// Descarga la config del backend y mapea backendIds a los actuadores/sensores
// del hardware real (por tipo+gpio / tipo+protocolo). Nunca sobrescribe con
// duplicados ni fuerza activeLow — el hardware sabe lo que necesita.
bool commDownloadConfig() {
    if (!s_status.wifiConnected || s_status.backendUrl.isEmpty() ||
        s_status.greenhouseId <= 0) {
        Serial.println("[Config] No se puede descargar: sin WiFi o sin GH ID.");
        return false;
    }

    HTTP_LOCK();
    HTTPClient http;
    String url = s_status.backendUrl + "/api/device/config/" +
                 String(s_status.greenhouseId);
    http.begin(s_cliente, url);
    http.setTimeout(TIMEOUT_HTTP_MS * 2);
    int code = http.GET();

    if (code != 200) {
        Serial.printf("[Config] Error descargando config: HTTP %d\n", code);
        http.end();
        HTTP_UNLOCK();
        return false;
    }

    String resp = http.getString();
    http.end();
    HTTP_UNLOCK();

    Serial.println("[Config] JSON recibido:");
    Serial.println(resp.substring(0, min((int)resp.length(), 600)));

    // 24 KB: suficiente para 130 actuadores duplicados del backend
    DynamicJsonDocument doc(24576);
    if (deserializeJson(doc, resp)) {
        Serial.println("[Config] Error parseando JSON de config.");
        return false;
    }

    JsonArray sensArr = doc["sensors"].as<JsonArray>();
    JsonArray actArr  = doc["actuators"].as<JsonArray>();

    // ── Cargar defaults del hardware (cnt=0 → defaults) ──────
    // Esto resetea a los 5 sensores y 4 actuadores reales del ESP32,
    // independientemente de cuántos duplicados tenga el backend.
    {
        Preferences p;
        p.begin("agr_s", false); p.putInt("cnt", 0); p.end();
        p.begin("agr_a", false); p.putInt("cnt", 0); p.end();
    }
    sensorsLoadDynamic();   // carga 5 sensores del hardware (backendId=0)
    actuatorsLoadDynamic(); // carga 4 actuadores del hardware (backendId=0)

    // ── Mapear backendIds de sensores (match por tipo+protocol) ─
    for (int i = 0; i < NUM_SENSORES; i++) {
        for (JsonObject s : sensArr) {
            String tipo = s["type"] | "";
            String prot = s["protocol"] | "";
            if (tipo == sensores[i].tipo && prot == sensores[i].protocol) {
                sensores[i].backendId = s["id"] | 0;
                break;
            }
        }
        Serial.printf("  [Config] Sensor[%d] %s/%s -> id=%d\n",
            i, sensores[i].tipo, sensores[i].protocol, sensores[i].backendId);
    }
    {
        Preferences p;
        p.begin("agr_s", false);
        p.putInt("cnt", NUM_SENSORES);
        for (int i = 0; i < NUM_SENSORES; i++) {
            char k[8];
            sprintf(k, "id_%d", i); p.putInt(k,    sensores[i].backendId);
            sprintf(k, "nm_%d", i); p.putString(k, sensores[i].nombre);
            sprintf(k, "ty_%d", i); p.putString(k, sensores[i].tipo);
            sprintf(k, "pr_%d", i); p.putString(k, sensores[i].protocol);
            sprintf(k, "gp_%d", i); p.putInt(k,    sensores[i].gpioPin);
        }
        p.end();
    }

    // ── Mapear backendIds de actuadores (match por tipo+gpio) ────
    // Ignora duplicados: solo guarda el primer match por tipo+gpio.
    // activeLow se toma del hardware (actuatorsLoadDefaults), no del backend.
    for (int i = 0; i < NUM_ACTUADORES; i++) {
        for (JsonObject a : actArr) {
            String tipo = a["type"] | "";
            if (tipo.isEmpty()) tipo = a["actuatorType"] | "";
            if (tipo.isEmpty() || tipo == "RELAY") {
                String nomLow = String(a["name"] | ""); nomLow.toLowerCase();
                if      (nomLow.indexOf("bomb") >= 0 || nomLow.indexOf("pump") >= 0)  tipo = "PUMP";
                else if (nomLow.indexOf("motor") >= 0)                                 tipo = "MOTOR";
                else if (nomLow.indexOf("fan") >= 0 || nomLow.indexOf("ventil") >= 0) tipo = "FAN";
                else if (nomLow.indexOf("servo") >= 0)                                 tipo = "SERVO";
                else if (nomLow.indexOf("led") >= 0)                                   tipo = "LED";
            }
            int gpio = a["gpioPin"] | (a["gpio"] | (a["pin"] | 0));
            if (tipo == actuadores[i].tipo && gpio == actuadores[i].pin) {
                actuadores[i].backendId = a["id"] | 0;
                break;
            }
        }
        Serial.printf("  [Config] Act[%d] %s gpio=%d -> id=%d (aLow=%d)\n",
            i, actuadores[i].tipo, actuadores[i].pin,
            actuadores[i].backendId, actuadores[i].activeLow);
    }
    {
        Preferences p;
        p.begin("agr_a", false);
        p.putInt("cnt", NUM_ACTUADORES);
        for (int i = 0; i < NUM_ACTUADORES; i++) {
            char k[8];
            sprintf(k, "id_%d", i); p.putInt(k,    actuadores[i].backendId);
            sprintf(k, "nm_%d", i); p.putString(k, actuadores[i].nombre);
            sprintf(k, "ty_%d", i); p.putString(k, actuadores[i].tipo);
            sprintf(k, "gp_%d", i); p.putInt(k,    actuadores[i].pin);
            sprintf(k, "al_%d", i); p.putBool(k,   actuadores[i].activeLow);
        }
        p.end();
    }

    Serial.printf("[Config] Mapeados: %d sensores, %d actuadores\n",
        NUM_SENSORES, NUM_ACTUADORES);

    s_sensorsRegistered = false;  // re-registrar sensores con ids actualizados
    actuatorsSetupGPIO();
    return true;
}

// ── commVincular() — GET /api/greenhouses/{codigo} ────────────
bool commVincular() {
    if (s_status.backendUrl.isEmpty()) {
        Serial.println("[Vincular] Falta la URL del backend.");
        return false;
    }
    if (s_codigoVinculacion.isEmpty()) {
        Serial.println("[Vincular] Falta el codigo del invernadero.");
        return false;
    }

    HTTP_LOCK();
    HTTPClient http;
    String url = s_status.backendUrl + "/api/greenhouses/" + s_codigoVinculacion;
    http.begin(s_cliente, url);
    http.setTimeout(TIMEOUT_HTTP_MS);
    int code = http.GET();

    if (code == 200) {
        String resp = http.getString();
        StaticJsonDocument<512> doc;
        deserializeJson(doc, resp);
        int id = doc["id"];
        if (id > 0) {
            s_status.greenhouseId = id;
            s_nombreInvernadero   = doc["name"].as<String>();
            guardarConfig();
            Serial.printf("[Vincular] OK — GH ID: %d, Nombre: %s\n",
                s_status.greenhouseId, s_nombreInvernadero.c_str());
            http.end();
            HTTP_UNLOCK();
            // Descargar configuración inmediatamente tras vincular
            commDownloadConfig();
            // Reportar ubicación GPS tras vincular
            displayMensaje("Buscando GPS...", "Espera max 10s", "", "");
            double lat, lng;
            if (gpsReadBlocking(lat, lng)) {
                commSendLocation(lat, lng);
            }
            return true;
        }
    }
    Serial.printf("[Vincular] Error HTTP %d. Verifica el ID.\n", code);
    http.end();
    HTTP_UNLOCK();
    return false;
}

// ── Busca actuador por tipo (para comandos serial) ────────────
static int findActByType(const char* tipo) {
    for (int i = 0; i < NUM_ACTUADORES; i++)
        if (strcmp(actuadores[i].tipo, tipo) == 0) return i;
    return -1;
}

// ── commProcessSerialCommand() ────────────────────────────────
void commProcessSerialCommand(const String& rawCmd,
                              ActuatorThresholds& thresholds,
                              bool& autoMode) {
    String cmd = rawCmd;
    cmd.trim();
    String cmdLow = cmd;
    cmdLow.toLowerCase();

    if (cmdLow == "status") {
        Serial.println("=== STATUS ===");
        Serial.printf("  Nombre:  %s\n", s_nombreInvernadero.c_str());
        Serial.printf("  Backend: %s\n", s_status.backendUrl.isEmpty() ? "(sin configurar)" : s_status.backendUrl.c_str());
        Serial.printf("  GH ID:   %d\n", s_status.greenhouseId);
        Serial.printf("  WiFi:    %s\n", WiFi.isConnected() ? WiFi.SSID().c_str() : "Sin conexion");
        if (WiFi.isConnected()) Serial.printf("  IP:      %s\n", WiFi.localIP().toString().c_str());
        Serial.printf("  Modo:    %s\n", autoMode ? "AUTO" : "MANUAL");
        Serial.printf("  Sensores: %d  Actuadores: %d\n", NUM_SENSORES, NUM_ACTUADORES);
        actuatorsPrintStatus();

    } else if (cmdLow.startsWith("url:")) {
        s_status.backendUrl = cmd.substring(4); s_status.backendUrl.trim();
        guardarConfig();
        Serial.println("[Config] URL: " + s_status.backendUrl);

    } else if (cmdLow.startsWith("codigo:")) {
        s_codigoVinculacion = cmd.substring(7); s_codigoVinculacion.trim();
        guardarConfig();
        Serial.println("[Config] Codigo: " + s_codigoVinculacion);

    } else if (cmdLow.startsWith("nombre:")) {
        s_nombreInvernadero = cmd.substring(7); s_nombreInvernadero.trim();
        guardarConfig();
        Serial.println("[Config] Nombre: " + s_nombreInvernadero);

    } else if (cmdLow == "vincular") {
        commVincular();

    } else if (cmdLow == "config") {
        commDownloadConfig();

    } else if (cmdLow.startsWith("bomba:")) {
        int i = findActByType("PUMP");
        if (i >= 0) { actuadores[i].estado = cmdLow.endsWith("on"); actuatorsApply(i); }

    } else if (cmdLow.startsWith("motor:") || cmdLow.startsWith("fan:")) {
        int i = findActByType("MOTOR");
        if (i < 0) i = findActByType("FAN");
        if (i >= 0) { actuadores[i].estado = cmdLow.endsWith("on"); actuatorsApply(i); }

    } else if (cmdLow.startsWith("servo:")) {
        int i = findActByType("SERVO");
        if (i >= 0) { actuadores[i].angulo = constrain(cmd.substring(6).toInt(), 0, 180); actuatorsApply(i); }

    } else if (cmdLow.startsWith("led1:")) {
        int i = findActByType("LED");
        if (i >= 0) { actuadores[i].estado = cmdLow.endsWith("on"); actuatorsApply(i); }

    } else if (cmdLow == "auto:on")  { autoMode = true;  Serial.println("[Cmd] Modo AUTO ON");  }
    else if (cmdLow == "auto:off") { autoMode = false; Serial.println("[Cmd] Modo AUTO OFF"); }

    else if (cmdLow.startsWith("pump_on:")) {
        thresholds.pumpOnThreshold = cmd.substring(8).toFloat();
        Serial.printf("[Cmd] Umbral bomba ON: %.1f\n", thresholds.pumpOnThreshold);
    } else if (cmdLow.startsWith("pump_off:")) {
        thresholds.pumpOffThreshold = cmd.substring(9).toFloat();
        Serial.printf("[Cmd] Umbral bomba OFF: %.1f\n", thresholds.pumpOffThreshold);
    } else if (cmdLow.startsWith("fan_on:")) {
        thresholds.fanOnThreshold = cmd.substring(7).toFloat();
        Serial.printf("[Cmd] Umbral ventilador ON: %.1f\n", thresholds.fanOnThreshold);

    } else if (cmdLow == "resets") {
        Preferences p;
        p.begin("agr_s", false); p.clear(); p.end();
        Serial.println("[Config] Config sensores borrada. Recargando defaults...");
        sensorsLoadDynamic();  // carga 5 defaults con tipos correctos
        s_sensorsRegistered = false;

    } else if (cmdLow == "resetact") {
        Preferences p;
        p.begin("agr_a", false); p.clear(); p.end();
        Serial.println("[Config] Config actuadores borrada. Recargando defaults...");
        actuatorsLoadDynamic();
        actuatorsSetupGPIO();
        actuatorsPrintStatus();

    } else if (cmdLow == "reset") {
        // Borra config general + config de sensores/actuadores
        Preferences p;
        p.begin("agropulse", false); p.clear(); p.end();
        p.begin("agr_s",    false); p.clear(); p.end();
        p.begin("agr_a",    false); p.clear(); p.end();
        Serial.println("[Config] NVS borrado. Reiniciando...");
        delay(500);
        ESP.restart();

    } else if (cmdLow == "help") {
        Serial.println("Comandos:");
        Serial.println("  status | vincular | config | reset | resetact | resets | auto:on/off");
        Serial.println("  url:URL | codigo:X | nombre:TEXTO");
        Serial.println("  bomba:on/off | motor:on/off | servo:90 | led1:on/off");
        Serial.println("  pump_on:X | pump_off:X | fan_on:X");
        Serial.println("  resetact — borra actuadores y carga defaults");
        Serial.println("  resets   — borra sensores y carga defaults");

    } else if (cmd.length() > 0) {
        Serial.printf("[Cmd] Desconocido: '%s' — escribe 'help'\n", cmd.c_str());
    }
}

CommStatus commGetStatus() { return s_status; }

// ── Tarea HTTP en Core 0 ──────────────────────────────────────
// Mueve TODAS las peticiones HTTP fuera del loop principal.
// El loop() queda libre para botones/display/sensores sin bloqueos.

static QueueHandle_t s_reportQueue = nullptr;

static void httpTask(void*) {
    unsigned long lastSend = 0;
    for (;;) {
        // 1. Registrar sensores en el backend UNA VEZ por sesión.
        //    Si todos los sensores ya tienen backendId (asignado por commDownloadConfig)
        //    se salta el bucle: evita llamadas PUT a /api/sensors/{id} sin JWT que
        //    devuelven 403 porque el endpoint requiere autenticación de usuario.
        if (!s_sensorsRegistered &&
            s_status.wifiConnected && !s_status.backendUrl.isEmpty() &&
            s_status.greenhouseId > 0) {
            bool allHaveId = true;
            for (int i = 0; i < NUM_SENSORES; i++) {
                if (sensores[i].backendId <= 0) { allHaveId = false; break; }
            }
            if (!allHaveId) {
                for (int i = 0; i < NUM_SENSORES; i++) {
                    commReportSensor(i);
                    vTaskDelay(pdMS_TO_TICKS(300));
                }
            }
            s_sensorsRegistered = true;
        }
        // 2. Procesar reportes de actuadores encolados (baja latencia)
        int idx;
        while (s_reportQueue && xQueueReceive(s_reportQueue, &idx, 0) == pdTRUE) {
            commReportActuator(idx);
        }
        // 3. Envío periódico de lecturas + sincronización
        unsigned long now = (unsigned long)(esp_timer_get_time() / 1000ULL);
        if (now - lastSend >= INTERVALO_ENVIO_MS) {
            commSendReadings();
            commSyncActuators();
            lastSend = now;
        }
        vTaskDelay(pdMS_TO_TICKS(50));  // cede al scheduler; no bloquea Core 1
    }
}

void commStartTask() {
    s_reportQueue = xQueueCreate(8, sizeof(int));
    xTaskCreatePinnedToCore(httpTask, "http_task", 8192, NULL, 1, NULL, 0);
    Serial.println("[Comm] Tarea HTTP iniciada en Core 0.");
}

void commQueueReport(int idx) {
    if (s_reportQueue) xQueueSend(s_reportQueue, &idx, 0);
}

// ── commSetGhId() — guarda ID directamente (sin HTTP) ─────────
void commSetGhId(int id) {
    s_status.greenhouseId = id;
    s_codigoVinculacion   = String(id);  // sync so commVincular() uses the updated ID
    guardarConfig();
    Serial.printf("[Config] GH ID actualizado a %d\n", id);
}

// ── commReiniciarWifi() — borra credenciales y reinicia ────────
void commReiniciarWifi() {
    Serial.println("[WiFi] Borrando credenciales y reiniciando...");
    WiFiManager wm;
    wm.resetSettings();   // borra SSID/password guardados en flash
    delay(300);
    ESP.restart();        // en el siguiente boot el portal se abre
}

// ── commSendLocation() — PATCH /api/greenhouses/{ghId}/location ──────
// Usa endpoint dedicado para dispositivos: no requiere JWT ni X-User-Id.
bool commSendLocation(double lat, double lng) {
    if (!s_status.wifiConnected || s_status.backendUrl.isEmpty() || s_status.greenhouseId <= 0) {
        Serial.println("[GPS] Sin WiFi/URL/GH ID — ubicacion no enviada.");
        return false;
    }

    StaticJsonDocument<128> doc;
    doc["latitude"]  = lat;
    doc["longitude"] = lng;
    String payload;
    serializeJson(doc, payload);

    HTTP_LOCK();
    HTTPClient http;
    String url = s_status.backendUrl + "/api/greenhouses/" +
                 String(s_status.greenhouseId) + "/location";
    http.begin(s_cliente, url);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("X-Device-Source", DEVICE_ID);
    http.setTimeout(TIMEOUT_HTTP_MS);
    int code = http.PATCH(payload);
    http.end();
    HTTP_UNLOCK();

    if (code == 200 || code == 201) {
        Serial.printf("[GPS] Ubicacion enviada: lat=%.6f lng=%.6f (HTTP %d)\n", lat, lng, code);
        return true;
    }
    Serial.printf("[GPS] Error enviando ubicacion: HTTP %d\n", code);
    return false;
}
