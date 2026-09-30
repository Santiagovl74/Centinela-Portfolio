# Seguridad

Resumen de las medidas de seguridad implementadas en CENTINELA, sin exponer
ninguna configuración real de un despliegue en producción.

## Modelo de amenazas (resumen)

| Riesgo | Mitigación |
|---|---|
| Interceptar/reutilizar credenciales | No hay secretos en el binario compilado: viven en NVS, sembrados desde un archivo local excluido de git. |
| Escritura no autorizada en la hoja de cálculo | Token compartido validado en el backend (`INGEST_TOKEN`), con modo `observe`/`enforce` para desplegar sin cortar el servicio. |
| Exposición del panel del ESP32 a internet | El equipo nunca se publica directamente: siempre detrás de un portal de login con proxy inverso. |
| Fuerza bruta contra el login | Bloqueo de 15 minutos tras 5 intentos fallidos por IP (caché del servidor). |
| Robo de la contraseña del portal | Se almacena `SHA-256(salt + contraseña)`, nunca texto plano; el salt es aleatorio por despliegue. |
| Secuestro de sesión | Cookie de sesión únicamente sobre HTTPS (`requireSSL="true"`), expiración de 120 min con renovación. |
| Actualización de firmware maliciosa (OTA) | La actualización OTA exige una clave (`OTAPASS`) independiente del token de ingesta. |
| Falsos positivos de alarma | Confirmación por lecturas consecutivas + histéresis al salir del rango, para no disparar alarmas por aperturas breves de puerta. |
| Equipo "silencioso" (fallo no detectado) | Watchdog en el backend: si no llegan registros en un tiempo configurable, se envía una alerta por correo. |

## Qué NO cubre esta versión

- No incluye rotación automática de secretos: cambiar el token de ingesta o
  la clave OTA sigue siendo una acción manual (`SET TOKEN`/`SET OTAPASS` por
  consola Serial + redesplegar el backend).
- El backend en Google Apps Script confía en la infraestructura de Google
  para TLS en tránsito; no hay cifrado adicional a nivel de aplicación de las
  lecturas enviadas por el ESP32 (solo el token de autenticación).
- El límite de intentos de login es por IP; una red con NAT compartido podría
  bloquear a varios usuarios legítimos a la vez.

## Recomendaciones para un despliegue real

1. Genera el token de ingesta y la clave OTA con un generador aleatorio
   (20+ caracteres), no valores memorizables.
2. Regenera `salt`/`hash` del portal de login por cada despliegue (el script
   PowerShell para hacerlo está documentado en `portal-login/login.aspx`).
3. Restringe la regla de proxy inverso (`ReverseProxyToCentinela` en
   `web.config`) a la IP interna real del equipo, y mantenla fuera de
   control de versiones.
4. Considera mover el token de ingesta de la query string a una cabecera
   HTTP si tu backend lo permite, para reducir su aparición en logs de
   acceso.
