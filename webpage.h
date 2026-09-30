#pragma once
// ============================================================
//  webpage.h  —  Dashboard local de CENTINELA (servido por el ESP32)
//  Una IPS — Cuarto de Vacunas
//
//  NOTA (versión portafolio): copia sanitizada. Logo, nombre de
//  organización y URL de Apps Script son ficticios/placeholder.
//  Ver README.md.
//
//  CAMBIOS v4.0 (Sprint 1):
//    - Intervalo de registro DINÁMICO: llega en /api/data
//      (campo "interval") y corrige etiquetas de tiempo y footer.
//      Se acabó el SAMPLE_MIN=1 fijo.
//    - Robustez ante CDN: si Chart.js no carga (internet lento),
//      se muestra aviso claro y se reintenta solo — antes la
//      página moría en silencio (causa de los "bugs" del panel).
//    - Botón PDF: verifica que jsPDF esté cargada; si no, la
//      recarga y avisa, en lugar de fallar sin mensaje.
//
//  CAMBIOS v3.3:
//    - Nueva paleta de colores:
//        Nevera 1  → #00AEEF  (azul cielo)
//        Nevera 2  → #6C63FF  (violeta)
//        Cuarto    → #10B981  (verde esmeralda)
//    - Logo institucional agregado en header
//    - Eliminado botón "Exportar historial desde Google Sheets"
//      (solo queda HISTORIAL CLOUD)
//    - Líneas de neveras: continuas (solid)
//      Líneas de cuarto: punteadas/discontinuas
//    - Color ONLINE → #07EB47 (verde brillante)
//    - Nueva sección "Reportes Históricos" con:
//        · Bloque NEVERAS: cumplimiento %, max, min, alertas
//        · Bloque CUARTO TEMP: cumplimiento %, max, min, alertas
//        · Bloque CUARTO HUM: cumplimiento %, max, min, alertas
//    - 2 nuevas gráficas inferiores:
//        · Comparación de temperaturas (Ambient + N1 + N2)
//        · Humedad cuarto de vacunas
// ============================================================

