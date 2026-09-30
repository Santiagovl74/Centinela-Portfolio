// ============================================================
//  CENTINELA — Backend (Google Apps Script)
//  Una IPS — Cuarto de Vacunas
//
//  NOTA (versión portafolio): copia sanitizada. Nombres de
//  organización, destinatarios de alertas y tokens son ficticios
//  o placeholders. Ver README.md.
//
//  Parámetros GET que envía el ESP32:
//    ?tempN1=XX.X   → Nevera 1 · Sonda A (DS18B20 #1)
//    &tempN2=XX.X   → Nevera 1 · Sonda B (DS18B20 #2)
//    &tempExt=XX.X  → Temp. ambiente cuarto (DHT11)
//    &hum=XX.X      → Humedad relativa cuarto (DHT11)
//    &token, &fw, &up, &rssi, &heap, &rst, &fails   (telemetría)
//    &age, &try, &sid                (edad, reintentos e idempotencia)
//
//  ALMACENAMIENTO
//  Los datos se escriben en la pestaña "CENTINELA". Al superar
//  MAX_ROWS la pestaña se archiva renombrándose con la fecha
//  —"CENTINELA [2026-09-08]"— y se crea una nueva vacía.
//  Las consultas recorren AUTOMÁTICAMENTE la pestaña activa y todas
//  las archivadas, de modo que el histórico completo sigue visible.
// ============================================================

// ── Configuración central ────────────────────────────────────
// Nombre de la pestaña activa de datos. Las archivadas usan este
// mismo nombre seguido de la fecha entre corchetes.
var SHEET_NAME  = "CENTINELA";

// Nombre heredado de versiones anteriores. Se conserva SOLO para que
// la migración sea automática y para poder leer histórico antiguo si
// alguna pestaña quedara sin renombrar. No se escribe nunca en él.
var LEGACY_SHEET_NAME = "Datalogger v3";

var N1_MIN      = 2;                 // °C mínimo Nevera 1 · Sonda A
var N1_MAX      = 8;                 // °C máximo Nevera 1 · Sonda A
var N2_MIN      = 2;                 // °C mínimo Nevera 1 · Sonda B
var N2_MAX      = 8;                 // °C máximo Nevera 1 · Sonda B

// ── Verificación de instrumentación (campaña temporal) ──────
// Ambas sondas comparten nevera: |A - B| mide la consistencia de
// la instrumentacion. Tolerancia alineada con el firmware.
var DELTA_TOL   = 0.5;               // °C — muestra "dentro de tolerancia"
var MAX_ROWS    = 5000;              // limite de filas antes de archivar
                                     // (10 min = 144 filas/dia -> archiva cada ~35 dias)

// Colores del encabezado
var COLOR_HEADER_BG   = "#1a3a5c";  // azul oscuro institucional
var COLOR_HEADER_TEXT = "#ffffff";

// Colores de columnas por sección
var COLOR_N1   = "#dbeafe";  // azul claro  — Nevera 1
var COLOR_N2   = "#ede9fe";  // violeta claro — Nevera 2
var COLOR_ENV  = "#d1fae5";  // verde claro — Cuarto / DHT11
var COLOR_TIME = "#f1f5f9";  // gris claro  — Timestamp

// ============================================================
//  v4.0 (Sprint 1) — SEGURIDAD DE INGESTA
//  1) Pega aquí el MISMO token configurado en el ESP32 (secrets.h).
//  2) TOKEN_MODE:
//     "observe" → acepta todo pero marca en la columna Token si
//                 la petición venía firmada (fase de verificación).
//     "enforce" → rechaza peticiones sin token válido.
//     Desplegar primero en "observe"; tras confirmar 2–3 registros
//     con Token=OK, cambiar a "enforce" y re-desplegar (misma URL).
// ============================================================
// ⚠ Reemplaza con el mismo token real definido en secrets.h antes de desplegar.
//   No se sube el valor real a git — este es solo un placeholder.
var INGEST_TOKEN = "REEMPLAZAR_CON_TOKEN_REAL";
var TOKEN_MODE   = "enforce";

// Alerta de temperatura fuera de rango
var COLOR_ALERTA_BG   = "#fee2e2";  // rojo claro
var COLOR_ALERTA_TEXT = "#991b1b";  // rojo oscuro
var COLOR_OK_TEXT     = "#000000";

