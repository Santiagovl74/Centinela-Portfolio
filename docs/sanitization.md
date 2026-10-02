# Nota de sanitización

Este documento resume, con fines de transparencia, qué tipo de información se
cambió respecto al proyecto original (privado) para poder publicar esta
versión de portafolio. No es un changelog funcional: la lógica y arquitectura
del sistema son las mismas; solo se reemplazó información identificable.

## Reemplazado

- Nombre de la organización real → "Una IPS", en todos los archivos del
  proyecto (firmware, backend, dashboard, portal de login y README).
- Ubicación (sede/ciudad) y nombre legal de la empresa — eliminados.
- Logo real → ícono genérico (`logo.svg`).
- Direcciones de correo reales → direcciones de ejemplo `@example.com`.
- URLs de despliegue y direcciones físicas de hardware real → placeholders
  de ejemplo.
- Usuario/salt/hash del portal de login — ya venían como placeholder en el
  proyecto original; se mantienen así.
- Historial de git — el repositorio público parte de un único commit
  inicial, sin el historial del repositorio original, para no exponer datos
  de terceros ni información interna de versiones anteriores.

## Ya no era sensible en el original

- Las credenciales de firmware (WiFi, token de ingesta, clave OTA) ya
  estaban fuera de control de versiones en el proyecto original
  (archivo excluido por `.gitignore`, con una plantilla `.example` como
  referencia) — no fue necesario tocarlas.
- La IP interna del equipo en la configuración del portal ya estaba como
  placeholder.