static const char INDEX_HTML[] PROGMEM = R"rawhtml(
<!DOCTYPE html>
<html lang="es">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>CENTINELA — Cuarto de Vacunas</title>
<link rel="preconnect" href="https://fonts.googleapis.com">
<link href="https://fonts.googleapis.com/css2?family=Rajdhani:wght@400;500;600;700&family=Exo+2:wght@300;400;600&display=swap" rel="stylesheet">
<script src="https://cdnjs.cloudflare.com/ajax/libs/Chart.js/4.4.1/chart.umd.min.js"></script>
<script src="https://cdnjs.cloudflare.com/ajax/libs/jspdf/2.5.1/jspdf.umd.min.js"></script>
<style>
/* ============================================================
   PALETA v3.3
   Nevera 1 : #00AEEF  (azul cielo)
   Nevera 2 : #6C63FF  (violeta)
   Cuarto   : #10B981  (verde esmeralda)
   ONLINE   : #07EB47  (verde brillante)
============================================================ */
:root{
  --bg:#f0f4f8; --sf:#ffffff; --bd:#d0dce9;
  --c-n1:#00AEEF;  --c-n1b:rgba(0,174,239,.08);
  --c-n2:#6C63FF;  --c-n2b:rgba(108,99,255,.08);
  --c-env:#10B981; --c-envb:rgba(16,185,129,.08);
  --c-online:#07EB47;
  --dark:#1a2535; --dark2:#2c3e50;
  --tx:#1a2535; --su:#6b7f92; --su2:#94a3b8;
  --red:#e74c3c; --green:#27ae60;
  --font-h:'Rajdhani',sans-serif;
  --font-b:'Exo 2',sans-serif;
}
*,*::before,*::after{box-sizing:border-box;margin:0;padding:0}
html{scroll-behavior:smooth}
body{background:var(--bg);color:var(--tx);font-family:var(--font-b);min-height:100vh;overflow-x:hidden}
body::before{content:'';position:fixed;inset:0;
  background:linear-gradient(135deg,#e8f4fb 0%,#f0f4f8 50%,#e8f0f7 100%);
  pointer-events:none;z-index:0}

/* ── Wrapper ─────────────────────────────────────────────── */
.wrap{position:relative;z-index:1;max-width:1100px;margin:0 auto;padding:18px 16px 40px}

/* ── Header ─────────────────────────────────────────────── */
header{
  background:linear-gradient(135deg,var(--dark) 0%,var(--dark2) 100%);
  border-radius:16px;padding:16px 20px;margin-bottom:20px;
  display:flex;align-items:center;gap:14px;flex-wrap:wrap;
  box-shadow:0 8px 32px rgba(0,0,0,.18),0 0 0 1px rgba(0,174,239,.2);
  position:relative;overflow:hidden}
header::before{content:'';position:absolute;inset:0;
  background:linear-gradient(90deg,rgba(0,174,239,.1),transparent 60%);pointer-events:none}
header::after{content:'';position:absolute;bottom:0;left:0;right:0;height:3px;
  background:linear-gradient(90deg,var(--c-n1),var(--c-n2),var(--c-env))}

/* Logo institucional en header */
.logo-img{height:36px;width:auto;flex-shrink:0;filter:drop-shadow(0 0 6px rgba(0,174,239,.25))}
.hdr-text{flex:1;min-width:0}
.hdr-text h1{
  font-family:var(--font-h);font-size:clamp(.78rem,1.8vw,1.05rem);
  font-weight:700;color:#fff;letter-spacing:.05em;
  text-transform:uppercase;line-height:1.3}
.hdr-text .sub{font-size:.7rem;color:var(--su2);margin-top:2px;font-family:var(--font-b)}

/* Badge ONLINE / OFFLINE */
.badge{
  display:flex;align-items:center;gap:6px;padding:5px 13px;border-radius:20px;
  font-size:.62rem;font-family:var(--font-h);font-weight:600;
  border:1.5px solid var(--c-online);
  color:var(--c-online);
  background:rgba(7,235,71,.1);
  letter-spacing:.08em;text-transform:uppercase;white-space:nowrap;flex-shrink:0}
.badge.off{border-color:var(--red);color:var(--red);background:rgba(231,76,60,.08)}
.dot{width:7px;height:7px;border-radius:50%;background:currentColor;flex-shrink:0}
.dot.pulse{animation:pulse 1.6s ease infinite}
@keyframes pulse{0%,100%{opacity:1}50%{opacity:.25}}

/* ── Sección label ──────────────────────────────────────── */
.sec-label{
  font-family:var(--font-h);font-size:.68rem;font-weight:700;
  letter-spacing:.16em;text-transform:uppercase;color:#8899aa;
  margin-bottom:12px;padding-left:2px;display:flex;align-items:center;gap:8px}
.sec-label::before{content:'';display:inline-block;width:18px;height:2px;
  background:linear-gradient(90deg,var(--c-n1),transparent)}

/* ── Cards de sensores ──────────────────────────────────── */
.cards{
  display:grid;
  grid-template-columns:repeat(auto-fit,minmax(235px,1fr));
  gap:14px;margin-bottom:20px}
@media(max-width:900px){.cards{grid-template-columns:repeat(2,1fr)}}
@media(max-width:520px){.cards{grid-template-columns:1fr}}

.card{
  background:var(--sf);border:1.5px solid var(--bd);border-radius:14px;
  padding:18px;position:relative;overflow:hidden;
  transition:transform .2s,box-shadow .2s;
  box-shadow:0 2px 12px rgba(0,0,0,.06)}
.card:hover{transform:translateY(-2px);box-shadow:0 8px 24px rgba(0,0,0,.1)}
.card::before{content:'';position:absolute;top:0;left:0;right:0;height:4px;border-radius:14px 14px 0 0}
.card-strip{position:absolute;right:0;top:0;bottom:0;width:4px;border-radius:0 14px 14px 0;opacity:.25}

/* Nevera 1 — Azul cielo #00AEEF */
.c-n1::before{background:linear-gradient(90deg,#00AEEF,#0090c8)}
.c-n1{border-color:rgba(0,174,239,.25)}
.c-n1:hover{border-color:rgba(0,174,239,.5)}
.c-n1 .card-strip{background:linear-gradient(180deg,var(--c-n1),transparent)}
.c-n1 .metric-val{color:var(--c-n1)}

/* Nevera 2 — Violeta #6C63FF */
.c-n2::before{background:linear-gradient(90deg,#6C63FF,#5549e0)}
.c-n2{border-color:rgba(108,99,255,.25)}
.c-n2:hover{border-color:rgba(108,99,255,.5)}
.c-n2 .card-strip{background:linear-gradient(180deg,var(--c-n2),transparent)}
.c-n2 .metric-val{color:var(--c-n2)}

/* Verificacion de instrumentacion — Slate (no es cadena de frio) */
.c-ver::before{background:linear-gradient(90deg,#475569,#2c3e50)}
.c-ver{border-color:rgba(71,85,105,.25);background:#fbfcfe}
.c-ver:hover{border-color:rgba(71,85,105,.5)}
.c-ver .card-strip{background:linear-gradient(180deg,#475569,transparent)}
.c-ver .metric-val{color:#334155}

/* Semaforo de consistencia entre sondas */
.dchip{
  display:inline-flex;align-items:center;gap:6px;margin-top:12px;
  padding:5px 12px;border-radius:20px;
  font-family:var(--font-h);font-size:.6rem;font-weight:700;
  letter-spacing:.08em;text-transform:uppercase;
  border:1.5px solid var(--su2);color:var(--su);background:#f1f5f9}
.dchip .dot{width:8px;height:8px;border-radius:50%;background:currentColor}
.dchip.ok  {border-color:#16a34a;color:#15803d;background:rgba(22,163,74,.09)}
.dchip.warn{border-color:#d97706;color:#b45309;background:rgba(217,119,6,.10)}
.dchip.crit{border-color:var(--red);color:#b91c1c;background:rgba(231,76,60,.10)}

/* Nota de campaña temporal */
.campaign-note{
  display:flex;align-items:flex-start;gap:9px;
  background:#f1f5f9;border:1.5px solid var(--bd);border-left:4px solid #475569;
  border-radius:10px;padding:11px 15px;margin-bottom:16px;
  font-size:.7rem;line-height:1.55;color:var(--su)}
.campaign-note b{color:var(--dark2);font-weight:700}

/* Cuarto — Verde esmeralda #10B981 */
.c-env::before{background:linear-gradient(90deg,#10B981,#0d9268)}
.c-env{border-color:rgba(16,185,129,.25)}
.c-env:hover{border-color:rgba(16,185,129,.5)}
.c-env .card-strip{background:linear-gradient(180deg,var(--c-env),transparent)}
.c-env .metric-val{color:var(--c-env)}

.card-device{
  font-family:var(--font-h);font-size:.55rem;font-weight:600;color:var(--su);
  letter-spacing:.12em;text-transform:uppercase;margin-bottom:4px}
.card-title{font-family:var(--font-h);font-size:.9rem;font-weight:700;color:var(--dark2)}
.metric{margin-top:12px}
.metric-lbl{
  font-size:.55rem;font-family:var(--font-h);color:var(--su);
  text-transform:uppercase;letter-spacing:.1em;margin-bottom:4px}
.metric-val{
  font-family:var(--font-h);font-size:2.2rem;font-weight:700;
  line-height:1;transition:color .4s}
.metric-val .unit{font-size:.85rem;font-weight:400;color:var(--su2);margin-left:2px}
.metric-row{display:flex;gap:14px;flex-wrap:wrap}
.metric-row .metric{flex:1;min-width:72px}

/* ── Alertas en cards ───────────────────────────────────── */
.card.alerta{
  border-color:var(--red)!important;
  box-shadow:0 0 0 3px rgba(231,76,60,.15),0 4px 20px rgba(231,76,60,.12)!important}
.card.alerta .metric-val{color:var(--red)!important}
.alerta-badge{
  display:none;
  font-family:var(--font-h);font-size:.57rem;font-weight:700;
  color:var(--red);border:1.5px solid var(--red);border-radius:5px;
  padding:4px 10px;margin-top:12px;letter-spacing:.06em;
  text-transform:uppercase;width:fit-content;
  animation:badge-blink 2s ease infinite}
@keyframes badge-blink{0%,100%{opacity:1}50%{opacity:.6}}
.card.alerta .alerta-badge{display:block}

/* ── Controles (historial) ──────────────────────────────── */
.controls{
  display:flex;flex-wrap:wrap;align-items:center;
  gap:8px;margin-bottom:18px}
.ctrl-group{display:flex;align-items:center;gap:6px;flex-wrap:wrap}
.ctrl-lbl{
  font-family:var(--font-h);font-size:.62rem;font-weight:600;color:var(--su);
  text-transform:uppercase;letter-spacing:.1em;white-space:nowrap}
.btn{
  background:var(--sf);border:1.5px solid var(--bd);color:var(--su);
  font-family:var(--font-h);font-weight:600;font-size:.68rem;padding:5px 13px;
  border-radius:6px;cursor:pointer;letter-spacing:.06em;
  transition:all .2s;text-transform:uppercase}
.btn:hover{border-color:var(--c-n1);color:var(--c-n1)}
.btn.active{
  border-color:var(--c-n1);color:var(--c-n1);
  background:var(--c-n1b);font-weight:700}
.btn:disabled{opacity:.38;cursor:not-allowed}
.btn:disabled:hover{border-color:var(--bd);color:var(--su)}

/* ── Leyenda de la ventana de muestras ──────────────────── */
.win-info{
  font-family:var(--font-h);font-size:.64rem;font-weight:600;
  color:var(--su);letter-spacing:.04em;margin:-6px 0 12px 2px;
  display:flex;align-items:center;gap:8px;flex-wrap:wrap}
.win-info b{color:var(--dark2);font-weight:700}
.win-info .sep-dot{color:var(--su2)}
.sep{width:1px;height:22px;background:var(--bd);flex-shrink:0}
@media(max-width:480px){.sep{display:none}.controls{gap:6px}}

.btn-exp{
  background:var(--sf);border:1.5px solid var(--bd);color:var(--su);
  font-family:var(--font-h);font-weight:600;font-size:.68rem;padding:5px 13px;
  border-radius:6px;cursor:pointer;letter-spacing:.06em;
  transition:all .2s;text-transform:uppercase}
.btn-exp:hover{border-color:var(--c-n1);color:var(--c-n1)}
.btn-cloud{
  background:var(--dark);border:1.5px solid var(--dark);color:#fff;
  font-family:var(--font-h);font-weight:600;font-size:.65rem;padding:5px 13px;
  border-radius:6px;cursor:pointer;letter-spacing:.06em;
  transition:opacity .2s;text-transform:uppercase}
.btn-cloud:hover{opacity:.82}

/* ── Gráficas locales ───────────────────────────────────── */
.charts{
  display:grid;grid-template-columns:1fr 1fr;
  gap:14px;margin-bottom:22px}
@media(max-width:620px){.charts{grid-template-columns:1fr}}
.chart-card{
  background:var(--sf);border:1.5px solid var(--bd);
  border-radius:14px;padding:18px;box-shadow:0 2px 8px rgba(0,0,0,.05)}
.chart-title{
  font-family:var(--font-h);font-size:.66rem;font-weight:700;
  text-transform:uppercase;letter-spacing:.1em;color:var(--dark2);margin-bottom:12px}
.chart-wrap{position:relative;height:190px}

/* ── Chip última actualización (header) ─────────────────── */
.chip{
  display:flex;align-items:center;gap:6px;padding:5px 11px;border-radius:20px;
  font-size:.62rem;font-family:var(--font-h);font-weight:600;
  border:1.5px solid rgba(148,163,184,.45);color:var(--su2);
  letter-spacing:.06em;white-space:nowrap;flex-shrink:0}
.chip b{color:#fff;font-weight:700}

/* ── Banner de estado global del sistema ────────────────── */
#sysBanner{
  display:none;align-items:center;gap:10px;
  border:1.5px solid;border-radius:12px;padding:10px 16px;margin-bottom:18px;
  font-family:var(--font-h);font-weight:700;font-size:.72rem;
  letter-spacing:.08em;text-transform:uppercase}
#sysBanner.show{display:flex}
#sysBanner.warn{border-color:#d97706;color:#92400e;background:#fef3c7}
#sysBanner.crit{border-color:var(--red);color:#7f1d1d;background:#fee2e2;
  animation:badge-blink 2s ease infinite}

/* ── Meta de cards: min / máx / tendencia ───────────────── */
.card-meta{
  display:flex;gap:12px;align-items:center;margin-top:12px;
  padding-top:10px;border-top:1px dashed var(--bd);
  font-family:var(--font-h);font-size:.6rem;color:var(--su);
  text-transform:uppercase;letter-spacing:.08em;flex-wrap:wrap}
.card-meta b{color:var(--dark);font-weight:700}
.trend{margin-left:auto;font-size:.8rem;font-weight:700;color:var(--dark2)}
.trend.eq{color:var(--su2)}

/* ── Datos obsoletos (sin enlace con el panel) ──────────── */
.stale .metric-val{opacity:.35;filter:grayscale(.6)}

/* ── Microinteracción al actualizar valor ───────────────── */
.metric-val.tick{animation:tick .45s ease}
@keyframes tick{0%{transform:scale(1)}35%{transform:scale(1.05)}100%{transform:scale(1)}}

/* ── Accesibilidad ──────────────────────────────────────── */
button:focus-visible{outline:2px solid var(--c-n1);outline-offset:2px}
@media (prefers-reduced-motion:reduce){
  *,*::before,*::after{animation:none!important;transition:none!important}
}

/* ── Footer ─────────────────────────────────────────────── */
footer{
  margin-top:24px;text-align:center;
  font-family:var(--font-h);font-size:.65rem;font-weight:600;color:#324152;
  border-top:1px solid rgba(44,62,80,.18);padding-top:14px;
  letter-spacing:.05em;line-height:1.9;opacity:.92}
footer span{color:#16202c;font-weight:700}

/* ── Responsive extra — móvil ───────────────────────────── */
@media(max-width:480px){
  .wrap{padding:12px 10px 32px}
  header{padding:12px 14px;gap:10px;border-radius:12px}
  .logo-img{height:36px}
  .hdr-text h1{font-size:.72rem}
  .badge{padding:4px 8px;font-size:.58rem}
  .metric-val{font-size:1.8rem}
  .btn,.btn-exp,.btn-range{font-size:.62rem;padding:4px 10px}
  .btn-cloud{font-size:.6rem;padding:4px 10px}
  .exp-custom input[type=datetime-local]{font-size:.6rem;padding:3px 6px}
  footer{font-size:.6rem;line-height:1.7}
}
</style>
</head>
<body>
<div class="wrap">

<!-- ══ HEADER ══════════════════════════════════════════════ -->
<header>
  <img class="logo-img" id="logoImg" alt="Una IPS">
  <div class="hdr-text">
    <h1>CENTINELA &mdash; Cuarto de Vacunas</h1>
    <div class="sub">Una IPS &middot; Registro cada <span id="ivalH">10</span> min</div>
  </div>
  <!-- Badge ONLINE con color #07EB47 -->
  <div class="badge" id="wifiBadge" role="status">
    <div class="dot pulse" id="wifiDot"></div>
    <span id="wifiTxt">ONLINE</span>
  </div>
  <div class="chip" title="Ultima actualizacion del panel">&#8635; <b id="hdrUpd">--:--</b></div>
</header>

<!-- ══ BANNER ESTADO GLOBAL ═════════════════════════════════ -->
<div id="sysBanner" role="status" aria-live="polite"><span id="sysBannerTxt"></span></div>

<!-- ══ CARDS DE SENSORES ════════════════════════════════════ -->
<div class="sec-label">Estado en tiempo real</div>

<!-- Nota de la campaña de validacion metrologica (etapa temporal) -->
<div class="campaign-note" role="note">
  <span>&#9878;</span>
  <span><b>Campaña de validación metrológica.</b> Las dos sondas DS18B20 están
  instaladas dentro de la <b>misma Nevera 1</b> para verificar por redundancia la
  consistencia de la instrumentación antes de la puesta en marcha de la segunda
  unidad de refrigeración. No existen dos neveras en operación.</span>
</div>

<div class="cards">

  <!-- Nevera 1 — Azul cielo #00AEEF -->
  <div class="card c-n1" id="cardN1">
    <div class="card-strip"></div>
    <div class="card-device">DS18B20 #1 &middot; OneWire GPIO4</div>
    <div class="card-title">Nevera 1 &middot; Sonda A</div>
    <div class="metric">
      <div class="metric-lbl">Temperatura Interna</div>
      <div class="metric-val" id="vN1">--<span class="unit">&deg;C</span></div>
    </div>
    <div class="card-meta">
      <span>M&iacute;n <b id="mnN1">--</b></span>
      <span>M&aacute;x <b id="mxN1">--</b></span>
      <span class="trend eq" id="trN1" title="Tendencia">&#9644;</span>
    </div>
    <div class="alerta-badge">&#9888; FUERA DE RANGO (2&ndash;8 &deg;C)</div>
  </div>

  <!-- Nevera 2 — Violeta #6C63FF -->
  <div class="card c-n2" id="cardN2">
    <div class="card-strip"></div>
    <div class="card-device">DS18B20 #2 &middot; OneWire GPIO4</div>
    <div class="card-title">Nevera 1 &middot; Sonda B <span style="font-weight:400;color:var(--su)">(Verificación)</span></div>
    <div class="metric">
      <div class="metric-lbl">Temperatura Interna</div>
      <div class="metric-val" id="vN2">--<span class="unit">&deg;C</span></div>
    </div>
    <div class="card-meta">
      <span>M&iacute;n <b id="mnN2">--</b></span>
      <span>M&aacute;x <b id="mxN2">--</b></span>
      <span class="trend eq" id="trN2" title="Tendencia">&#9644;</span>
    </div>
    <div class="alerta-badge">&#9888; FUERA DE RANGO (2&ndash;8 &deg;C)</div>
  </div>

  <!-- Cuarto de Vacunas — Verde esmeralda #10B981 -->
  <div class="card c-env" id="cardCuarto">
    <div class="card-strip"></div>
    <div class="card-device">DHT11 &middot; GPIO5</div>
    <div class="card-title">Cuarto de Vacunas</div>
    <div class="metric-row">
      <div class="metric">
        <div class="metric-lbl">Temperatura</div>
        <div class="metric-val" id="vTE">--<span class="unit">&deg;C</span></div>
      </div>
      <div class="metric">
        <div class="metric-lbl">Humedad</div>
        <div class="metric-val" id="vH">--<span class="unit">%</span></div>
      </div>
    </div>
    <div class="card-meta">
      <span>T <b id="mnTE">--</b>/<b id="mxTE">--</b></span>
      <span>HR <b id="mnH">--</b>/<b id="mxH">--</b></span>
      <span class="trend eq" id="trTE" title="Tendencia temperatura">&#9644;</span>
    </div>
    <div class="alerta-badge" id="cuartoBadge">&#9888; FUERA DE RANGO</div>
  </div>

  <!-- Verificación de Instrumentación — consistencia entre sondas -->
  <div class="card c-ver" id="cardVer">
    <div class="card-strip"></div>
    <div class="card-device">Redundancia &middot; Sonda A vs Sonda B</div>
    <div class="card-title">Verificación de Instrumentación</div>
    <div class="metric">
      <div class="metric-lbl">Desviación &Delta;T</div>
      <div class="metric-val" id="vDelta">--<span class="unit">&deg;C</span></div>
    </div>
    <div class="dchip" id="dChip">
      <span class="dot"></span><span id="dChipTxt">SIN DATOS</span>
    </div>
  </div>

</div><!-- /.cards -->

<!-- ══ CONTROLES ════════════════════════════════════════════ -->
<div class="controls">
  <div class="ctrl-group">
    <span class="ctrl-lbl">Muestras:</span>
    <button class="btn"        id="w12"  onclick="setWindow(12)">12</button>
    <button class="btn"        id="w24"  onclick="setWindow(24)">24</button>
    <button class="btn active" id="w48"  onclick="setWindow(48)">48</button>
    <button class="btn"        id="w72"  onclick="setWindow(72)">72</button>
    <button class="btn"        id="w96"  onclick="setWindow(96)">96</button>
    <button class="btn"        id="w144" onclick="setWindow(144)">144</button>
  </div>
  <div class="sep"></div>
  <div class="ctrl-group">
    <span class="ctrl-lbl">Exportar:</span>
    <button class="btn-exp" onclick="exportPNG()">&#128247; PNG</button>
    <button class="btn-exp" onclick="exportPDF()">&#128196; PDF</button>
    <!-- Solo queda HISTORIAL CLOUD — eliminado "Exportar historial desde Google Sheets" -->
    <!-- Reemplaza por la URL /exec de TU propio despliegue de Code_v4.gs. -->
    <button class="btn-cloud"
      onclick="window.open('https://script.google.com/macros/s/TU_ID_DE_DESPLIEGUE/exec','_blank')">
      &#9729; HISTORIAL CLOUD
    </button>
  </div>
</div>

<!-- ══ GRÁFICAS LOCALES ═════════════════════════════════════ -->
<div class="sec-label">Historial en la memoria del equipo</div>
<div class="win-info" id="winInfo">Cargando ventana de muestras&hellip;</div>
<div class="charts">
  <div class="chart-card">
    <div class="chart-title">Temperatura &deg;C &mdash; Neveras y Cuarto</div>
    <div class="chart-wrap"><canvas id="cT"></canvas></div>
  </div>
  <div class="chart-card">
    <div class="chart-title">Humedad % &mdash; Cuarto de Vacunas</div>
    <div class="chart-wrap"><canvas id="cH"></canvas></div>
  </div>
</div><!-- /.charts -->


<!-- ══ FOOTER ═══════════════════════════════════════════════ -->
<footer>
  Registro cada <span id="ivalF">--</span> min &nbsp;&middot;&nbsp;
  Panel actualizado cada 60 s &nbsp;&middot;&nbsp;
  Última lectura: <span id="lu">--</span>
  &nbsp;&middot;&nbsp;
  Nevera 1: <span>2&ndash;8 &deg;C</span>
  &nbsp;&middot;&nbsp;
  Nevera 2: <span>2&ndash;8 &deg;C</span>
  &nbsp;&middot;&nbsp;
  Cuarto: <span>18&ndash;25 &deg;C / 30&ndash;70 % HR</span>
  &nbsp;&middot;&nbsp;
  Una IPS
</footer>

</div><!-- /.wrap -->

<script>
// ── Logo (versión portafolio: genérico, no el logo real de la organización) ──
var LOGO ='data:image/svg+xml;base64,PHN2ZyB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciIHZpZXdCb3g9IjAgMCA2NCA2NCI+CjxjaXJjbGUgY3g9IjMyIiBjeT0iMzIiIHI9IjMwIiBmaWxsPSIjMWEyNTM1Ii8+CjxjaXJjbGUgY3g9IjMyIiBjeT0iMzIiIHI9IjMwIiBmaWxsPSJub25lIiBzdHJva2U9IiMwMEFFRUYiIHN0cm9rZS13aWR0aD0iMiIvPgo8cGF0aCBkPSJNMzIgMTQgTDQ2IDIwIFYzMiBDNDYgNDIgNDAgNDggMzIgNTEgQzI0IDQ4IDE4IDQyIDE4IDMyIFYyMCBaIiBmaWxsPSIjMDBBRUVGIiBvcGFjaXR5PSIwLjkiLz4KPHJlY3QgeD0iMjciIHk9IjI0IiB3aWR0aD0iMTAiIGhlaWdodD0iNCIgZmlsbD0iIzFhMjUzNSIvPgo8cmVjdCB4PSIzMCIgeT0iMjEiIHdpZHRoPSI0IiBoZWlnaHQ9IjEwIiBmaWxsPSIjMWEyNTM1Ii8+Cjwvc3ZnPgo=';
document.getElementById('logoImg').src = LOGO;

// ── v4.0: guarda ante fallo de CDN (Chart.js) ─────────────────
// Si la librería de gráficas no cargó (internet lento/caído),
// avisar y reintentar en vez de morir en silencio.
if (typeof Chart === 'undefined') {
  var cdnBanner = document.createElement('div');
  cdnBanner.style.cssText = 'background:#7f1d1d;color:#fff;padding:10px 14px;'+
    'text-align:center;font-size:.8rem;border-radius:8px;margin:10px 0';
  cdnBanner.textContent = 'No se pudo cargar la libreria de graficas '+
    '(requiere internet). Reintentando automaticamente en 8 s...';
  (document.querySelector('.wrap') || document.body).prepend(cdnBanner);
  setTimeout(function(){ location.reload(); }, 8000);
  throw new Error('Chart.js no disponible');
}

// ── Constantes y rangos normativos ───────────────────────────
var TITLE      = 'CENTINELA — Cuarto de Vacunas · Una IPS';
var SAMPLE_MIN = 1;

var N1_MIN    = 2,   N1_MAX    = 8;   // Nevera 1: vacunas
// Umbrales de verificacion de instrumentacion. Valores de respaldo:
// la fuente de verdad es el ESP32, que los envia en /api/data.
var DELTA_WARN = 0.5, DELTA_CRIT = 1.0;
var N2_MIN    = 2,   N2_MAX    = 8;   // Nevera 2: mismo rango que N1
var CRT_T_MIN = 18,  CRT_T_MAX = 25;   // cuarto: criterio institucional
var CRT_H_MIN = 30,  CRT_H_MAX = 70;

// Ventana del historial guardado en el equipo. El usuario elige
// cuantas MUESTRAS quiere ver; la equivalencia en tiempo se calcula
// con el intervalo real de registro y se muestra en la leyenda.
// El maximo (144) corresponde a 24 h con el intervalo de 10 min.
var WINDOWS     = [12, 24, 48, 72, 96, 144];
var HIST_MAX    = 144;    // capacidad real del equipo (llega en /api/data)
var activeWin   = 48;     // muestras seleccionadas
var activeRange = 48;     // muestras efectivas a graficar
var activeLabel = '48 muestras';
var allN1=[], allN2=[], allTE=[], allH=[];

// ── Opciones base Chart.js ────────────────────────────────────
var GC = 'rgba(208,220,233,.7)';
var GO = {
  responsive: true, maintainAspectRatio: false, animation:{duration:350},
  interaction:{mode:'index',intersect:false},
  plugins:{
    legend:{labels:{color:'#5c7085',font:{size:11},boxWidth:14,padding:10}},
    tooltip:{
      backgroundColor:'#1a2535',borderColor:'#2c3e50',borderWidth:1,
      titleColor:'#fff',bodyColor:'#94a3b8'}
  },
  scales:{
    x:{ticks:{color:'#7a8a99',maxTicksLimit:6,font:{size:10}},grid:{color:GC}},
    y:{ticks:{color:'#7a8a99',font:{size:10}},grid:{color:GC}}
  }
};

// ── Plugin: bandas de rango normativo sobre las gráficas ─────
// Dibuja franja del rango aceptable + limites punteados, para
// leer de un vistazo si la curva esta dentro de norma (SCADA).
Chart.register({id:'bands', beforeDatasetsDraw:function(ch){
  var bs = ch.config.options.bands; if(!bs) return;
  var y = ch.scales.y, a = ch.chartArea, ctx = ch.ctx;
  if(!y || !a) return;
  bs.forEach(function(b){
    var y1 = Math.max(y.getPixelForValue(b.max), a.top);
    var y2 = Math.min(y.getPixelForValue(b.min), a.bottom);
    if(y2 < a.top || y1 > a.bottom || y2 <= y1) return;
    ctx.save();
    ctx.fillStyle = b.fill;
    ctx.fillRect(a.left, y1, a.right - a.left, y2 - y1);
    ctx.strokeStyle = b.line; ctx.setLineDash([4,4]); ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.moveTo(a.left, y1); ctx.lineTo(a.right, y1);
    ctx.moveTo(a.left, y2); ctx.lineTo(a.right, y2);
    ctx.stroke();
    ctx.restore();
  });
}});
function withBands(bands){
  var o = Object.assign({}, GO); o.bands = bands; return o;
}

// ── Helpers ───────────────────────────────────────────────────
function mkL(n){
  var r=[], now=new Date();
  for(var i=0;i<n;i++){
    var t=new Date(now.getTime()-(n-1-i)*SAMPLE_MIN*60000);
    r.push(t.toLocaleTimeString('es',{hour:'2-digit',minute:'2-digit'}));
  }
  return r;
}
function tail(arr,n){ return arr.slice(-n); }

// ── Gráficas principales ──────────────────────────────────────
// Neveras: línea SÓLIDA (sin borderDash)
// Cuarto:  línea PUNTEADA (borderDash)
var cT = new Chart(document.getElementById('cT'),{
  type:'line',
  data:{labels:[],datasets:[
    {label:'Nevera 1 (DS18B20 #1)', data:[],
     borderColor:'#00AEEF', backgroundColor:'rgba(0,174,239,.07)',
     tension:.4, pointRadius:2, spanGaps:true},
    {label:'Nevera 2 (DS18B20 #2)', data:[],
     borderColor:'#6C63FF', backgroundColor:'rgba(108,99,255,.07)',
     tension:.4, pointRadius:2, spanGaps:true},
    {label:'Cuarto (DHT11)', data:[],
     borderColor:'#10B981', backgroundColor:'rgba(16,185,129,.07)',
     tension:.4, pointRadius:2, borderDash:[5,4], spanGaps:true}
  ]},
  options:withBands([
    // N1 y N2 comparten rango en esta etapa: una sola banda 2-8 C
    {min:2,   max:8,  fill:'rgba(0,174,239,.06)',  line:'rgba(231,76,60,.45)'},
    {min:18,  max:25, fill:'rgba(16,185,129,.05)', line:'rgba(16,185,129,.35)'}
  ])
});

var cH = new Chart(document.getElementById('cH'),{
  type:'line',
  data:{labels:[],datasets:[
    {label:'Humedad (DHT11)', data:[],
     borderColor:'#10B981', backgroundColor:'rgba(16,185,129,.09)',
     tension:.4, pointRadius:2, fill:true, borderDash:[5,4], spanGaps:true}
  ]},
  options:withBands([
    {min:30, max:70, fill:'rgba(16,185,129,.06)', line:'rgba(231,76,60,.45)'}
  ])
});

function renderCharts(){
  var n=Math.min(activeRange,Math.max(allN1.length,allN2.length,allTE.length,1));
  cT.data.labels           = mkL(n);
  cT.data.datasets[0].data = tail(allN1,n);
  cT.data.datasets[1].data = tail(allN2,n);
  cT.data.datasets[2].data = tail(allTE,n);
  cT.update();
  cH.data.labels           = mkL(Math.min(activeRange,Math.max(allH.length,1)));
  cH.data.datasets[0].data = tail(allH,activeRange);
  cH.update();
}

// Formatea una duracion en minutos de forma legible
function fmtDur(mins){
  if(mins < 60) return mins + ' min';
  var h = mins / 60;
  return (h % 1 === 0 ? h : h.toFixed(1)) + ' h';
}

// Muestras realmente almacenadas en el equipo en este momento
function availableSamples(){
  return Math.max(allN1.length, allN2.length, allTE.length, allH.length, 0);
}

function refreshWindowButtons(){
  WINDOWS.forEach(function(w){
    var b = document.getElementById('w' + w);
    if(!b) return;
    var ok = (w <= HIST_MAX);
    b.disabled = !ok;
    b.setAttribute('aria-pressed', (ok && w === activeWin) ? 'true' : 'false');
    if(ok) b.classList.toggle('active', w === activeWin);
    else   b.classList.remove('active');
    b.title = ok
      ? (w + ' muestras = ' + fmtDur(w * SAMPLE_MIN) + ' con intervalo de ' + SAMPLE_MIN + ' min')
      : ('No disponible: el equipo almacena ' + HIST_MAX + ' muestras como maximo');
  });
}

function applyWindow(){
  if(activeWin > HIST_MAX) activeWin = HIST_MAX;   // por si cambia la capacidad
  activeRange = activeWin;
  activeLabel = activeWin + ' muestras';
  refreshWindowButtons();
  renderCharts();
  updMeta();

  // Leyenda: distingue lo pedido de lo realmente disponible, para que
  // nadie interprete una grafica corta como perdida de datos.
  var disp = availableSamples();
  var mostradas = Math.min(activeWin, disp);
  var txt = 'Mostrando <b>' + mostradas + '</b> de <b>' + disp + '</b> muestras guardadas' +
            '<span class="sep-dot">&middot;</span>ventana <b>' + fmtDur(activeWin * SAMPLE_MIN) + '</b>' +
            '<span class="sep-dot">&middot;</span>intervalo <b>' + SAMPLE_MIN + ' min</b>' +
            '<span class="sep-dot">&middot;</span>capacidad <b>' + HIST_MAX + '</b> (' + fmtDur(HIST_MAX * SAMPLE_MIN) + ')';
  if(disp < activeWin){
    txt += '<span class="sep-dot">&middot;</span>aún acumulando datos';
  }
  var el = document.getElementById('winInfo');
  if(el) el.innerHTML = txt;
}

function setWindow(w){
  if(w > HIST_MAX) return;   // boton deshabilitado
  activeWin = w;
  applyWindow();
}

// ── Sistema de alarmas ────────────────────────────────────────
function checkNeveraAlerta(cardId, val, min, max){
  var c=document.getElementById(cardId);
  var fuera=(val!==null && !isNaN(val) && (val<min || val>max));
  if(fuera) c.classList.add('alerta');
  else      c.classList.remove('alerta');
  return fuera;
}

function checkCuartoAlerta(d){
  var c     = document.getElementById('cardCuarto');
  var badge = document.getElementById('cuartoBadge');
  var tVal  = d.tempExt, hVal = d.hum;
  var tFuera = (tVal!==null && !isNaN(tVal) && (tVal<CRT_T_MIN || tVal>CRT_T_MAX));
  var hFuera = (hVal!==null && !isNaN(hVal) && (hVal<CRT_H_MIN || hVal>CRT_H_MAX));
  if(tFuera || hFuera){
    c.classList.add('alerta');
    if(badge){
      if(tFuera && hFuera)
        badge.textContent = '⚠ TEMP Y HUMEDAD FUERA DE RANGO';
      else if(tFuera)
        badge.textContent = '⚠ TEMPERATURA FUERA DE RANGO (18–25°C)';
      else
        badge.textContent = '⚠ HUMEDAD FUERA DE RANGO (30–70 %)';
    }
  } else {
    c.classList.remove('alerta');
  }
  return (tFuera || hFuera);
}

// ── Helpers de estado / meta / tendencia ─────────────────────
var failCount = 0;

function st(id,v){ var e=document.getElementById(id); if(e) e.textContent=v; }

function setVal(id,html){
  var e=document.getElementById(id); if(!e) return;
  if(e.innerHTML!==html){
    e.innerHTML=html;
    e.classList.remove('tick'); void e.offsetWidth; e.classList.add('tick');
  }
}

function setBanner(kind,msg){
  var b=document.getElementById('sysBanner');
  if(!kind){ b.className=''; return; }
  document.getElementById('sysBannerTxt').textContent=msg;
  b.className='show '+kind;
}

function rngOf(arr,n){
  var a=tail(arr,n).filter(function(v){return v!==null&&!isNaN(v);});
  if(!a.length) return null;
  return {mn:Math.min.apply(null,a), mx:Math.max.apply(null,a)};
}

function trendOf(arr){
  var a=arr.filter(function(v){return v!==null&&!isNaN(v);});
  if(a.length<6) return 0;
  var l=a.slice(-3), p=a.slice(-6,-3);
  var m1=(l[0]+l[1]+l[2])/3, m0=(p[0]+p[1]+p[2])/3;
  if(m1-m0> .15) return 1;
  if(m0-m1> .15) return -1;
  return 0;
}

function setTrend(id,t){
  var e=document.getElementById(id); if(!e) return;
  e.innerHTML = t>0 ? '&#9650;' : (t<0 ? '&#9660;' : '&#9644;');
  e.className = 'trend'+(t===0?' eq':'');
}

function updMeta(){
  var r1=rngOf(allN1,activeRange), r2=rngOf(allN2,activeRange),
      rt=rngOf(allTE,activeRange), rh=rngOf(allH,activeRange);
  st('mnN1', r1?r1.mn.toFixed(1):'--'); st('mxN1', r1?r1.mx.toFixed(1):'--');
  st('mnN2', r2?r2.mn.toFixed(1):'--'); st('mxN2', r2?r2.mx.toFixed(1):'--');
  st('mnTE', rt?rt.mn.toFixed(1):'--'); st('mxTE', rt?rt.mx.toFixed(1):'--');
  st('mnH',  rh?rh.mn.toFixed(1):'--'); st('mxH',  rh?rh.mx.toFixed(1):'--');
  setTrend('trN1', trendOf(tail(allN1,activeRange)));
  setTrend('trN2', trendOf(tail(allN2,activeRange)));
  setTrend('trTE', trendOf(tail(allTE,activeRange)));
}

// ── Verificación de instrumentación (ΔT entre sondas) ────────
// La diferencia la calcula el ESP32; aqui solo se presenta con el
// semaforo. Los umbrales tambien llegan del equipo, de modo que
// cambiarlos en el firmware basta para que la UI se adapte sola.
function updDelta(d){
  if(typeof d.dWarn === 'number') DELTA_WARN = d.dWarn;
  if(typeof d.dCrit === 'number') DELTA_CRIT = d.dCrit;

  var chip = document.getElementById('dChip');
  var txt  = document.getElementById('dChipTxt');
  if(!chip || !txt) return;

  if(d.delta === null || d.delta === undefined || isNaN(d.delta)){
    setVal('vDelta', '--<span class="unit">&deg;C</span>');
    chip.className = 'dchip';
    txt.textContent = 'SIN DATOS';
    chip.title = 'Alguna sonda no tiene lectura valida';
    return;
  }

  setVal('vDelta', d.delta.toFixed(2) + '<span class="unit">&deg;C</span>');
  if(d.delta < DELTA_WARN){
    chip.className = 'dchip ok';   txt.textContent = 'CONSISTENTE';
    chip.title = 'Diferencia por debajo de ' + DELTA_WARN.toFixed(1) + ' °C: sondas concordantes';
  } else if(d.delta < DELTA_CRIT){
    chip.className = 'dchip warn'; txt.textContent = 'REVISAR';
    chip.title = 'Diferencia entre ' + DELTA_WARN.toFixed(1) + ' y ' + DELTA_CRIT.toFixed(1) + ' °C: vigilar';
  } else {
    chip.className = 'dchip crit'; txt.textContent = 'DISCREPANCIA';
    chip.title = 'Diferencia de ' + DELTA_CRIT.toFixed(1) + ' °C o mas: revisar instalacion o calibracion';
  }
}

// ── Fetch datos del ESP32 ─────────────────────────────────────
function upd(){
  fetch('/api/data')
    .then(function(r){ return r.json(); })
    .then(function(d){
      // Enlace panel <-> ESP32 recuperado
      failCount=0;
      document.querySelector('.cards').classList.remove('stale');

      allN1=d.histN1||[]; allN2=d.histN2||[];
      allTE=d.histTE||[]; allH =d.histH ||[];

      var u='<span class="unit">';
      setVal('vN1', d.tempN1!==null  ? d.tempN1.toFixed(1)+u+'&deg;C</span>' : 'ERR');
      setVal('vN2', d.tempN2!==null  ? d.tempN2.toFixed(1)+u+'&deg;C</span>' : 'ERR');
      setVal('vTE', d.tempExt!==null ? d.tempExt.toFixed(1)+u+'&deg;C</span>': 'ERR');
      setVal('vH',  d.hum!==null     ? d.hum.toFixed(1)+u+'%</span>'         : 'ERR');

      var a1=checkNeveraAlerta('cardN1', d.tempN1, N1_MIN, N1_MAX);
      var a2=checkNeveraAlerta('cardN2', d.tempN2, N2_MIN, N2_MAX);
      var ac=checkCuartoAlerta(d);
      updDelta(d);
      var errS=(d.tempN1===null)||(d.tempN2===null)||(d.tempExt===null)||(d.hum===null);

      // Banner de estado global
      if(a1||a2||ac){
        setBanner('crit','⚠ Alerta activa: valores fuera de rango — revisar equipos');
      } else if(errS){
        setBanner('warn','Sensor sin lectura (ERR) — verificar conexión del sensor');
      } else {
        setBanner(null);
      }

      // Estado WiFi — ONLINE en #07EB47 ya aplicado via CSS .badge
      var badge=document.getElementById('wifiBadge');
      var dot  =document.getElementById('wifiDot');
      var txt  =document.getElementById('wifiTxt');
      if(d.wifi){
        badge.className='badge'; dot.className='dot pulse'; txt.textContent='ONLINE';
      } else {
        badge.className='badge off'; dot.className='dot'; txt.textContent='OFFLINE';
      }

      // Intervalo y capacidad reales ANTES de dibujar: las ventanas
      // de historial y las etiquetas de tiempo dependen de ellos.
      if(d.histMax)  HIST_MAX   = d.histMax;
      if(d.interval) SAMPLE_MIN = d.interval;
      applyWindow();    // recalcula ventana, botones, gráficas, meta y leyenda

      var now=new Date();
      document.getElementById('lu').textContent=now.toLocaleTimeString('es');
      st('hdrUpd', now.toLocaleTimeString('es',{hour:'2-digit',minute:'2-digit'}));

      if(d.interval){
        var fI=document.getElementById('ivalF'); if(fI) fI.textContent=d.interval;
        var hI=document.getElementById('ivalH'); if(hI) hI.textContent=d.interval;
      }
    })
    .catch(function(e){
      console.error('[upd] fetch error',e);
      // Perdida de enlace con el panel: avisar y marcar datos obsoletos
      failCount++;
      if(failCount>=2){
        document.querySelector('.cards').classList.add('stale');
        var badge=document.getElementById('wifiBadge');
        var dot  =document.getElementById('wifiDot');
        var txt  =document.getElementById('wifiTxt');
        badge.className='badge off'; dot.className='dot'; txt.textContent='SIN ENLACE';
        setBanner('warn','Sin conexión con el panel — reintentando cada 60 s. '+
                  'Último dato: '+document.getElementById('lu').textContent);
      }
    });
}

// ── Exportar PNG local ────────────────────────────────────────
function exportPNG(){
  var w=cT.canvas.width+cH.canvas.width+24;
  var h=Math.max(cT.canvas.height,cH.canvas.height)+70;
  var cv=document.createElement('canvas'); cv.width=w; cv.height=h;
  var ctx=cv.getContext('2d');
  ctx.fillStyle='#f0f4f8'; ctx.fillRect(0,0,w,h);
  ctx.fillStyle='#1a2535'; ctx.font='bold 13px sans-serif';
  ctx.fillText(TITLE+' | '+new Date().toLocaleString('es'),10,20);
  ctx.drawImage(cT.canvas,0,36);
  ctx.drawImage(cH.canvas,cT.canvas.width+24,36);
  var a=document.createElement('a');
  a.href=cv.toDataURL('image/png');
  a.download='CENTINELA_'+new Date().toISOString().slice(0,16).replace(/[:T]/g,'-')+'.png';
  a.click();
}

// ── Exportar PDF local ────────────────────────────────────────
function exportPDF(){
  // v4.0: si jsPDF no cargó (CDN lento), recargarla y avisar
  if(!window.jspdf || !window.jspdf.jsPDF){
    alert('La libreria de exportacion PDF aun no ha cargado '+
          '(requiere internet). Se esta recargando: intenta de nuevo en unos segundos.');
    var s=document.createElement('script');
    s.src='https://cdnjs.cloudflare.com/ajax/libs/jspdf/2.5.1/jspdf.umd.min.js';
    document.head.appendChild(s);
    return;
  }
  var jsPDF=window.jspdf.jsPDF;
  var doc=new jsPDF({orientation:'landscape',unit:'mm',format:'a4'});
  doc.setFontSize(13); doc.setTextColor(26,37,53);
  doc.text(TITLE,14,13);
  doc.setFontSize(8); doc.setTextColor(107,127,146);
  doc.text('Generado: '+new Date().toLocaleString('es')+'   Historial: '+activeLabel,14,19);
  doc.addImage(cT.canvas.toDataURL('image/png'),'PNG', 14,26,130,70);
  doc.addImage(cH.canvas.toDataURL('image/png'),'PNG',152,26,125,70);
  doc.setFontSize(7); doc.setTextColor(60,60,60);
  doc.text('Resumen estadistico:',14,104);
  var rows=[
    ['Nevera 1 (DS18B20 #1)',allN1],
    ['Nevera 2 (DS18B20 #2)',allN2],
    ['Cuarto Temp (DHT11)',  allTE],
    ['Cuarto Hum  (DHT11)',  allH]
  ];
  var y=110;
  rows.forEach(function(r){
    var arr=r[1].filter(function(v){return v!==null&&!isNaN(v);});
    if(!arr.length){ doc.text(r[0]+': sin datos',14,y); }
    else{
      var mn=Math.min.apply(null,arr).toFixed(1);
      var mx=Math.max.apply(null,arr).toFixed(1);
      var avg=(arr.reduce(function(a,b){return a+b;},0)/arr.length).toFixed(1);
      doc.text(r[0]+':  min='+mn+'  max='+mx+'  prom='+avg,14,y);
    }
    y+=6;
  });
  doc.save('CENTINELA_'+new Date().toISOString().slice(0,16).replace(/[:T]/g,'-')+'.pdf');
}

// ── Arranque ──────────────────────────────────────────────────
upd();
setInterval(upd, 60000);
</script>
</body>
</html>
)rawhtml";