// ============================================================
//  doGet — punto de entrada
// ============================================================
function doGet(e) {
  // v4.0: guarda — evita excepciones en ejecución manual o sin parámetros
  if (!e) e = {};
  if (!e.parameter) e.parameter = {};

  // === ADICIÓN: Si el Dashboard pide datos filtrados ===
  if (e && e.parameter && e.parameter["action"] === "readHistory") {
    return ContentService.createTextOutput(JSON.stringify(obtenerDatosFiltrados(e.parameter)))
      .setMimeType(ContentService.MimeType.JSON);
  }

  // === ADICIÓN: Si un humano abre el link en el navegador ===
  // (Detectamos esto si no vienen parámetros de sensores)
  if (!e.parameter.tempN1 && !e.parameter.action) {
    return HtmlService.createTemplateFromFile('Index')
      .evaluate()
      .setTitle("CENTINELA — Historial · Una IPS")
      .addMetaTag('viewport', 'width=device-width, initial-scale=1')
      .setXFrameOptionsMode(HtmlService.XFrameOptionsMode.ALLOWALL);
  }

  try {
    var sheet = getOrCreateSheet();
    var params = e.parameter;

    if (!params["tempN1"] && !params["tempN2"] && !params["tempExt"] && !params["hum"]) {
      return jsonResponse({ status: "ok", mensaje: "sin datos de sensor" });
    }

    // v4.0: validación de token de ingesta
    var tokenOk = (params["token"] === INGEST_TOKEN);
    if (TOKEN_MODE === "enforce" && !tokenOk) {
      return jsonResponse({ status: "error", mensaje: "no autorizado" });
    }

    var rawN1   = params["tempN1"]  || "ERR";
    var rawN2   = params["tempN2"]  || "ERR";
    var rawExt  = params["tempExt"] || "ERR";
    var rawHum  = params["hum"]     || "ERR";

    var tempN1  = (rawN1  !== "ERR") ? parseFloat(rawN1)  : "ERR";
    var tempN2  = (rawN2  !== "ERR") ? parseFloat(rawN2)  : "ERR";
    var tempExt = (rawExt !== "ERR") ? parseFloat(rawExt) : "ERR";
    var hum     = (rawHum !== "ERR") ? parseFloat(rawHum) : "ERR";

    // v4.0: validación de rango físico plausible en servidor
    if (typeof tempN1  === "number" && (isNaN(tempN1)  || tempN1  < -50 || tempN1  > 125)) tempN1  = "ERR";
    if (typeof tempN2  === "number" && (isNaN(tempN2)  || tempN2  < -50 || tempN2  > 125)) tempN2  = "ERR";
    if (typeof tempExt === "number" && (isNaN(tempExt) || tempExt < -50 || tempExt > 125)) tempExt = "ERR";
    if (typeof hum     === "number" && (isNaN(hum)     || hum     <   0 || hum     > 100)) hum     = "ERR";

    // v4.0: telemetría del dispositivo (columnas F–L, retrocompatible)
    var fw    = params["fw"]    || "";
    var up    = params["up"]    || "";
    var rssi  = params["rssi"]  || "";
    var heap  = params["heap"]  || "";
    var rst   = params["rst"]   || "";
    var fails = params["fails"] || "";

    // v4.3: EDAD DE LA MUESTRA
    // El ESP32 no tiene reloj. Envia cuantos segundos han pasado desde
    // que midio; restarlos da la hora REAL de captura, aunque el dato
    // llegue tras uno o varios reintentos. Sin esto, una muestra
    // recuperada quedaria sellada con la hora de llegada.
    var ageSec = parseInt(params["age"], 10);
    if (isNaN(ageSec) || ageSec < 0 || ageSec > 3600) ageSec = 0;   // guarda de cordura

    // Numero de reintentos que necesito esta muestra (0 = llego al primer envio)
    var intentos = parseInt(params["try"], 10);
    if (isNaN(intentos) || intentos < 0 || intentos > 10) intentos = 0;

    // v4.3.1: IDEMPOTENCIA
    // Cada muestra trae un identificador unico (sid) que se repite en
    // los reintentos. Si ya escribimos ese sid, la peticion es un
    // reintento de algo que SI se guardo: respondemos ok sin duplicar.
    // Sin esto, un envio que se da por fallido pese a haberse escrito
    // generaba dos filas con la misma hora y valores.
    var sid = params["sid"];
    if (sid) {
      var propsSid = PropertiesService.getScriptProperties();
      if (propsSid.getProperty("lastSid") === String(sid)) {
        return jsonResponse({ status: "ok", mensaje: "duplicado ignorado", sid: sid });
      }
    }

    var now = new Date(new Date().getTime() - ageSec * 1000);

    // v4.0: LockService — escritura atómica (evita carreras entre
    // ingesta, archivado y funciones manuales)
    var lock = LockService.getScriptLock();
    lock.waitLock(10000);
    var lastRow;
    try {
      if (sheet.getLastRow() > MAX_ROWS) {
        archivarHoja(sheet);
        sheet = getOrCreateSheet();
      }

      sheet.appendRow([now, tempN1, tempN2, tempExt, hum,
                       fw, up, rssi, heap, rst, fails,
                       tokenOk ? "OK" : "SIN TOKEN", intentos]);
      lastRow = sheet.getLastRow();
      // Registrar el sid recien escrito para detectar reintentos
      if (sid) PropertiesService.getScriptProperties().setProperty("lastSid", String(sid));
    } finally {
      lock.releaseLock();
    }

    sheet.getRange(lastRow, 2, 1, 4).setNumberFormat("0.0");
    sheet.getRange(lastRow, 1).setNumberFormat("dd/MM/yyyy HH:mm:ss");

    aplicarColorFila(sheet, lastRow);

    aplicarAlerta(sheet, lastRow, 2, tempN1);
    aplicarAlerta(sheet, lastRow, 3, tempN2);
    aplicarAlertaCuarto(sheet, lastRow, tempExt, hum);

    return ContentService.createTextOutput(JSON.stringify({
        status : "ok",
        fila   : lastRow
      })).setMimeType(ContentService.MimeType.JSON);

  } catch (err) {
    return jsonResponse({ status: "error", mensaje: err.toString() });
  }
}

