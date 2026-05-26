#include "ota.h"
#include "comm.h"
#include "display.h"
#include "../include/config.h"

#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Update.h>
#include <mbedtls/pk.h>
#include <mbedtls/sha256.h>
#include <mbedtls/error.h>

// ── Clave pública ECDSA P-256 ─────────────────────────────────────────────────
// Generada con: python scripts/generar_claves.py
// REEMPLAZAR con tu clave real después de ejecutar generar_claves.py.
// La clave privada NUNCA va aquí — solo la pública.
static const char OTA_PUBLIC_KEY_PEM[] = R"(
-----BEGIN PUBLIC KEY-----
MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEedhTH63j3CiD9iUkQ5UQmPtxZlYA
Re8jWh5kVFEp28u+bzUymEW1mwzXRL3SD+3vtm91roySR5Nu8U5Q7EPsmA==
-----END PUBLIC KEY-----
)";

// ── Cliente HTTPS exclusivo para OTA ──────────────────────────────────────────
// Separado del cliente de comm.cpp para no interferir con el mutex de la tarea HTTP.
static WiFiClientSecure s_otaClient;

// ── Convierte "3.1.0" → 30100 ────────────────────────────────────────────────
static int parseVersionInt(const String& ver) {
    int major = 0, minor = 0, patch = 0;
    sscanf(ver.c_str(), "%d.%d.%d", &major, &minor, &patch);
    return major * 10000 + minor * 100 + patch;
}

// ── Verifica firma ECDSA P-256 sobre el hash SHA-256 ya calculado ─────────────
static bool verifyEcdsa(const uint8_t hash[32], const uint8_t* sigDer, size_t sigLen) {
    mbedtls_pk_context pk;
    mbedtls_pk_init(&pk);

    int ret = mbedtls_pk_parse_public_key(
        &pk,
        (const unsigned char*)OTA_PUBLIC_KEY_PEM,
        strlen(OTA_PUBLIC_KEY_PEM) + 1
    );
    if (ret != 0) {
        char err[80];
        mbedtls_strerror(ret, err, sizeof(err));
        Serial.printf("[OTA] Error leyendo clave publica: %s\n", err);
        mbedtls_pk_free(&pk);
        return false;
    }

    ret = mbedtls_pk_verify(&pk, MBEDTLS_MD_SHA256, hash, 32, sigDer, sigLen);
    mbedtls_pk_free(&pk);

    if (ret != 0) {
        Serial.println("[OTA] *** FIRMA INVALIDA — firmware rechazado ***");
        return false;
    }
    Serial.println("[OTA] Firma ECDSA verificada correctamente.");
    return true;
}

