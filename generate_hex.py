"""
generate_hex.py  –  Post-build script para AgroPulse Firmware
Genera un archivo Intel HEX (.hex) a partir del ELF compilado por PlatformIO.
El .hex queda en:  .pio/build/esp32doit-devkit-v1/firmware.hex

Estrategia dual:
  1. Intenta objcopy nativo (rápido, preciso).
  2. Si falla (p. ej. binario Linux en Windows), convierte el ELF
     directamente con Python puro — sin dependencias externas.
"""

Import("env")  # noqa: F821  (inyectado por PlatformIO)

import os
import struct
import subprocess


# ── Conversor ELF-32 LE → Intel HEX (Python puro) ───────────────────────────

def _cs(data: bytes) -> int:
    """Checksum Intel HEX: complemento a dos del byte de suma."""
    return (-sum(data)) & 0xFF


def _ela(upper16: int) -> str:
    """Extended Linear Address Record (:02 0000 04 XXXX CC)."""
    rec = bytes([0x02, 0x00, 0x00, 0x04, (upper16 >> 8) & 0xFF, upper16 & 0xFF])
    return f":{rec.hex().upper()}{_cs(rec):02X}"


def _data_rec(addr32: int, chunk: bytes) -> str:
    """Data Record (:LL AAAA 00 DD... CC)."""
    addr16 = addr32 & 0xFFFF
    rec    = bytes([len(chunk), (addr16 >> 8) & 0xFF, addr16 & 0xFF, 0x00]) + chunk
    return f":{rec.hex().upper()}{_cs(rec):02X}"


def elf32_to_ihex(elf_path: str) -> str:
    """
    Extrae todos los segmentos PT_LOAD de un ELF-32 LE y devuelve
    el contenido de un archivo Intel HEX como cadena.
    """
    with open(elf_path, 'rb') as f:
        if f.read(4) != b'\x7fELF':
            raise ValueError("No es un archivo ELF válido")
        ei_class = struct.unpack('B', f.read(1))[0]
        if ei_class != 1:
            raise ValueError("Solo se admite ELF de 32 bits")

        f.seek(28)
        e_phoff, _, _, _, e_phentsize, e_phnum = struct.unpack('<IIIHHH', f.read(18))

        segments = []
        for i in range(e_phnum):
            f.seek(e_phoff + i * e_phentsize)
            p_type, p_off, _, p_paddr, p_filesz = struct.unpack('<IIIII', f.read(20))
            if p_type == 1 and p_filesz > 0:   # PT_LOAD
                f.seek(p_off)
                segments.append((p_paddr, f.read(p_filesz)))

    segments.sort(key=lambda s: s[0])

    lines     = []
    last_high = -1
    for base, data in segments:
        off = 0
        while off < len(data):
            addr32 = base + off
            high   = addr32 >> 16
            if high != last_high:
                lines.append(_ela(high))
                last_high = high
            chunk = data[off:off + 16]
            lines.append(_data_rec(addr32, chunk))
            off += 16

    lines.append(":00000001FF")
    return '\n'.join(lines) + '\n'


# ── Post-build action ─────────────────────────────────────────────────────────

def generate_hex(source, target, env):          # noqa: E302
    build_dir = env.subst("$BUILD_DIR")
    prog_name = env.subst("${PROGNAME}")
    objcopy   = env.subst("$OBJCOPY")
    elf_path  = os.path.join(build_dir, prog_name + ".elf")
    hex_path  = os.path.join(build_dir, prog_name + ".hex")

    if not os.path.isfile(elf_path):
        print(f"\n⚠  [HEX] ELF no encontrado: {elf_path}\n")
        return

    # ── Intento 1: objcopy nativo ──────────────────────────────────────────
    if os.path.isfile(objcopy):
        try:
            result = subprocess.run(
                [objcopy, "-O", "ihex", elf_path, hex_path],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
            )
            if result.returncode == 0:
                size_kb = os.path.getsize(hex_path) / 1024
                print(f"\n✔  [HEX] Generado con objcopy: {hex_path}  ({size_kb:.1f} KB)\n")
                return
            print(f"\n⚠  [HEX] objcopy error (código {result.returncode}): "
                  f"{result.stderr.strip()}")
        except OSError as exc:
            # Binario no ejecutable en este SO (p.ej. ELF Linux en Windows)
            print(f"\n⚠  [HEX] objcopy no ejecutable ({exc}); usando conversor Python…")

    # ── Intento 2: conversor Python puro (sin dependencias) ───────────────
    try:
        ihex = elf32_to_ihex(elf_path)
        with open(hex_path, 'w') as f:
            f.write(ihex)
        size_kb = os.path.getsize(hex_path) / 1024
        print(f"\n✔  [HEX] Generado con Python (fallback): {hex_path}  ({size_kb:.1f} KB)\n")
    except Exception as exc:
        print(f"\n✘  [HEX] Error al generar .hex: {exc}\n")


# Ejecutar después de que el ELF esté listo
env.AddPostAction("$BUILD_DIR/${PROGNAME}.elf", generate_hex)