// === MOTOR DE BÚSQUEDA Y ESTADÍSTICAS PARA EL DASHBOARD ===
// Retorna labels, datos de los 4 sensores y estadísticas para el index.html
// CORRECCIONES v3.2:
//   - Agrega cumplTE, cumplH (cumplimiento por variable del cuarto)
//   - Agrega alertasTE, alertasH (alertas independientes por variable)
//   - Agrega maxH, minH (rango de humedad)
//   - Agrega maxCumplNev, minCumplNev (rango de cumplimiento de neveras)
//   - Agrega totalNev (total de registros de neveras evaluados)
//   - Mantiene compatibilidad total con v3.1
function obtenerDatosFiltrados(params) {
  // ── Filtros de fecha ────────────────────────────────────────
  var fInicio = params.from ? new Date(parseInt(params.from)) : new Date(0);
  var fFin    = params.to   ? new Date(parseInt(params.to))   : new Date();

  // Lectura MULTIHOJA: pestaña activa + archivadas, ya filtradas por
  // rango y ordenadas cronológicamente.
  var filtrados = leerRegistros(fInicio, fFin);

  if (!filtrados.length) {
    return { labels: [], tempN1: [], tempN2: [], tempExt: [], hum: [], stats: {
      maxNev: '--', minNev: '--', avgN1: '--', avgN2: '--',
      maxTE: '--', minTE: '--', avgTE: '--', avgH: '--',
      maxH: '--', minH: '--',
      cumplimiento: '--', cumplTE: '--', cumplH: '--',
      alertas: 0, alertasTE: 0, alertasH: 0,
      maxCumplNev: '--', minCumplNev: '--', totalNev: 0, total: 0,
      deltaAvg: '--', deltaMax: '--', deltaPct: '--', deltaN: 0,
      hojas: 0
    }, delta: []};
  }

  // Cuántas pestañas se consultaron (se informa en la interfaz para
  // que quede claro que el histórico mostrado es el completo).
  var nHojas = hojasDeDatos(fInicio).length;

  // ── Helpers estadísticos (seguros ante arrays vacíos) ───────
  function safeMax(arr) { return arr.length ? Math.max.apply(null, arr).toFixed(1) : '--'; }
  function safeMin(arr) { return arr.length ? Math.min.apply(null, arr).toFixed(1) : '--'; }
  function safeAvg(arr) {
    if (!arr.length) return '--';
    return (arr.reduce(function(a, b) { return a + b; }, 0) / arr.length).toFixed(1);
  }
  function safePct(ok, total) {
    if (!total) return '--';
    return (ok / total * 100).toFixed(1);
  }

  // ── Extraer valores numéricos por columna ───────────────────
  function toNullable(v) {
    var n = parseFloat(v);
    return isNaN(n) ? null : n;
  }

  var n1Vals = filtrados.map(function(r) { return parseFloat(r[1]); }).filter(function(v) { return !isNaN(v); });
  var n2Vals = filtrados.map(function(r) { return parseFloat(r[2]); }).filter(function(v) { return !isNaN(v); });
  var teVals = filtrados.map(function(r) { return parseFloat(r[3]); }).filter(function(v) { return !isNaN(v); });
  var hVals  = filtrados.map(function(r) { return parseFloat(r[4]); }).filter(function(v) { return !isNaN(v); });

  // ── Estadísticas individuales Nevera 1 ──
var alertasN1 = 0, okN1 = 0;

n1Vals.forEach(function(v){
  if (v >= N1_MIN && v <= N1_MAX) {
    okN1++;
  } else {
    alertasN1++;
  }
});

var cumplN1 = safePct(okN1, n1Vals.length);

// ── Estadísticas individuales Nevera 2 ──
var alertasN2 = 0, okN2 = 0;

n2Vals.forEach(function(v){
  if (v >= N2_MIN && v <= N2_MAX) {
    okN2++;
  } else {
    alertasN2++;
  }
});

var cumplN2 = safePct(okN2, n2Vals.length);

  // ── Cumplimiento neveras ────────────────────────────────────
  // Evalúa AMBAS neveras por registro (si cualquiera está fuera → alerta)
  var fueraRangoNev = 0;
  var dentroRangoNev = 0;
  filtrados.forEach(function(r) {
    var n1ok = (r[1] !== '' && !isNaN(parseFloat(r[1])) && parseFloat(r[1]) >= N1_MIN && parseFloat(r[1]) <= N1_MAX);
    var n2ok = (r[2] !== '' && !isNaN(parseFloat(r[2])) && parseFloat(r[2]) >= N2_MIN && parseFloat(r[2]) <= N2_MAX);
    if (!n1ok || !n2ok) { fueraRangoNev++; } else { dentroRangoNev++; }
  });
  var totalNev = filtrados.length;
  var cumplimiento = totalNev > 0 ? safePct(dentroRangoNev, totalNev) : '--';

  // ── Cumplimiento neveras por ventana (max/min diario) ───────
  // Agrupa registros por día y calcula el % de cumplimiento de cada día
  var cumplPorDia = {};
  var tz = Session.getScriptTimeZone();
  filtrados.forEach(function(r) {
    var dia = Utilities.formatDate(new Date(r[0]), tz, "dd/MM/yyyy");
    if (!cumplPorDia[dia]) { cumplPorDia[dia] = { ok: 0, total: 0 }; }
    cumplPorDia[dia].total++;
    var n1ok = !isNaN(parseFloat(r[1])) && parseFloat(r[1]) >= N1_MIN && parseFloat(r[1]) <= N1_MAX;
    var n2ok = !isNaN(parseFloat(r[2])) && parseFloat(r[2]) >= N2_MIN && parseFloat(r[2]) <= N2_MAX;
    if (n1ok && n2ok) { cumplPorDia[dia].ok++; }
  });
  var pctsDia = [];
  for (var dia in cumplPorDia) {
    var d = cumplPorDia[dia];
    if (d.total > 0) { pctsDia.push(d.ok / d.total * 100); }
  }
  var maxCumplNev = pctsDia.length ? Math.max.apply(null, pctsDia).toFixed(1) + '%' : '--';
  var minCumplNev = pctsDia.length ? Math.min.apply(null, pctsDia).toFixed(1) + '%' : '--';

  // ── Cumplimiento Temperatura Cuarto ────────────────────────
  var alertasTE = 0, okTE = 0;
  filtrados.forEach(function(r) {
    var v = parseFloat(r[3]);
    if (!isNaN(v)) {
      if (v < CUARTO_T_MIN || v > CUARTO_T_MAX) { alertasTE++; } else { okTE++; }
    }
  });
  var totalTE = alertasTE + okTE;
  var cumplTE = safePct(okTE, totalTE);

  // ── Cumplimiento Humedad Cuarto ─────────────────────────────
  var alertasH = 0, okH = 0;
  filtrados.forEach(function(r) {
    var v = parseFloat(r[4]);
    if (!isNaN(v)) {
      if (v < CUARTO_H_MIN || v > CUARTO_H_MAX) { alertasH++; } else { okH++; }
    }
  });
  var totalH = alertasH + okH;
  var cumplH = safePct(okH, totalH);

  // ── Verificación de instrumentación: |Sonda A - Sonda B| ───
  // Solo se evaluan muestras con ambas lecturas validas.
  var deltaSerie = filtrados.map(function(r) {
    var a = parseFloat(r[1]), b = parseFloat(r[2]);
    if (isNaN(a) || isNaN(b)) return null;
    return Math.round(Math.abs(a - b) * 100) / 100;
  });
  var deltaVals = deltaSerie.filter(function(v) { return v !== null; });
  var deltaAvg = '--', deltaMax = '--', deltaPct = '--';
  if (deltaVals.length) {
    deltaAvg = (deltaVals.reduce(function(x, y) { return x + y; }, 0) / deltaVals.length).toFixed(2);
    deltaMax = Math.max.apply(null, deltaVals).toFixed(2);
    var dentro = deltaVals.filter(function(v) { return v < DELTA_TOL; }).length;
    deltaPct = (dentro / deltaVals.length * 100).toFixed(1);
  }

  return {
    delta:   deltaSerie,
    labels:  filtrados.map(function(r) { return Utilities.formatDate(new Date(r[0]), tz, "dd/MM HH:mm"); }),
    tempN1:  filtrados.map(function(r) { return toNullable(r[1]); }),
    tempN2:  filtrados.map(function(r) { return toNullable(r[2]); }),
    tempExt: filtrados.map(function(r) { return toNullable(r[3]); }),
    hum:     filtrados.map(function(r) { return toNullable(r[4]); }),
    stats: {

  // ───────── NEVERA 1 ─────────
  maxN1:       safeMax(n1Vals),
  minN1:       safeMin(n1Vals),
  avgN1:       safeAvg(n1Vals),
  cumplN1:     cumplN1,
  alertasN1:   alertasN1,

  // ───────── NEVERA 2 ─────────
  maxN2:       safeMax(n2Vals),
  minN2:       safeMin(n2Vals),
  avgN2:       safeAvg(n2Vals),
  cumplN2:     cumplN2,
  alertasN2:   alertasN2,

  // Compatibilidad vieja
  maxNev:       safeMax(n1Vals.concat(n2Vals)),
  minNev:       safeMin(n1Vals.concat(n2Vals)),
  cumplimiento: totalNev > 0 ? cumplimiento + "%" : "--",
  alertas:      fueraRangoNev,
  totalNev:     totalNev,
  maxCumplNev:  maxCumplNev,
  minCumplNev:  minCumplNev,
      // Cuarto — Temperatura
      maxTE:        safeMax(teVals),
      minTE:        safeMin(teVals),
      avgTE:        safeAvg(teVals),
      cumplTE:      cumplTE,
      alertasTE:    alertasTE,
      // Cuarto — Humedad
      maxH:         safeMax(hVals),
      minH:         safeMin(hVals),
      avgH:         safeAvg(hVals),
      cumplH:       cumplH,
      alertasH:     alertasH,
      // Verificación de instrumentación
      deltaAvg:     deltaAvg,
      deltaMax:     deltaMax,
      deltaPct:     deltaPct,
      deltaN:       deltaVals.length,
      deltaTol:     DELTA_TOL,
      // General
      hojas:        nHojas,
      total:        filtrados.length
    }
  };
}

