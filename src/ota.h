#ifndef AGROPULSE_OTA_H
#define AGROPULSE_OTA_H

/*
 * ota.h — Actualización de firmware OTA con firma ECDSA P-256
 *
 * Seguridad implementada:
 *   1. ECDSA P-256 — verifica firma del binario antes de flashear.
 *      Clave privada vive solo en la máquina del desarrollador.
 *   2. Anti-rollback — rechaza versiones iguales o inferiores a la actual.
 *   3. SHA-256 acumulado en streaming — sin cargar el .bin completo en RAM.
 *   4. Device code — el ESP32 acredita su identidad en cada petición.
 *   5. Aborto seguro — si la firma falla, la partición OTA nunca se marca
 *      como arrancable (Update.end() no se llama).
 *
 * Uso: llamar otaCheck() en setup() después de commConnectWifi().
 *      Si hay actualización disponible y verificada, la función no retorna:
 *      hace ESP.restart() automáticamente.
 */

// Retorna false si no hay actualización o si la verificación falla.
// Si hay actualización válida: flashea y reinicia (no retorna).
bool otaCheck();

#endif // AGROPULSE_OTA_H
