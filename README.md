# CENTINELA (versión portafolio)

> **Aviso.** Este repositorio es una versión **sanitizada, de portafolio/demo**, de un
> sistema desarrollado originalmente en un entorno organizacional real (práctica
> profesional en una IPS de salud). Todos los nombres de organización, usuarios,
> credenciales, direcciones de infraestructura, correos y logos han sido
> reemplazados por datos ficticios o genéricos ("Una IPS"). El código refleja
> fielmente el diseño y la lógica reales del sistema; solo se sanitizó la
> identidad de la organización y los secretos.
>
> Ningún archivo de este repositorio contiene credenciales reales: los tokens,
> URLs de despliegue, direcciones de sensores y destinatarios de alerta son
> placeholders o valores de ejemplo.

## Descripción

Sistema IoT de monitoreo y registro automatizado de variables ambientales para
la gestión de cadena de frío en el área de vacunación de una IPS (institución
de salud). Sustituye el registro manual en papel (dos tomas de temperatura al
día, que dejan más del 99 % del tiempo de almacenamiento sin evidencia
documentada) por vigilancia continua: verificación de rangos cada 3 segundos,
registro cada 10 minutos, alarmas locales y notificaciones remotas ante
desviaciones.

## Problema

Las vacunas deben conservarse dentro de un rango estrecho de temperatura
(2–8 °C). El control manual en papel es esporádico, propenso a errores de
transcripción y no deja evidencia de lo que ocurre fuera del horario laboral
(madrugadas, fines de semana). Una falla silenciosa de refrigeración puede
inutilizar un lote completo de biológicos sin que nadie se entere a tiempo.

## Solución

Un equipo embebido (ESP32) mide continuamente la temperatura de dos neveras y
la temperatura/humedad ambiente del cuarto de vacunas, evalúa los valores
contra rangos normativos con una máquina de estados (confirmación por
lecturas consecutivas + histéresis, para evitar falsas alarmas), dispara una
alarma sonora local ante desviaciones sostenidas o fallo de sensor, expone un
panel web en tiempo real, y respalda cada lectura en Google Sheets vía Google
Apps Script — con reintentos, control de duplicados (idempotencia) y alertas
por correo ante silencio prolongado del equipo o valores fuera de rango.

## Características principales

- **Sensores:** 2x DS18B20 (temperatura, unidades de refrigeración) + DHT11
  (temperatura/humedad ambiente del cuarto de vacunación).
- **Pantalla local:** OLED SH1106 128x64 (I2C) para consulta en sitio.
- **Alarma sonora:** cadencia 2s ON / 2s OFF, con confirmación de 3 lecturas
  seguidas fuera de rango (evita falsas alarmas por apertura de puerta) y
  patrón distinto para fallo de sensor.
- **Servidor web embebido:** panel en tiempo real vía `http://centinela.local/`
  (ESPAsyncWebServer + mDNS), con historial de las últimas 24 horas.
- **Actualización OTA:** por WiFi (ArduinoOTA), sin necesidad de cable USB.
- **Registro remoto:** cada lectura se envía firmada con token a Google
  Sheets mediante Google Apps Script, con reintentos ante fallos de
  transmisión y control de duplicados.
- **Reportes:** estadísticas descriptivas (máximos, mínimos, promedios, % de
  cumplimiento normativo) con opción de exportación (PNG/PDF).
- **Alertas por correo:** ante desviaciones persistentes, interrupción del
  reporte del equipo (watchdog de silencio) o retorno a condiciones normales.
- **Configuración sin reflashear:** credenciales WiFi, URL de destino, token e
  intervalo de registro viven en memoria NVS del ESP32 y se administran por
  consola Serial.
- **Portal de acceso remoto:** panel protegido por un login con hash
  SHA-256 + salt, bloqueo por fuerza bruta y proxy inverso, para consultar el
  panel desde fuera de la red local sin exponer el ESP32 directamente.

## Arquitectura

```
┌────────────┐   HTTP (token)   ┌──────────────────┐        ┌───────────────┐
│   ESP32    │ ───────────────► │ Google Apps       │──────► │ Google Sheets │
│  Centinela │                  │ Script (backend)  │        │  (histórico)  │
│            │ ◄─── panel ───── │                    │        └───────────────┘
└─────┬──────┘   HTTP (LAN)     └──────────────────┘
      │ sirve /api/data, /
      │
┌─────▼──────┐   HTTPS + login   ┌──────────────────┐
│  Navegador │ ◄───────────────► │ Portal IIS/ASP.NET│  (proxy inverso al ESP32,
│  (remoto)  │                   │  (login.aspx)      │   protege el acceso externo)
└────────────┘                   └──────────────────┘
```

