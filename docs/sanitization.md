# Nota de sanitización

Este documento resume, con fines de transparencia, qué se cambió respecto al
proyecto original (privado) para poder publicar esta versión de portafolio.
No es un changelog funcional: la lógica y arquitectura del sistema son las
mismas; solo se reemplazó información identificable o sensible.

## Reemplazado

- **Nombre de la organización real** (institución de salud donde se hizo la
  práctica profesional) → "Una IPS", en firmware, backend, dashboard, portal
  de login y README.
- **Nombre legal de la empresa** — eliminado del footer del dashboard.
- **Ubicación** (sede/ciudad) — eliminada de comentarios y textos visibles.
- **Logo real** (imagen embebida en base64 y archivo `logo.webp`) → ícono
  genérico (`logo.svg`).
- **Lista de destinatarios de alertas por correo** (5 direcciones reales,
  una de ellas asociada al nombre de una persona) → direcciones de ejemplo
  `@example.com`.
- **URL real y activa de despliegue de Google Apps Script**, embebida en el
  botón "Historial Cloud" del dashboard → placeholder
  `TU_ID_DE_DESPLIEGUE`.
- **Direcciones físicas de los sensores DS18B20** (identificadores únicos de
  hardware instalado) → valores de ejemplo.
- **Usuario/salt/hash del portal de login** — ya venían como placeholder en
  el proyecto original (`CAMBIAR_USUARIO`, etc.); se mantienen así.
- **Autoría de terceros en el historial de git** — el repositorio público
  parte de un único commit inicial, sin el historial del repositorio
  original, para no exponer nombres ni correos de colaboradores.

## Ya no era sensible en el original

- El token de ingesta (`INGEST_TOKEN`) y las credenciales WiFi/OTA del
  firmware ya estaban fuera de control de versiones en el proyecto original
  (`secrets.h` excluido por `.gitignore`, con `secrets.h.example` como
  plantilla) — no fue necesario tocarlos.
- La IP interna del ESP32 en `web.config` ya estaba como placeholder
  (`IP_DEL_ESP32`).

## Hallazgo de seguridad fuera del alcance de esta sanitización

Durante la auditoría se detectó que el **historial de git** del repositorio
original contenía, en un commit temprano, un token de ingesta real que
posteriormente fue reemplazado por un placeholder en el archivo — pero no en
el despliegue activo del backend, según el propio mensaje de ese commit. Esto
es un riesgo operacional del sistema en producción, independiente de esta
sanitización, y se comunicó directamente para que el token y la URL de
despliegue del Apps Script se roten cuanto antes.