// ── Respuesta JSON estándar ─────────────────────────────────
function jsonResponse(obj) {
  return ContentService
    .createTextOutput(JSON.stringify(obj))
    .setMimeType(ContentService.MimeType.JSON);
}

// ============================================================
//  getOrCreateSheet — pestaña activa de datos
//
//  Incluye MIGRACIÓN AUTOMÁTICA: si todavía no existe la pestaña
//  "CENTINELA" pero sí la heredada, la adopta renombrándola. Así el
//  cambio de nombre no pierde ni un registro y no exige intervención
//  manual: ocurre solo en el primer envío tras el despliegue.
// ============================================================
function getOrCreateSheet() {
  var ss    = SpreadsheetApp.getActiveSpreadsheet();
  var sheet = ss.getSheetByName(SHEET_NAME);

  if (!sheet) {
    var heredada = ss.getSheetByName(LEGACY_SHEET_NAME);
    if (heredada) {
      heredada.setName(SHEET_NAME);          // conserva datos y formato
      sheet = heredada;
      Logger.log("Migración: pestaña renombrada a " + SHEET_NAME);
    } else {
      sheet = ss.insertSheet(SHEET_NAME);
      crearEncabezado(sheet);
    }
  } else if (sheet.getLastRow() === 0) {
    crearEncabezado(sheet);                  // existía pero fue vaciada
  }

  return sheet;
}

// ============================================================
//  CONSULTA MULTIHOJA
//
//  Al archivarse, una pestaña pasa a llamarse "CENTINELA [fecha]".
//  Estas funciones localizan la activa y todas las archivadas para
//  que el histórico completo siga siendo consultable.
// ============================================================

// ¿El nombre pertenece a una pestaña de datos de CENTINELA?
function esHojaDeDatos(nombre) {
  var bases = [SHEET_NAME, LEGACY_SHEET_NAME];
  for (var i = 0; i < bases.length; i++) {
    if (nombre === bases[i]) return true;                  // activa
    if (nombre.indexOf(bases[i] + " [") === 0) return true; // archivada
  }
  return false;
}

// Fecha de corte de una pestaña archivada, o null si es la activa.
// Se usa para descartar hojas sin abrirlas: una archivada el 2026-09-08
// no contiene datos posteriores a esa fecha.
function fechaCorteHoja(nombre) {
  var m = nombre.match(/\[(\d{4})-(\d{2})-(\d{2})\]/);
  if (!m) return null;
  return new Date(Number(m[1]), Number(m[2]) - 1, Number(m[3]), 23, 59, 59);
}

// Pestañas que pueden contener datos del rango pedido.
// El filtrado por nombre evita leer hojas irrelevantes: una consulta
// de la última semana no abre pestañas archivadas hace meses.
function hojasDeDatos(fInicio) {
  var ss = SpreadsheetApp.getActiveSpreadsheet();
  var todas = ss.getSheets();
  var sel = [];
  for (var i = 0; i < todas.length; i++) {
    var nombre = todas[i].getName();
    if (!esHojaDeDatos(nombre)) continue;
    var corte = fechaCorteHoja(nombre);
    // Archivada antes del inicio del rango: no aporta nada
    if (corte && fInicio && corte.getTime() < fInicio.getTime()) continue;
    sel.push(todas[i]);
  }
  return sel;
}