- **Firmware (`Centinela_v4.ino` + `webpage.h`):** lectura de sensores,
  máquina de estados de alarmas, servidor web embebido (dashboard en
  `webpage.h`), envío en segundo plano (cola FreeRTOS) a Google Apps Script,
  consola Serial de aprovisionamiento, actualización OTA.
- **Backend (`Code_v4.gs`):** recibe las lecturas por `doGet`, valida el
  token de ingesta, escribe en Google Sheets con formato y colores
  condicionales, calcula estadísticas e historial multihoja (archivado
  automático al superar un número de filas), y ejecuta un watchdog periódico
  que envía alertas por correo.
- **Portal de acceso (`portal-login/`):** sitio ASP.NET/IIS que actúa como
  proxy inverso hacia el ESP32 y exige sesión (Forms Authentication) antes de
  reenviar cualquier petición, para que el equipo nunca quede expuesto
  directamente a internet.

## Stack tecnológico

| Capa | Tecnología |
|---|---|
| Firmware | C++ (Arduino framework), ESP32 |
| Comunicación | HTTP, WiFi, OneWire, I2C |
| Backend / almacenamiento | Google Apps Script + Google Sheets |
| Portal de acceso | ASP.NET (C#) sobre IIS, URL Rewrite + Application Request Routing |
| Frontend del panel | HTML/CSS/JS embebido (Chart.js, jsPDF) |

## Seguridad

- **Sin secretos en el binario:** las credenciales WiFi, la URL del backend,
  el token de ingesta y la clave OTA se siembran una vez desde `secrets.h`
  (excluido de git) y luego viven en memoria NVS del ESP32; se administran
  por consola Serial, sin necesidad de recompilar.
- **Token de ingesta:** cada envío del ESP32 al backend incluye un token
  compartido; el backend puede operar en modo `observe` (registra sin
  bloquear, para verificar el despliegue) o `enforce` (rechaza peticiones sin
  token válido).
- **Acceso remoto protegido:** el panel del ESP32 no tiene autenticación
  propia — se publica siempre detrás de un portal de login (hash
  SHA-256 + salt, nunca contraseña en texto plano) con bloqueo tras 5
  intentos fallidos por IP durante 15 minutos, cookies de sesión solo por
  HTTPS, y proxy inverso que evita exponer el equipo directamente.
- **Sin este repositorio:** cualquier valor específico de un despliegue real
  (token, URL de Apps Script, usuario/hash del portal, IP interna del ESP32)
  vive fuera del control de versiones, como placeholder o archivo `.example`.

## Instalación

### Firmware (ESP32)

1. Instala en el Arduino IDE las librerías: `OneWire`, `DallasTemperature`,
   `DHT` (Adafruit), `AsyncTCP`, `ESPAsyncWebServer`, `Adafruit SH110X`,
   `Adafruit GFX`. (`Preferences`, `ArduinoOTA`, `ESPmDNS` y `Update` vienen
   con el core de ESP32.)
2. Copia `secrets.h.example` como `secrets.h` en la misma carpeta y completa
   tus propios valores (ver más abajo).
3. Selecciona el esquema de partición **"Minimal SPIFFS (1.9MB APP with OTA)"**
   en Arduino IDE → Tools → Partition Scheme (imprescindible: con el esquema
   por defecto el sketch no deja margen para OTA).
4. Compila y sube `Centinela_v4.ino` una primera vez por USB.
5. Tras el primer arranque, la configuración vive en NVS y se ajusta por
   consola Serial (115200 baudios, terminación de línea "NL"):

   | Comando | Qué hace |
   |---|---|
   | `SHOW` | Muestra la configuración actual (token/clave ocultos) |
   | `SET SSID <nombre red>` | Guarda el SSID WiFi |
   | `SET PASS <clave>` | Guarda la contraseña de esa red WiFi |
   | `SET URL <url GAS /exec>` | Guarda la URL de destino en Google Apps Script |
   | `SET TOKEN <token>` | Guarda el token de autenticación usado al enviar datos |
   | `SET OTAPASS <clave>` | Guarda la clave requerida para actualizar por OTA |
   | `SET INTERVAL <min>` | Cambia el intervalo de registro (1–60 min) |
   | `REBOOT` / `WIPE` | Reinicia / borra la configuración guardada |

### Backend (Google Apps Script)

1. Crea una hoja de cálculo de Google Sheets nueva y abre
   Extensiones → Apps Script.
2. Copia el contenido de `Code_v4.gs`.
3. Reemplaza `INGEST_TOKEN` por el mismo token que configuraste en el ESP32.
4. Despliega como aplicación web (ejecutar como tú, acceso "cualquiera con el
   enlace") y copia la URL `/exec` resultante en `SET URL` del ESP32 y en el
   botón "Historial Cloud" de `webpage.h`.
5. (Opcional) Ejecuta manualmente `probarCorreoAlerta()` y
   `crearActivadorAlertas()` para habilitar las alertas por correo.

### Portal de acceso (opcional, para exponerlo fuera de la red local)

1. En IIS, crea un sitio con certificado HTTPS y los módulos **URL Rewrite**
   y **Application Request Routing** (proxy habilitado).
2. Copia `login.aspx`, `logout.aspx`, `logo.svg` y `web.config.example`
   (renombrado a `web.config`).
3. En `web.config`, reemplaza `IP_DEL_ESP32` por la IP interna real del
   equipo.
4. En `login.aspx`, reemplaza `CAMBIAR_USUARIO`, `CAMBIAR_SALT_BASE64` y
   `CAMBIAR_HASH_SHA256_HEX` (instrucciones de generación en el propio
   archivo).

## Variables de entorno / configuración

Ningún valor real se versiona. Plantilla disponible en
[`secrets.h.example`](secrets.h.example):

```c
#define DEF_WIFI_SSID      "NOMBRE_DE_TU_RED"
#define DEF_WIFI_PASSWORD  "CONTRASENA_DE_TU_RED"
#define DEF_GAS_URL        "https://script.google.com/macros/s/TU_ID_DE_DESPLIEGUE/exec"
#define DEF_INGEST_TOKEN   "GENERA_UN_TOKEN_ALEATORIO_20_CHARS"
#define DEF_OTA_PASSWORD   "DEFINE_UNA_CLAVE_OTA"
#define DEF_INTERVAL_MIN   10
```

## Datos de demostración

- Direcciones de sensores DS18B20 en `Centinela_v4.ino`: **valores de
  ejemplo** (`0x28, 0x00, ..., 0x01` / `...0x02`) — se descubren las reales
  con `printDS18Addresses()` por consola Serial.
- Destinatarios de alerta en `Code_v4.gs` (`ALERT_EMAILS`): direcciones
  `@example.com` de demostración.
- Nombre de organización: **"Una IPS"** en todo el repositorio (footer,
  título del panel, pantalla OLED, correos de alerta).
- Logo: ícono genérico (`logo.svg`), no el logo real de ninguna organización.

## Estructura del proyecto

```
.
├── README.md
├── LICENSE
├── .gitignore
├── secrets.h.example        # plantilla de credenciales (sin valores reales)
├── logo.svg                 # logo genérico de demostración
├── Centinela_v4.ino          # firmware (Arduino IDE, ESP32)
├── webpage.h                 # dashboard embebido servido por el ESP32
├── Code_v4.gs                 # backend (Google Apps Script)
├── portal-login/              # portal de acceso remoto (ASP.NET/IIS)
│   ├── login.aspx
│   ├── logout.aspx
│   └── web.config.example
└── docs/
    ├── security.md            # detalle de medidas de seguridad y modelo de amenazas
    └── sanitization.md        # qué se sanitizó y por qué (transparencia del portafolio)
```

## Estado del proyecto

Portafolio derivado de un sistema en operación real (opción de grado /
práctica profesional en ingeniería electrónica). Esta versión pública es
únicamente para fines de demostración técnica; no está conectada a ningún
sistema en producción. En el proyecto original está en desarrollo un sistema
de respaldo energético autónomo (prototipo validado, pendiente de rediseño
de tarjeta).

## Consideraciones

- Este repositorio **no** debe usarse tal cual en un entorno clínico real sin
  una revisión de seguridad propia (rotación de todos los tokens, hash de
  contraseña generado de nuevo, revisión de la política de TLS del portal).
- Las capturas de pantalla no se incluyen para no exponer paneles de un
  sistema en producción; el diagrama de arquitectura arriba resume el flujo
  de datos.

## Autoría

Desarrollado por Santiago Vargas como práctica profesional (opción de grado)
en Ingeniería Electrónica — Universidad del Magdalena.

## Licencia

Este proyecto se publica bajo la licencia MIT (ver [`LICENSE`](LICENSE)).
