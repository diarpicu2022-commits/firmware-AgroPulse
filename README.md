# AgroPulse Firmware v2.0 — ESP32

Firmware para el microcontrolador ESP32-WROOM-32D que actúa como nodo de campo del sistema AgroPulse.

---

## Estructura

```
firmware/
├── platformio.ini         ← Configuración PlatformIO (dependencias, board)
├── include/
│   └── config.h           ← Todos los pines, umbrales y parámetros de red
└── src/
    ├── main.cpp           ← Punto de entrada (setup + loop)
    ├── sensors.h / .cpp   ← Lectura de DHT11, DHT22, sensor de suelo
    ├── actuators.h / .cpp ← Control de relay (bomba, ventilador, calefactor, puerta)
    ├── comm.h / .cpp      ← WiFi-HTTP al backend + fallback Serial
    └── display.h / .cpp   ← Pantalla OLED SSD1306
```

---

## Hardware conectado

| Componente | Pin | Descripción |
|-----------|-----|-------------|
| DHT11 (exterior) | GPIO 4 | Temperatura y humedad exterior |
| DTH22 (interior) | GPIO 34 (ADC) | Temperatura interior |
| Sensor suelo | GPIO 35 (ADC) | Humedad del suelo |
| Relay bomba | GPIO 12 | Motobomba de riego |
| Relay ventilador | GPIO 13 | Ventilador/Extractor |
| Relay calefactor | GPIO 14 | Calefactor |
| Relay puerta | GPIO 15 | Puerta del invernadero |
| OLED SSD1306 | SDA=21, SCL=22 | Pantalla de estado (I2C) |
| LED estado | GPIO 2 | LED integrado del ESP32 |
| LED WiFi | GPIO 25 | Indicador conexión WiFi |
| LED sensor | GPIO 26 | Parpadea al leer sensores |

---

## Inicio rápido

1. Editar `include/config.h` — cambiar `WIFI_SSID`, `WIFI_PASSWORD` y `BACKEND_HOST`
2. Conectar el ESP32 por USB
3. Compilar y flashear:

```bash
cd firmware
pio run --target upload
```

4. Monitorear:

```bash
pio device monitor --baud 115200
```

---

## Comunicación con el backend

El firmware envía las lecturas en formato JSON:

```
POST http://<BACKEND_HOST>:8080/api/readings
Content-Type: application/json

{"device_id":"ESP32-AP-001","temp_interior":24.1,"temp_exterior":18.5,"humidity":68.0,"soil_moisture":52.7}
```

Si WiFi no está disponible, los datos se envían por Serial USB (fallback automático).

---

## Comandos de control (enviados desde el backend o monitor Serial)

| Comando | Acción |
|---------|--------|
| `PUMP:ON` / `PUMP:OFF` | Control manual de la bomba |
| `FAN:ON` / `FAN:OFF` | Control manual del ventilador |
| `HEAT:ON` / `HEAT:OFF` | Control del calefactor |
| `DOOR:ON` / `DOOR:OFF` | Apertura/cierre de puerta |
| `AUTO:ON` / `AUTO:OFF` | Activar/desactivar control automático |
| `PUMP_ON:30` | Cambiar umbral ON de la bomba (en %) |
| `FAN_ON:28` | Cambiar umbral ON del ventilador (en °C) |
| `STATUS` | Ver estado actual |
| `RESET` | Apagar todos los actuadores |

---

## Arquitectura — Principios aplicados

**Separación de responsabilidades (SRP):** cada archivo `.cpp` tiene una única tarea.  
**Abstracción:** `main.cpp` solo orquesta — no conoce los detalles de hardware.  
**Fallback:** si WiFi falla, la comunicación cae automáticamente a Serial USB.  
**Configuración centralizada:** todos los pines y umbrales en `config.h`.

---

*AgroPulse — Universidad Cooperativa de Colombia, Pasto, Nariño*