// ── otaCheck() ────────────────────────────────────────────────────────────────
bool otaCheck() {
    String url        = commGetUrl();
    String deviceCode = commGetCodigo();

    if (url.isEmpty() || deviceCode.isEmpty()) {
        Serial.println("[OTA] Sin URL o codigo de dispositivo — omitiendo OTA.");
        return false;
    }

    // El cliente no verifica el certificado TLS del servidor.
    // La seguridad real la aporta la firma ECDSA: aunque un atacante intercepte
    // el tráfico, no puede forjar un binario con firma válida sin la clave privada.
    s_otaClient.setInsecure();

    // ── 1. Verificar versión disponible ───────────────────────────────────────
    HTTPClient httpVer;
    httpVer.begin(s_otaClient, url + "/firmware/version");
    httpVer.addHeader("X-Device-Code", deviceCode);
    httpVer.setTimeout(OTA_HTTP_TIMEOUT_MS);

    int httpCode = httpVer.GET();
    if (httpCode != 200) {
        Serial.printf("[OTA] Version check fallido: HTTP %d\n", httpCode);
        httpVer.end();
        return false;
    }

    JsonDocument doc;
    deserializeJson(doc, httpVer.getString());
    httpVer.end();

    String newVersion    = doc["version"]      | String("none");
    int    newVersionInt = doc["versionInt"]    | 0;
    int    minVersionInt = doc["minVersionInt"] | 0;

    if (newVersion == "none" || newVersionInt == 0) {
        Serial.println("[OTA] No hay firmware disponible en el servidor.");
        return false;
    }

    int currentVersionInt = parseVersionInt(FIRMWARE_VERSION);

    // Anti-rollback: rechaza versiones iguales o inferiores
    if (newVersionInt <= currentVersionInt) {
        Serial.printf("[OTA] Firmware al dia (actual=%d, disponible=%d).\n",
            currentVersionInt, newVersionInt);
        return false;
    }
    // El servidor puede imponer un mínimo aceptable
    if (newVersionInt < minVersionInt) {
        Serial.printf("[OTA] Version disponible (%d) menor que el minimo del servidor (%d).\n",
            newVersionInt, minVersionInt);
        return false;
    }

    Serial.printf("[OTA] Nueva version: %s → %s\n", FIRMWARE_VERSION, newVersion.c_str());
    displayMensaje("OTA Update", ("Nueva: v" + newVersion).c_str(),
                   "Descargando...", "No apagues el ESP32");

    // ── 2. Descargar firmware y calcular SHA-256 en streaming ────────────────
    HTTPClient httpDl;
    httpDl.begin(s_otaClient, url + "/firmware/download");
    httpDl.addHeader("X-Device-Code", deviceCode);
    httpDl.setTimeout(OTA_DOWNLOAD_TIMEOUT_MS);

    int dlCode = httpDl.GET();
    if (dlCode != 200) {
        Serial.printf("[OTA] Descarga fallida: HTTP %d\n", dlCode);
        httpDl.end();
        displayMensaje("OTA Error", "Fallo descarga", "Sistema normal", "");
        delay(2000);
        return false;
    }

    int fwSize = httpDl.getSize();
    if (fwSize <= 0) {
        Serial.println("[OTA] Tamano de firmware desconocido — abortando.");
        httpDl.end();
        return false;
    }

    if (!Update.begin(fwSize, U_FLASH)) {
        Serial.printf("[OTA] Update.begin fallo: %s\n", Update.errorString());
        httpDl.end();
        return false;
    }

    // Inicializar contexto SHA-256 incremental
    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    mbedtls_sha256_starts_ret(&sha, 0); // 0 = SHA-256 (no SHA-224)

    WiFiClient* stream = httpDl.getStreamPtr();
    uint8_t     buf[512];
    int         written = 0;

    while (written < fwSize && httpDl.connected()) {
        int available = stream->available();
        if (available > 0) {
            int toRead = min(available, (int)sizeof(buf));
            int len    = stream->readBytes(buf, toRead);
            if (len > 0) {
                Update.write(buf, len);
                mbedtls_sha256_update_ret(&sha, buf, len);
                written += len;
            }
        } else {
            delay(1);
        }
    }
    httpDl.end();

    if (written != fwSize) {
        Serial.printf("[OTA] Descarga incompleta: %d/%d bytes.\n", written, fwSize);
        Update.abort();
        mbedtls_sha256_free(&sha);
        return false;
    }

    uint8_t hash[32];
    mbedtls_sha256_finish_ret(&sha, hash);
    mbedtls_sha256_free(&sha);

    // ── 3. Descargar firma ECDSA ──────────────────────────────────────────────
    HTTPClient httpSig;
    httpSig.begin(s_otaClient, url + "/firmware/signature");
    httpSig.addHeader("X-Device-Code", deviceCode);
    httpSig.setTimeout(OTA_HTTP_TIMEOUT_MS);

    int sigCode = httpSig.GET();
    if (sigCode != 200) {
        Serial.printf("[OTA] Descarga de firma fallida: HTTP %d\n", sigCode);
        httpSig.end();
        Update.abort();
        return false;
    }

    // Firma ECDSA P-256 DER: máximo 72 bytes
    uint8_t sigBuf[72];
    int     sigLen = httpSig.getStreamPtr()->readBytes(sigBuf, sizeof(sigBuf));
    httpSig.end();

    if (sigLen <= 0 || sigLen > 72) {
        Serial.printf("[OTA] Longitud de firma invalida: %d bytes.\n", sigLen);
        Update.abort();
        return false;
    }

    // ── 4. Verificar firma — si falla, NO se llama Update.end() ──────────────
    if (!verifyEcdsa(hash, sigBuf, sigLen)) {
        displayMensaje("OTA RECHAZADO", "Firma invalida!", "Posible ataque", "Sistema normal");
        delay(3000);
        Update.abort(); // Resetea estado interno; partición nunca marcada como arrancable
        return false;
    }

    // ── 5. Confirmar y reiniciar ──────────────────────────────────────────────
    if (!Update.end(false)) {
        Serial.printf("[OTA] Update.end fallo: %s\n", Update.errorString());
        return false;
    }

    Serial.println("[OTA] Actualizacion verificada y confirmada. Reiniciando...");
    displayMensaje("OTA Exitoso!", ("v" + newVersion).c_str(), "Reiniciando...", "Espera...");
    delay(2000);
    ESP.restart();
    return true; // No se alcanza nunca
}
