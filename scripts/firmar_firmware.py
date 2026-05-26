#!/usr/bin/env python3
"""
AgroPulse — Firma un firmware.bin con ECDSA P-256 (SHA-256).

Uso:
  python firmar_firmware.py <firmware.bin> <private_key.pem>

Salida:
  <firmware>.sig  — firma DER binaria (max 72 bytes)

Ejecutar después de cada build de PlatformIO antes de subir al backend.
El .bin y el .sig se suben juntos al endpoint POST /api/firmware/upload.

Requiere: pip install cryptography
"""

import sys
from pathlib import Path
from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.hazmat.primitives.serialization import load_pem_private_key
import hashlib

def main():
    if len(sys.argv) != 3:
        print("Uso: python firmar_firmware.py <firmware.bin> <private_key.pem>")
        sys.exit(1)

    bin_path = Path(sys.argv[1])
    key_path = Path(sys.argv[2])

    if not bin_path.exists():
        print(f"Error: {bin_path} no existe.")
        sys.exit(1)
    if not key_path.exists():
        print(f"Error: {key_path} no existe.")
        sys.exit(1)

    bin_data = bin_path.read_bytes()
    key_pem  = key_path.read_bytes()

    # Cargar clave privada
    private_key = load_pem_private_key(key_pem, password=None)
    if not isinstance(private_key, ec.EllipticCurvePrivateKey):
        print("Error: se esperaba clave ECDSA.")
        sys.exit(1)

    # Firmar SHA-256(firmware) con ECDSA → DER
    signature = private_key.sign(bin_data, ec.ECDSA(hashes.SHA256()))

    # Guardar firma
    sig_path = bin_path.with_suffix(".sig")
    sig_path.write_bytes(signature)

    # Info para el usuario
    sha256 = hashlib.sha256(bin_data).hexdigest()
    print(f"Firmware : {bin_path} ({len(bin_data):,} bytes)")
    print(f"SHA-256  : {sha256}")
    print(f"Firma    : {sig_path} ({len(signature)} bytes DER)")
    print("\n✓ Listo. Sube ambos archivos al backend:")
    print(f"  POST /api/firmware/upload")
    print(f"    bin     = {bin_path.name}")
    print(f"    sig     = {sig_path.name}")
    print(f"    version = <nueva version, ej: 3.1.0>")

if __name__ == "__main__":
    main()