// Lee las columnas A–E (fecha y las cuatro variables) de las hojas
// seleccionadas. Leer solo 5 columnas —en vez de las 13— reduce
// notablemente el volumen y evita problemas con pestañas antiguas
// que tenían menos columnas.
function leerRegistros(fInicio, fFin) {
  var hojas = hojasDeDatos(fInicio);
  var filas = [];
  for (var i = 0; i < hojas.length; i++) {
    var sh = hojas[i];
    var ultima = sh.getLastRow();
    if (ultima <= 1) continue;                    // solo encabezado
    var nCols = Math.min(5, sh.getMaxColumns());
    var vals  = sh.getRange(2, 1, ultima - 1, nCols).getValues();
    for (var j = 0; j < vals.length; j++) {
      var r = vals[j];
      if (!r[0]) continue;
      var fecha = (r[0] instanceof Date) ? r[0] : new Date(r[0]);
      if (isNaN(fecha.getTime())) continue;
      if (fecha.getTime() < fInicio.getTime()) continue;
      if (fecha.getTime() > fFin.getTime())    continue;
      filas.push(r);
    }
  }
  // Orden cronológico: las hojas pueden venir en cualquier orden y una
  // muestra recuperada tras reintentos puede quedar fuera de secuencia.
  filas.sort(function(a, b) {
    var da = (a[0] instanceof Date) ? a[0] : new Date(a[0]);
    var db = (b[0] instanceof Date) ? b[0] : new Date(b[0]);
    return da.getTime() - db.getTime();
  });
  return filas;
}

// ============================================================
//  crearEncabezado — escribe la fila 1 con formato completo
// ============================================================
function crearEncabezado(sheet) {
  var headers = [
    "Timestamp",
    "Nevera 1 – Temp. (°C)\nDS18B20 #1",
    "Nevera 2 – Temp. (°C)\nDS18B20 #2",
    "Cuarto – Temp. (°C)\nDHT11",
    "Cuarto – Humedad (%)\nDHT11",
    "FW",             // v4.0: versión de firmware
    "Uptime (s)",     // v4.0: segundos encendido
    "RSSI (dBm)",     // v4.0: señal WiFi
    "Heap libre",     // v4.0: memoria libre
    "Reinicio",       // v4.0: causa último reinicio (diagnóstico UPS)
    "Fallos envío",   // v4.0: envíos fallidos acumulados
    "Token",          // v4.0: OK / SIN TOKEN (modo observe)
    "Reintentos"      // v4.3: intentos necesarios para entregar la muestra
  ];

  sheet.appendRow(headers);

  // Estilo general del encabezado
  var hdr = sheet.getRange(1, 1, 1, headers.length);
  hdr.setBackground(COLOR_HEADER_BG);
  hdr.setFontColor(COLOR_HEADER_TEXT);
  hdr.setFontWeight("bold");
  hdr.setHorizontalAlignment("center");
  hdr.setVerticalAlignment("middle");
  hdr.setWrap(true);
  sheet.setRowHeight(1, 52);

  // Anchos de columna
  sheet.setColumnWidth(1, 160);  // Timestamp
  sheet.setColumnWidth(2, 140);  // Nevera 1
  sheet.setColumnWidth(3, 140);  // Nevera 2
  sheet.setColumnWidth(4, 140);  // Cuarto Temp
  sheet.setColumnWidth(5, 140);  // Cuarto Hum

  // Inmovilizar fila de encabezado
  sheet.setFrozenRows(1);

  // Comentarios descriptivos en cada celda del encabezado
  sheet.getRange(1, 2).setNote("Sensor DS18B20 #1 — instalado dentro de la Nevera 1 (vacunas).\nRango aceptable: " + N1_MIN + " a " + N1_MAX + " °C");
  sheet.getRange(1, 3).setNote("Sensor DS18B20 #2 — Nevera 2.\nRango aceptable: " + N2_MIN + " a " + N2_MAX + " °C");
  sheet.getRange(1, 4).setNote("Sensor DHT11 — temperatura ambiente del Cuarto de Vacunas.");
  sheet.getRange(1, 5).setNote("Sensor DHT11 — humedad relativa del Cuarto de Vacunas.");
}

// ============================================================
//  aplicarColorFila — color de fondo alterno por sección
// ============================================================
function aplicarColorFila(sheet, row) {
  // Columna A — Timestamp
  sheet.getRange(row, 1).setBackground(COLOR_TIME);

  // Columnas B–C — Neveras (se sobreescribirán si hay alerta)
  sheet.getRange(row, 2).setBackground(COLOR_N1);
  sheet.getRange(row, 3).setBackground(COLOR_N2);

  // Columnas D–E — Cuarto
  sheet.getRange(row, 4, 1, 2).setBackground(COLOR_ENV);
}

// ============================================================
//  estiloCelda — FUENTE ÚNICA DE VERDAD del coloreado
//
//  Decide el aspecto de una celda a partir de su valor y del rango
//  vigente. La usan tanto la escritura en vivo como el repintado del
//  histórico, de modo que un cambio de rango se refleja igual en
//  ambos casos sin duplicar lógica.
// ============================================================
function estiloCelda(valor, mn, mx, colorBase) {
  var v = (typeof valor === "number") ? valor : parseFloat(valor);
  if (valor === "ERR" || valor === "" || valor === null ||
      valor === undefined || isNaN(v)) {
    return { bg: "#fef3c7", fc: "#92400e", fw: "normal" };   // sin lectura
  }
  if (v < mn || v > mx) {
    return { bg: COLOR_ALERTA_BG, fc: COLOR_ALERTA_TEXT, fw: "bold" };
  }
  return { bg: colorBase, fc: COLOR_OK_TEXT, fw: "normal" };
}

// ============================================================
//  aplicarAlerta — neveras: col B (Sonda A) y col C (Sonda B)
// ============================================================
function aplicarAlerta(sheet, row, col, valor) {
  var rMin = (col === 3) ? N2_MIN  : N1_MIN;
  var rMax = (col === 3) ? N2_MAX  : N1_MAX;
  var base = (col === 3) ? COLOR_N2 : COLOR_N1;
  var e = estiloCelda(valor, rMin, rMax, base);
  sheet.getRange(row, col)
       .setBackground(e.bg).setFontColor(e.fc).setFontWeight(e.fw);
}

