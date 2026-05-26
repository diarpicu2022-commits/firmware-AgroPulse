#!/usr/bin/env python3
"""
AgroPulse — Generador de par de claves ECDSA P-256 para firma de firmware OTA.

Ejecutar UNA SOLA VEZ. Guarda private_key.pem en lugar seguro (offline/USB).
Nunca subas private_key.pem a GitHub ni al servidor.

Requiere: pip install cryptography

Salida:
  private_key.pem        — clave privada (GUARDAR OFFLINE, nunca en Git)
  public_key.pem         — clave pública en formato PEM
  public_key_base64.txt  — clave pública DER en Base64 (para FIRMWARE_PUBLIC_KEY en Render)
"""

from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.hazmat.primitives import serialization
import base64, sys

def main():
    print("Generando par de claves ECDSA P-256...")
    private_key = ec.generate_private_key(ec.SECP256R1())
    public_key  = private_key.public_key()

    # Serializar clave privada
    priv_pem = private_key.private_bytes(
        encoding=serialization.Encoding.PEM,
        format=serialization.PrivateFormat.TraditionalOpenSSL,
        encryption_algorithm=serialization.NoEncryption()
    )

    # Serializar clave pública en PEM
    pub_pem = public_key.public_bytes(
        encoding=serialization.Encoding.PEM,
        format=serialization.PublicFormat.SubjectPublicKeyInfo
    )

    # Serializar clave pública en DER (para Base64 → variable de entorno)
    pub_der = public_key.public_bytes(
        encoding=serialization.Encoding.DER,
        format=serialization.PublicFormat.SubjectPublicKeyInfo
    )
    pub_b64 = base64.b64encode(pub_der).decode()

    # Guardar archivos
    with open("private_key.pem", "wb") as f: f.write(priv_pem)
    with open("public_key.pem",  "wb") as f: f.write(pub_pem)
    with open("public_key_base64.txt", "w") as f: f.write(pub_b64 + "\n")

    print("\n✓ Archivos generados:")
    print("  private_key.pem        — GUARDAR OFFLINE. Nunca subir a Git.")
    print("  public_key.pem         — Copiar contenido a OTA_PUBLIC_KEY_PEM en ota.cpp")
    print("  public_key_base64.txt  — Pegar valor en FIRMWARE_PUBLIC_KEY en Render\n")
    print("─────────────────────────────────────────────────────────")
    print("Clave pública PEM (para ota.cpp):")
    print(pub_pem.decode())
    print("─────────────────────────────────────────────────────────")
    print("Clave pública Base64 DER (para variable FIRMWARE_PUBLIC_KEY en Render):")
    print(pub_b64)

if __name__ == "__main__":
    main()