// ============================================================
//  aplicarAlertaCuarto — col D (temp 18–25 °C) y E (hum 30–70 %HR)
// ============================================================
// Cuarto de vacunas: criterio operativo institucional (definitivo)
var CUARTO_T_MIN = 18, CUARTO_T_MAX = 25;
var CUARTO_H_MIN = 30, CUARTO_H_MAX = 70;

function aplicarAlertaCuarto(sheet, row, tempExt, hum) {
  var eT = estiloCelda(tempExt, CUARTO_T_MIN, CUARTO_T_MAX, COLOR_ENV);
  sheet.getRange(row, 4)
       .setBackground(eT.bg).setFontColor(eT.fc).setFontWeight(eT.fw);

  var eH = estiloCelda(hum, CUARTO_H_MIN, CUARTO_H_MAX, COLOR_ENV);
  sheet.getRange(row, 5)
       .setBackground(eH.bg).setFontColor(eH.fc).setFontWeight(eH.fw);
}

// ============================================================
//  repintarHistorico — RECALCULA el color de TODO el histórico
//
//  El color de cada celda se graba al escribirla, así que las filas
//  antiguas conservan el aspecto que tenían con los rangos de
//  entonces. Esta función las repinta con los rangos VIGENTES.
//
//  Escribe por bloques (setBackgrounds/setFontColors/setFontWeights
//  una vez por hoja) en lugar de celda a celda: miles de llamadas
//  agotarían el límite de 6 minutos de Apps Script.
//
//  No modifica ningún dato: solo formato. Ejecutar tras un cambio
//  de rangos, o cuando se quiera homogeneizar el histórico.
// ============================================================
function repintarHistorico() {
  var hojas = hojasDeDatos(null);   // activa + archivadas
  var totalFilas = 0;

  for (var h = 0; h < hojas.length; h++) {
    var sh = hojas[h];
    var n  = sh.getLastRow() - 1;
    if (n <= 0) continue;

    var vals = sh.getRange(2, 1, n, 5).getValues();
    var bg = [], fc = [], fw = [];

    for (var i = 0; i < n; i++) {
      var eB = estiloCelda(vals[i][1], N1_MIN,       N1_MAX,       COLOR_N1);
      var eC = estiloCelda(vals[i][2], N2_MIN,       N2_MAX,       COLOR_N2);
      var eD = estiloCelda(vals[i][3], CUARTO_T_MIN, CUARTO_T_MAX, COLOR_ENV);
      var eE = estiloCelda(vals[i][4], CUARTO_H_MIN, CUARTO_H_MAX, COLOR_ENV);

      bg.push([COLOR_TIME,     eB.bg, eC.bg, eD.bg, eE.bg]);
      fc.push([COLOR_OK_TEXT,  eB.fc, eC.fc, eD.fc, eE.fc]);
      fw.push(["normal",       eB.fw, eC.fw, eD.fw, eE.fw]);
    }

    var rango = sh.getRange(2, 1, n, 5);
    rango.setBackgrounds(bg);
    rango.setFontColors(fc);
    rango.setFontWeights(fw);

    Logger.log("  " + sh.getName() + ": " + n + " filas repintadas");
    totalFilas += n;
  }

  Logger.log("Repintado completo. Hojas: " + hojas.length +
             " | Filas: " + totalFilas);
  Logger.log("Rangos aplicados: Sonda A " + N1_MIN + "-" + N1_MAX +
             " °C | Sonda B " + N2_MIN + "-" + N2_MAX +
             " °C | Cuarto " + CUARTO_T_MIN + "-" + CUARTO_T_MAX +
             " °C | HR " + CUARTO_H_MIN + "-" + CUARTO_H_MAX + " %");
}

// ============================================================
//  archivarHoja — cuando se alcanzan MAX_ROWS, renombra la
//  hoja actual con la fecha y crea una nueva "CENTINELA"
// ============================================================
function archivarHoja(sheet) {
  var fecha      = Utilities.formatDate(new Date(), Session.getScriptTimeZone(), "yyyy-MM-dd");
  var nuevoNombre = SHEET_NAME + " [" + fecha + "]";
  sheet.setName(nuevoNombre);
  Logger.log("Hoja archivada como: " + nuevoNombre);
}

// ============================================================
//  UTILIDADES DE MANTENIMIENTO
//  Se ejecutan manualmente desde el editor cuando hace falta.
// ============================================================

// Vacía la pestaña activa conservando el encabezado y su formato.
// Útil para arrancar un período limpio sin perder la estructura.
function limpiarDatos() {
  var sheet = SpreadsheetApp.getActiveSpreadsheet().getSheetByName(SHEET_NAME);
  if (!sheet) { Logger.log("Pestaña " + SHEET_NAME + " no encontrada."); return; }
  var lastRow = sheet.getLastRow();
  if (lastRow > 1) sheet.deleteRows(2, lastRow - 1);
  Logger.log("Datos limpiados. Encabezado conservado.");
}

// Lista las pestañas de datos que ve el sistema y cuántas filas
// tiene cada una. Diagnóstico rápido del histórico multihoja.
function verHojasDeDatos() {
  var hojas = hojasDeDatos(null);
  Logger.log("Pestañas de CENTINELA detectadas: " + hojas.length);
  for (var i = 0; i < hojas.length; i++) {
    Logger.log("  · " + hojas[i].getName() +
               "  (" + Math.max(hojas[i].getLastRow() - 1, 0) + " registros)");
  }
}

// ============================================================
//  MIGRACIONES — ejecutar UNA SOLA VEZ cada una.
//  Se conservan porque vuelven a hacer falta si se recrea el
//  libro de cálculo o se restaura un respaldo antiguo.
// ============================================================

// Renombra las pestañas ARCHIVADAS que aún usen el nombre heredado,
// para que todo el libro use la identidad CENTINELA. La pestaña
// activa se migra sola en el primer envío (ver getOrCreateSheet).
function migrarNombresHistoricos() {
  var ss = SpreadsheetApp.getActiveSpreadsheet();
  var hojas = ss.getSheets();
  var n = 0;
  for (var i = 0; i < hojas.length; i++) {
    var nombre = hojas[i].getName();
    if (nombre.indexOf(LEGACY_SHEET_NAME + " [") === 0) {
      var nuevo = SHEET_NAME + nombre.substring(LEGACY_SHEET_NAME.length);
      hojas[i].setName(nuevo);
      Logger.log("  " + nombre + "  →  " + nuevo);
      n++;
    } else if (nombre === LEGACY_SHEET_NAME && !ss.getSheetByName(SHEET_NAME)) {
      hojas[i].setName(SHEET_NAME);
      Logger.log("  " + nombre + "  →  " + SHEET_NAME + " (pestaña activa)");
      n++;
    }
  }
  Logger.log("Pestañas migradas: " + n);
}

// ============================================================
//  extenderEncabezadoV4()
//  Añade los títulos de las columnas de telemetría (F–L) a una
//  hoja creada antes de la v4.0, sin tocar los datos.
// ============================================================
function extenderEncabezadoV4() {
  var sheet = getOrCreateSheet();
  var extra = [["FW", "Uptime (s)", "RSSI (dBm)", "Heap libre",
                "Reinicio", "Fallos envío", "Token"]];
  var rng = sheet.getRange(1, 6, 1, 7);      // F1:L1
  rng.setValues(extra);
  rng.setBackground(COLOR_HEADER_BG);
  rng.setFontColor(COLOR_HEADER_TEXT);
  rng.setFontWeight("bold");
  rng.setHorizontalAlignment("center");
  rng.setVerticalAlignment("middle");
  rng.setWrap(true);
  Logger.log("Encabezado v4 extendido (F1:L1).");
}


// ============================================================
//  v4.1 — ALERTAS POR CORREO Y WATCHDOG DE SILENCIO
//
//  El sistema registraba incidentes pero no avisaba a nadie: una
//  nevera danada de madrugada se descubria al dia siguiente. Este
//  modulo revisa la hoja periodicamente y notifica por correo.
//
//  PUESTA EN MARCHA (una sola vez):
//    1. Ejecutar manualmente  probarCorreoAlerta()  y autorizar.
//    2. Ejecutar manualmente  crearActivadorAlertas().
//  Para desactivar: ejecutar  eliminarActivadorAlertas().
// ============================================================
// DESTINATARIOS DE LAS ALERTAS
// Para agregar o quitar personas: edita esta lista (un correo por
// linea, entre comillas, separados por coma) y guarda con Ctrl+S.
// No hace falta redesplegar: el vigilante usa siempre el codigo guardado.
var ALERT_EMAILS = [
  "mantenimiento.demo@example.com",
  "coordinacion.demo@example.com",
  "vacunacion.demo@example.com",
  "farmacia.demo@example.com"
];
var ALERT_EMAIL     = ALERT_EMAILS.join(",");   // MailApp acepta lista separada por comas
var ALERT_CHECK_MIN = 10;   // periodicidad del activador (1,5,10,15,30)
var SILENCE_MIN     = 35;   // sin registros por mas tiempo -> equipo mudo
                            // (3,5 intervalos de 10 min: tolera un envio
                            //  perdido sin generar falsas alarmas)
var ALERT_CONFIRM_N = 2;    // lecturas seguidas fuera de rango para avisar
                            // (2 x 10 min: el correo llega a los ~20 min;
                            //  la alarma sonora local sigue siendo inmediata)
var REMINDER_HOURS  = 4;    // recordatorio mientras la alerta siga activa

// ── Revision principal (la ejecuta el activador) ─────────────
function revisarAlertas() {
  var ss = SpreadsheetApp.getActiveSpreadsheet();
  // Tolerante a la migración: si aún no existe la pestaña con el
  // nombre nuevo, usa la heredada. Nunca crea hojas.
  var sheet = ss.getSheetByName(SHEET_NAME) || ss.getSheetByName(LEGACY_SHEET_NAME);
  if (!sheet || sheet.getLastRow() <= 1) return;

  var lastRow = sheet.getLastRow();
  var n    = Math.min(ALERT_CONFIRM_N, lastRow - 1);
  var rows = sheet.getRange(lastRow - n + 1, 1, n, 5).getValues();

  var ultima = rows[rows.length - 1];
  var tsUlt  = (ultima[0] instanceof Date) ? ultima[0] : new Date(ultima[0]);
  var tz      = Session.getScriptTimeZone();
  var fechaTx = Utilities.formatDate(tsUlt, tz, "dd/MM/yyyy HH:mm:ss");
  var minsSin = (new Date().getTime() - tsUlt.getTime()) / 60000;

  var props  = PropertiesService.getScriptProperties();
  var estado = JSON.parse(props.getProperty("alertState") || "{}");

  // ── 1) Watchdog: el equipo dejo de reportar ───────────────
  if (minsSin > SILENCE_MIN) {
    notificarAlerta(estado, props, "silencio",
      "SIN REPORTES de CENTINELA",
      "El equipo no registra datos desde hace " + Math.round(minsSin) + " minutos.\n" +
      "Ultimo registro: " + fechaTx + "\n\n" +
      "Posibles causas: corte de energia (revisar UPS), caida de la red WiFi\n" +
      "o equipo apagado. Se recomienda verificar el cuarto de vacunas.");
    return;   // con datos viejos no tiene sentido evaluar rangos
  }
  resolverAlerta(estado, props, "silencio",
    "Equipo reportando de nuevo",
    "CENTINELA volvio a registrar datos normalmente.\nUltimo registro: " + fechaTx);

  // ── 2) Valores fuera de rango sostenidos ──────────────────
  var fuera = [];
  function evaluarVariable(idx, nombre, mn, mx, unidad) {
    var todasFuera = true, ultimoVal = null;
    for (var i = 0; i < rows.length; i++) {
      var v = parseFloat(rows[i][idx]);
      if (isNaN(v) || (v >= mn && v <= mx)) { todasFuera = false; break; }
      ultimoVal = v;
    }
    if (todasFuera && ultimoVal !== null) {
      fuera.push(nombre + ": " + ultimoVal.toFixed(1) + " " + unidad +
                 "   (rango normal " + mn + " a " + mx + " " + unidad + ")");
    }
  }
  evaluarVariable(1, "Nevera 1 (vacunas)",     N1_MIN,       N1_MAX,       "C");
  evaluarVariable(2, "Nevera 2",               N2_MIN,       N2_MAX,       "C");
  evaluarVariable(3, "Temperatura del cuarto", CUARTO_T_MIN, CUARTO_T_MAX, "C");
  evaluarVariable(4, "Humedad del cuarto",     CUARTO_H_MIN, CUARTO_H_MAX, "%");

  if (fuera.length > 0) {
    notificarAlerta(estado, props, "rango",
      "ALERTA: valores fuera de rango",
      "Se detectaron valores fuera de rango en " + ALERT_CONFIRM_N +
      " lecturas consecutivas:\n\n   " + fuera.join("\n   ") +
      "\n\nHora del ultimo registro: " + fechaTx +
      "\n\nSe recomienda verificar los equipos de refrigeracion.");
  } else {
    resolverAlerta(estado, props, "rango",
      "Valores normalizados",
      "Todas las variables volvieron al rango normal.\nUltimo registro: " + fechaTx);
  }
}

// ── Envia solo en la transicion, con recordatorio periodico ──
function notificarAlerta(estado, props, clave, asunto, cuerpo) {
  var ahora = new Date().getTime();
  var e = estado[clave];
  var enviar = (!e || !e.activa) ||
               ((ahora - (e.ultimoAviso || 0)) > REMINDER_HOURS * 3600000);
  if (!enviar) return;
  enviarCorreoAlerta(asunto, cuerpo);
  estado[clave] = { activa: true, ultimoAviso: ahora };
  props.setProperty("alertState", JSON.stringify(estado));
}

// ── Avisa el retorno a la normalidad (solo si hubo alerta) ───
function resolverAlerta(estado, props, clave, asunto, cuerpo) {
  var e = estado[clave];
  if (!e || !e.activa) return;
  enviarCorreoAlerta(asunto, cuerpo);
  estado[clave] = { activa: false, ultimoAviso: new Date().getTime() };
  props.setProperty("alertState", JSON.stringify(estado));
}

function enviarCorreoAlerta(asunto, cuerpo) {
  var url = "";
  try { url = ScriptApp.getService().getUrl(); } catch (e) { url = ""; }
  try {
    MailApp.sendEmail({
      to: ALERT_EMAIL,
      subject: "[CENTINELA] " + asunto,
      body: cuerpo +
            "\n\n----------------------------------------\n" +
            "Una IPS - Cuarto de Vacunas\n" +
            (url ? "Historial: " + url + "\n" : "") +
            "Mensaje automatico del sistema de monitoreo."
    });
    Logger.log("Correo enviado: " + asunto);
  } catch (err) {
    Logger.log("Error enviando correo: " + err.toString());
  }
}

// ── Gestion del activador (ejecutar manualmente) ─────────────
function crearActivadorAlertas() {
  eliminarActivadorAlertas();
  ScriptApp.newTrigger("revisarAlertas").timeBased().everyMinutes(ALERT_CHECK_MIN).create();
  Logger.log("Activador creado: revisarAlertas cada " + ALERT_CHECK_MIN + " minutos.");
}

function eliminarActivadorAlertas() {
  var trs = ScriptApp.getProjectTriggers();
  var n = 0;
  for (var i = 0; i < trs.length; i++) {
    if (trs[i].getHandlerFunction() === "revisarAlertas") { ScriptApp.deleteTrigger(trs[i]); n++; }
  }
  Logger.log("Activadores eliminados: " + n);
}

// ── Pruebas manuales ─────────────────────────────────────────
function probarCorreoAlerta() {
  enviarCorreoAlerta("Prueba de notificacion",
    "Si recibes este correo, las alertas del sistema estan configuradas correctamente.\n" +
    "Revision automatica cada " + ALERT_CHECK_MIN + " minutos.");
}

function verEstadoAlertas() {
  var p = PropertiesService.getScriptProperties().getProperty("alertState");
  Logger.log("Estado de alertas: " + (p || "sin registros aun"));
}

function reiniciarEstadoAlertas() {
  PropertiesService.getScriptProperties().deleteProperty("alertState");
  Logger.log("Estado de alertas reiniciado.");
}


// ============================================================
//  v4.2 — prepararColumnaFW()
//  EJECUTAR UNA VEZ. Google Sheets interpretaba "4.1.0" como una
//  fecha y lo mostraba como 4.1.2000. Fijar la columna F como
//  texto plano evita esa conversion automatica.
//  (El firmware v4.2 ademas envia la version con prefijo "v".)
// ============================================================
function prepararColumnaFW() {
  var sheet = getOrCreateSheet();
  var filas = Math.max(sheet.getMaxRows() - 1, 1);
  sheet.getRange(2, 6, filas, 1).setNumberFormat("@");
  Logger.log("Columna F (FW) fijada como texto plano.");
}


// ============================================================
//  v4.3 — extenderEncabezadoV43()
//  EJECUTAR UNA VEZ. Anade el titulo de la columna M (Reintentos)
//  a la hoja existente, sin tocar los datos.
// ============================================================
function extenderEncabezadoV43() {
  var sheet = getOrCreateSheet();
  var rng = sheet.getRange(1, 13, 1, 1);      // M1
  rng.setValue("Reintentos");
  rng.setBackground(COLOR_HEADER_BG);
  rng.setFontColor(COLOR_HEADER_TEXT);
  rng.setFontWeight("bold");
  rng.setHorizontalAlignment("center");
  rng.setVerticalAlignment("middle");
  rng.setWrap(true);
  Logger.log("Encabezado v4.3 extendido (M1 = Reintentos).");
}
