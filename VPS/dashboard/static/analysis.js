// analysis.js — Analyse-Tab: Langzeit-Aufnahme durchscrollen, Modell-Overlay,
// Labeling, manuelle Track-Bewertung. Gehört zu index.html (nutzt dessen globale
// colorFor/fmtTime/escapeHtml/mapPoints). Serverseite: /rec /density /adata
// /amodel /aparams /alabel(s) /amark /devreload /acoverage.
// Zeiten sind Unix-Sekunden (Serverzeit, ms-genau).
//
// ZEITLEISTE: zeigt IMMER genau das gewählte Zeitfenster (kein Übersichts-
// Balken mit blauem Fenster mehr — der wurde nach mehreren Tagen Aufnahme
// unbrauchbar). Ziehen = verschieben, Mausrad = strecken/stauchen, "Alles" =
// ganze Aufnahme einpassen. Rote Punkte = Zeitpunkte, zu denen das Modell
// CatDetected auslösen würde.
"use strict";

const LBL_COLORS = {
  "Katze":"#2ecc71", "Einzelereignis":"#8a8f98", "Insekt":"#e67e22",
  "Vegetation":"#7a9e35", "Vogel":"#3ad0d0", "Sonne/Lidar":"#e6c522",
  "Regen/Sturm":"#3a7bd5", "Mäher":"#c0743a", "unbekannt":"#b06fd8"
};
// "Mäher"-Label = Analyse-Ausschluss: Events im Fenster bleiben aufgezeichnet und
// sichtbar (gut zum Ausleuchten der Erfassungsbereiche), aber das Modell
// überspringt sie (kein Tracking/CatDetected). Serverseitig in /amodel.
const ANA = {
  win: null,            // {t0,t1} betrachtetes Fenster = gesamte Zeitleiste
  dens: null,           // /density des Fensters (beim Pannen kurz veraltet, wird nachgeladen)
  data: null,           // /adata des Fensters
  model: null,          // /amodel des Fensters (inkl. marks = manuelle Bewertungen)
  labels: [],
  rec: null,
  modelOn: true,
  follow: false,
  showCov: false,       // Erfassungsbereiche (xComDef-Sektoren) einblenden
  coverage: null,       // /acoverage (Sektoren + Zellen)
  sensorOff: new Set(), // "sender.sensor" ausgeblendet
  view: null,           // Karten-Transform {cx,cy,s} (Welt-mm -> px)
  selKey: null,         // angeklickter Track (stabiler key) — gelb in Karte+Zeitleiste
  mergeSel: new Set(),  // Track-Nummern, die zum Zusammenkleben gewählt sind
  sel: null,            // Zeit-Selektion auf der Zeitleiste {t0,t1}
  fetchTimer: null,
  behindTimer: null,    // Auto-Refresh, solange der Analysierer noch nachrechnet
};
const SEL_COLOR = "#ffd23b";   // Hervorhebung des angeklickten Tracks

function devInfo(sid){
  const d = (ANA.model && ANA.model.devices) ? ANA.model.devices[String(sid)] : null;
  return d || {name:"#"+sid, type:""};
}

function senderKeys(sid){
  const keys = [];
  for(const e of (ANA.data?.events||[]))
    if(String(e.sender) === String(sid)){
      const k = e.sender + "." + e.sensor;
      if(!keys.includes(k)) keys.push(k);
    }
  return keys;
}

function anaEl(id){ return document.getElementById(id); }
function fmtDur(s){
  if(s < 120) return Math.round(s) + " s";
  if(s < 7200) return Math.round(s/60) + " min";
  if(s < 172800) return (s/3600).toFixed(1) + " h";
  return (s/86400).toFixed(1) + " Tage";
}
function fmtDT(t){
  const d = new Date(t*1000);
  return d.toLocaleDateString("de-CH",{day:"2-digit",month:"2-digit"}) + " " +
         d.toLocaleTimeString("de-CH");
}

// ------------------------------------------------------------- Daten holen

async function anaJson(url, opts){
  const r = await fetch(url, opts);
  if(!r.ok) throw new Error((await r.json().catch(()=>({}))).error || r.statusText);
  return r.json();
}

async function anaRefreshRec(){
  try{ ANA.rec = await anaJson("/rec"); }catch(e){ ANA.rec = null; }
  renderRec();
}

function updateSenderSelect(){
  // Sender-Auswahl fürs Labeln aktuell halten (neue Sensoren tauchen auf)
  const sel = anaEl("anaLblSender");
  if(!sel || !ANA.dens) return;
  const cur = sel.value;
  sel.innerHTML = `<option value="-1">alle Sensoren</option>` +
    Object.keys(ANA.dens.per_sender).map(s=>`<option value="${s}">nur #${s}</option>`).join("");
  sel.value = [...sel.options].some(o=>o.value===cur) ? cur : "-1";
}

function anaAll(){
  // ganze Aufnahme einpassen (Bereich aus /rec)
  const r = ANA.rec;
  if(!r || !r.t_min || !r.t_max || r.t_max <= r.t_min){
    const t1 = Date.now()/1000;
    ANA.win = {t0: t1 - 3600, t1};
  } else {
    const pad = Math.max((r.t_max - r.t_min) * 0.02, 1);
    ANA.win = {t0: r.t_min - pad, t1: r.t_max + pad};
  }
  ANA.follow = false;
  anaScheduleFetch();
}

// Gewähltes Zeitfenster über Sitzungen hinweg merken, damit beim Öffnen des
// Analyse-Tabs nicht mehr die (mit wachsender Aufnahme immer langsamere) ganze
// Aufnahme geladen wird, sondern genau das zuletzt betrachtete Fenster.
const ANA_WIN_KEY = "catfind.ana.win";
function anaSaveWin(){
  try{ if(ANA.win) localStorage.setItem(ANA_WIN_KEY, JSON.stringify(ANA.win)); }catch(e){}
}
function anaLoadWin(){
  try{
    const w = JSON.parse(localStorage.getItem(ANA_WIN_KEY) || "null");
    if(w && typeof w.t0==="number" && typeof w.t1==="number" && w.t1 > w.t0) return w;
  }catch(e){}
  return null;
}

function anaScheduleFetch(){
  clearTimeout(ANA.fetchTimer);
  ANA.fetchTimer = setTimeout(anaFetchWindow, 300);
  anaSaveWin();        // zuletzt gewähltes Fenster für den nächsten Aufruf merken
  drawTimeline();      // sofort nachführen (Dichte ggf. kurz veraltet)
  renderWinInfo();
}

async function anaFetchWindow(){
  if(!ANA.win) return;
  const q = `t0=${ANA.win.t0}&t1=${ANA.win.t1}`;
  try{ ANA.dens = await anaJson(`/density?${q}&bins=700`); }catch(e){ ANA.dens = null; }
  updateSenderSelect();
  try{ ANA.data = await anaJson(`/adata?${q}`); }catch(e){ ANA.data = {events:[],total:0,stride:1}; }
  if(ANA.modelOn){
    try{ ANA.model = await anaJson(`/amodel?${q}`); anaEl("anaModelErr").textContent=""; }
    catch(e){ ANA.model = null; anaEl("anaModelErr").textContent = "Modell: " + e.message; }
  } else ANA.model = null;
  try{ ANA.labels = (await anaJson(`/alabels?${q}`)).labels; }catch(e){ ANA.labels = []; }
  renderSensors(); renderTracks(); renderLabels(); drawAnaMap(); drawTimeline(); renderWinInfo();
  // Analysierer noch am Nachrechnen (Backlog)? -> in 5 s automatisch nachladen
  clearTimeout(ANA.behindTimer);
  if(ANA.model?.ana?.backlog && view === "ana")
    ANA.behindTimer = setTimeout(anaFetchWindow, 5000);
}

// ------------------------------------------------------------- Fenster-Navigation

function anaShift(frac){
  const w = ANA.win, dt = (w.t1 - w.t0) * frac;
  w.t0 += dt; w.t1 += dt; ANA.follow = false; anaScheduleFetch();
}
function anaZoomTime(f, center){
  const w = ANA.win, c = center ?? (w.t0 + w.t1) / 2;
  let half0 = (c - w.t0) * f, half1 = (w.t1 - c) * f;
  const span = half0 + half1;
  if(span < 2){ half0 *= 2/span; half1 *= 2/span; }             // min 2 s
  if(span > 45*86400){ half0 *= 45*86400/span; half1 *= 45*86400/span; }
  w.t0 = c - half0; w.t1 = c + half1; anaScheduleFetch();
}
function anaNow(){
  const w = ANA.win, span = w.t1 - w.t0, now = Date.now()/1000;
  w.t1 = now; w.t0 = now - span; anaScheduleFetch();
}

// ------------------------------------------------------------- Zeitleiste
// x-Achse = das gewählte Fenster ANA.win (nicht mehr die ganze Aufnahme)

function tlX(t, W){
  const w = ANA.win; if(!w) return 0;
  return (t - w.t0) / Math.max(w.t1 - w.t0, 1e-6) * W;
}
function tlT(x, W){
  const w = ANA.win;
  return w.t0 + x / W * (w.t1 - w.t0);
}

// "schöne" Zeit-Ticks fürs Fenster
function tlTicks(t0, t1){
  const steps = [1,2,5,10,15,30,60,120,300,600,900,1800,3600,2*3600,3*3600,
                 6*3600,12*3600,86400,2*86400,7*86400,14*86400,30*86400];
  const span = t1 - t0;
  const step = steps.find(s => span / s <= 9) || 30*86400;
  const ticks = [];
  for(let t = Math.ceil(t0/step)*step; t <= t1; t += step) ticks.push(t);
  return {step, ticks};
}
function tlTickLabel(t, step){
  const d = new Date(t*1000);
  if(step >= 86400)
    return d.toLocaleDateString("de-CH",{day:"2-digit",month:"2-digit"});
  const hm = d.toLocaleTimeString("de-CH",{hour:"2-digit",minute:"2-digit"});
  return step < 60 ? hm + ":" + String(d.getSeconds()).padStart(2,"0") : hm;
}

function drawTimeline(){
  const cv = anaEl("anaTl"); if(!cv || cv.offsetParent === null) return;
  const dpr = window.devicePixelRatio || 1;
  const W = cv.clientWidth, H = cv.clientHeight;
  cv.width = W*dpr; cv.height = H*dpr;
  const ctx = cv.getContext("2d"); ctx.setTransform(dpr,0,0,dpr,0,0);
  ctx.fillStyle = "#0c0c10"; ctx.fillRect(0,0,W,H);
  const w = ANA.win;
  if(!w){ ctx.fillStyle="#555"; ctx.fillText("kein Zeitfenster", 10, 20); return; }
  const densH = H - 21;                       // darunter: Aufnahme-Band + Label-Band

  // Zeit-Gitter
  const {step, ticks} = tlTicks(w.t0, w.t1);
  ctx.strokeStyle = "#1c1c24"; ctx.lineWidth = 1;
  ctx.fillStyle = "#667"; ctx.font = "10px system-ui";
  for(const t of ticks){
    const x = tlX(t, W);
    ctx.beginPath(); ctx.moveTo(x+.5, 12); ctx.lineTo(x+.5, densH); ctx.stroke();
    const lbl = tlTickLabel(t, step);
    ctx.fillText(lbl, Math.min(Math.max(x - ctx.measureText(lbl).width/2, 1), W-34), 10);
  }

  // Dichte je Sender gestapelt (log-Skala, damit Bursts die Nächte nicht plattdrücken).
  // d.t0/d.t1 = Fenster zum Zeitpunkt des Ladens -> beim Pannen verschieben sich
  // die Balken korrekt mit, bis die frischen Daten da sind.
  const d = ANA.dens;
  if(d && d.t1 > d.t0){
    const senders = Object.keys(d.per_sender);
    const totals = new Array(d.bins).fill(0);
    senders.forEach(s => d.per_sender[s].forEach((n,i)=> totals[i]+=n));
    const maxN = Math.max(1, ...totals);
    const bt = (d.t1 - d.t0) / d.bins;                 // Bin-Breite in Sekunden
    const bw = Math.max(bt / (w.t1 - w.t0) * W, 1);    // ... in Pixeln
    for(let i=0;i<d.bins;i++){
      if(!totals[i]) continue;
      const x = tlX(d.t0 + i*bt, W);
      if(x + bw < 0 || x > W) continue;
      const h = Math.max(2, Math.log(1+totals[i]) / Math.log(1+maxN) * (densH-16));
      let y = densH;
      for(const s of senders){
        const n = d.per_sender[s][i]; if(!n) continue;
        const hs = h * n / totals[i];
        ctx.fillStyle = colorFor(+s, 0);
        ctx.fillRect(x, y-hs, bw, hs);
        y -= hs;
      }
    }
    // verworfene Burst-Events (Manager-Drop-Zähler) als rote Marker oben
    ctx.fillStyle = "#f33";
    d.drops.forEach((n,i)=>{ if(n) ctx.fillRect(tlX(d.t0 + i*bt, W), 12, bw, 3); });
    // Aufnahme-Band: grün = aufgezeichnet, dunkel = Lücke (Pause/Ausfall)
    ctx.fillStyle = "#3a1518";
    ctx.fillRect(0, H-19, W, 4);
    ctx.fillStyle = "#2ecc71";
    for(const [a,b] of (d.spans||[])){
      const x0 = Math.max(0, tlX(a,W)), x1 = Math.min(W, tlX(b,W));
      if(x1 > x0) ctx.fillRect(x0, H-19, Math.max(x1-x0, 1.5), 4);
    }
  } else {
    ctx.fillStyle="#555"; ctx.fillText("keine Aufnahme-Daten im Fenster", 10, 24);
  }

  // Labels als Bänder unten
  for(const l of (ANA.labels||[])){
    ctx.fillStyle = (LBL_COLORS[l.label]||"#999") + "cc";
    const x0 = Math.max(0, tlX(l.t0,W)), x1 = Math.min(W, tlX(l.t1,W));
    if(x1 > 0 && x0 < W) ctx.fillRect(x0, H-14, Math.max(x1-x0,2), 12);
  }
  // Zeit-Selektion (Shift+Ziehen)
  if(ANA.sel){
    ctx.fillStyle = "#e6c52233";
    const x0 = tlX(ANA.sel.t0,W), x1 = tlX(ANA.sel.t1,W);
    ctx.fillRect(Math.min(x0,x1), 0, Math.abs(x1-x0), H);
    ctx.strokeStyle = "#e6c522"; ctx.strokeRect(Math.min(x0,x1)+.5, .5, Math.abs(x1-x0), H-1);
  }
  // GELBES Band: Zeitspanne des angeklickten Tracks (damit man weiss, wo man ist)
  const selTr = ANA.model?.tracks?.find(t=>t.key===ANA.selKey);
  if(selTr){
    const x0 = tlX(selTr.t0, W), x1 = tlX(selTr.t1, W);
    ctx.fillStyle = SEL_COLOR + "2e";
    ctx.fillRect(x0, 12, Math.max(x1-x0, 3), densH-12);
    ctx.strokeStyle = SEL_COLOR; ctx.lineWidth = 1.5;
    ctx.strokeRect(x0+.5, 12.5, Math.max(x1-x0, 3), densH-13);
  }
  // ROTE Punkte: Zeitpunkte, zu denen das Modell CatDetected auslösen würde
  if(ANA.model) for(const tr of ANA.model.tracks) if(tr.confirmed){
    const x = tlX(tr.t_confirm, W);
    if(x < -5 || x > W+5) continue;
    ctx.fillStyle = "#f43b3b";
    ctx.beginPath(); ctx.arc(x, densH-7, 4, 0, 7); ctx.fill();
    ctx.strokeStyle = "#fff8"; ctx.lineWidth = 1;
    ctx.beginPath(); ctx.arc(x, densH-7, 4, 0, 7); ctx.stroke();
  }
  // Fenstergrenzen als Beschriftung
  ctx.fillStyle = "#89a"; ctx.font = "10px system-ui";
  ctx.fillText(fmtDT(w.t0), 4, densH - 4);
  const s1 = fmtDT(w.t1);
  ctx.fillText(s1, W - ctx.measureText(s1).width - 4, densH - 4);
}

function tlBind(){
  const cv = anaEl("anaTl");
  let drag = null;
  cv.addEventListener("mousedown", e => {
    const W = cv.clientWidth, t = tlT(e.offsetX, W);
    if(e.shiftKey){ drag = {mode:"sel", start:t}; ANA.sel = {t0:t, t1:t}; }
    else drag = {mode:"pan", x:e.offsetX, t0:ANA.win.t0, t1:ANA.win.t1};
  });
  cv.addEventListener("mousemove", e => {
    if(!drag) return;
    const W = cv.clientWidth;
    if(drag.mode==="sel"){
      const t = tlT(e.offsetX, W);
      ANA.sel = {t0:Math.min(drag.start,t), t1:Math.max(drag.start,t)}; drawTimeline();
    } else {
      const dt = -(e.offsetX - drag.x) / W * (drag.t1 - drag.t0);
      ANA.win.t0 = drag.t0 + dt; ANA.win.t1 = drag.t1 + dt;
      ANA.follow = false; anaScheduleFetch();
    }
  });
  window.addEventListener("mouseup", ()=> drag=null);
  cv.addEventListener("wheel", e => {
    e.preventDefault();
    anaZoomTime(e.deltaY > 0 ? 1.3 : 1/1.3, tlT(e.offsetX, cv.clientWidth));
  }, {passive:false});
}

// ------------------------------------------------------------- Karte

function anaFit(){
  const xs=[], ys=[];
  (mapPoints||[]).forEach(p=>{ xs.push(p[0]*1000); ys.push(p[1]*1000); });
  (ANA.data?.events||[]).forEach(e=>{ if(e.wv===1){ xs.push(e.wx); ys.push(e.wy); } });
  if(xs.length < 2){ ANA.view = {cx:0, cy:5000, s:0.05}; return; }
  const minx=Math.min(...xs), maxx=Math.max(...xs), miny=Math.min(...ys), maxy=Math.max(...ys);
  const cv = anaEl("anaMap"), W = cv.clientWidth, H = cv.clientHeight;
  const s = Math.min(W/Math.max(maxx-minx,1000), H/Math.max(maxy-miny,1000)) * 0.9;
  ANA.view = {cx:(minx+maxx)/2, cy:(miny+maxy)/2, s};
}

// lokale Sensor-Koordinate -> Welt (wie localToWorld in xComProc; head in
// PA-Einheiten 0..4096, mirror ±1, Blickrichtung = lokale +y-Achse)
function secToWorld(sec, xl, yl){
  const a = sec.head * 2*Math.PI/4096, c = Math.cos(a), s = Math.sin(a);
  const ym = sec.mir * yl;
  return [sec.x + xl*c - ym*s, sec.y + xl*s + ym*c];
}

function drawAnaMap(){
  const cv = anaEl("anaMap"); if(!cv || cv.offsetParent === null) return;
  if(!ANA.view) anaFit();
  const dpr = window.devicePixelRatio || 1;
  const W = cv.clientWidth, H = cv.clientHeight;
  cv.width = W*dpr; cv.height = H*dpr;
  const ctx = cv.getContext("2d"); ctx.setTransform(dpr,0,0,dpr,0,0);
  ctx.fillStyle = "#0c0c10"; ctx.fillRect(0,0,W,H);
  const v = ANA.view;
  const X = wx => W/2 + (wx - v.cx) * v.s;
  const Y = wy => H/2 - (wy - v.cy) * v.s;
  // Gitter (1 m bzw. 5 m)
  const step = v.s*1000 > 25 ? 1000 : 5000;
  const wx0 = v.cx - W/2/v.s, wx1 = v.cx + W/2/v.s;
  const wy0 = v.cy - H/2/v.s, wy1 = v.cy + H/2/v.s;
  ctx.strokeStyle = "#1d1d24"; ctx.lineWidth = 1;
  for(let gx=Math.ceil(wx0/step)*step; gx<=wx1; gx+=step){ ctx.beginPath(); ctx.moveTo(X(gx),0); ctx.lineTo(X(gx),H); ctx.stroke(); }
  for(let gy=Math.ceil(wy0/step)*step; gy<=wy1; gy+=step){ ctx.beginPath(); ctx.moveTo(0,Y(gy)); ctx.lineTo(W,Y(gy)); ctx.stroke(); }
  // Erfassungsbereiche: Sektoren aus xComDef + Welt-Pose (Quelle "xComDef"),
  // sonst Fallback empirische Zellen
  if(ANA.showCov && ANA.coverage){
    const polys = ANA.coverage.polygons || {};
    if(Object.keys(polys).length){
      for(const [sid, poly] of Object.entries(polys)){
        const keys = senderKeys(sid);
        if(keys.length && !keys.some(k=>!ANA.sensorOff.has(k))) continue;
        if(!poly.length) continue;
        const col = colorFor(+sid, 0);
        ctx.beginPath();
        poly.forEach(([wx,wy],i)=> i ? ctx.lineTo(X(wx),Y(wy)) : ctx.moveTo(X(wx),Y(wy)));
        ctx.closePath();
        ctx.fillStyle = col; ctx.globalAlpha = 0.10; ctx.fill();
        ctx.globalAlpha = 0.70; ctx.strokeStyle = col; ctx.lineWidth = 2; ctx.stroke();
        ctx.globalAlpha = 1;
      }
    }
    const secs = ANA.coverage.sectors || {};
    if(!Object.keys(polys).length && Object.keys(secs).length){
      for(const [sid, sec] of Object.entries(secs)){
        const keys = senderKeys(sid);
        if(keys.length && !keys.some(k=>!ANA.sensorOff.has(k))) continue;
        const col = colorFor(+sid, 0);
        ctx.beginPath();
        const full = (sec.right - sec.left) >= 360;
        if(!full) ctx.moveTo(X(sec.x), Y(sec.y));
        const n = Math.max(24, Math.ceil((sec.right - sec.left) / 4));
        for(let i=0; i<=n; i++){
          const deg = sec.left + (sec.right - sec.left) * i / n;
          const rad = deg * Math.PI/180;
          const [wx, wy] = secToWorld(sec, Math.sin(rad)*sec.range, Math.cos(rad)*sec.range);
          (i===0 && full) ? ctx.moveTo(X(wx), Y(wy)) : ctx.lineTo(X(wx), Y(wy));
        }
        ctx.closePath();
        ctx.fillStyle = col; ctx.globalAlpha = 0.08; ctx.fill();
        ctx.globalAlpha = 0.55; ctx.strokeStyle = col; ctx.lineWidth = 1.5; ctx.stroke();
        ctx.globalAlpha = 1;
        // Sensor-Standort
        ctx.fillStyle = col;
        ctx.beginPath(); ctx.arc(X(sec.x), Y(sec.y), 4, 0, 7); ctx.fill();
      }
    } else {
      const cell = ANA.coverage.cell_mm, cw = Math.max(cell * v.s, 1);
      ctx.globalAlpha = 0.13;
      for(const [sid, cells] of Object.entries(ANA.coverage.senders)){
        const keys = senderKeys(sid);
        if(keys.length && !keys.some(k=>!ANA.sensorOff.has(k))) continue;
        ctx.fillStyle = colorFor(+sid, 0);
        for(const [cx,cy] of cells) ctx.fillRect(X(cx-cell/2), Y(cy+cell/2), cw, cw);
      }
      ctx.globalAlpha = 1;
    }
  }
  // RasenKarte
  if(mapPoints) { ctx.fillStyle="#556"; mapPoints.forEach(p=>ctx.fillRect(X(p[0]*1000)-1.5, Y(p[1]*1000)-1.5, 3, 3)); }
  // Events des Fensters (Alter im Fenster -> Helligkeit)
  const w = ANA.win, span = Math.max(w.t1-w.t0, 1e-6);
  for(const e of (ANA.data?.events||[])){
    if(e.wv !== 1) continue;
    if(ANA.sensorOff.has(e.sender + "." + e.sensor)) continue;
    const a = 0.25 + 0.75 * (e.t - w.t0) / span;
    ctx.globalAlpha = Math.max(0.15, Math.min(1, a));
    ctx.fillStyle = colorFor(e.sender, e.sensor);
    ctx.beginPath(); ctx.arc(X(e.wx), Y(e.wy), 2.5, 0, 7); ctx.fill();
  }
  ctx.globalAlpha = 1;
  // Modell-Tracks. Manuelle Bewertung: vom Modell bestätigte, aber von Hand als
  // "keine Katze" markierte Tracks orange (Differenz sichtbar machen).
  const marks = ANA.model?.marks || {};
  if(ANA.model) for(const tr of ANA.model.tracks){
    if(tr.pts.length < 2 && !tr.confirmed) continue;
    const hot = ANA.selKey === tr.key;
    const mk = marks[tr.key];
    const col = tr.confirmed ? ((mk && mk!=="cat") ? "#e67e22" : "#2ecc71")
                             : (mk==="cat" ? "#2ecc71" : "#888a");
    ctx.strokeStyle = col;
    ctx.lineWidth = hot ? 3.5 : (tr.confirmed ? 2 : 1);
    ctx.setLineDash(!tr.confirmed && mk==="cat" ? [5,4] : []);
    ctx.beginPath();
    tr.pts.forEach((p,i)=> i ? ctx.lineTo(X(p[1]),Y(p[2])) : ctx.moveTo(X(p[1]),Y(p[2])));
    ctx.stroke();
    ctx.setLineDash([]);
    if(tr.confirmed){
      // ROTER Punkt = Ort/Zeitpunkt, an dem CatDetected ausgelöst würde
      // (bevor die Katze den Erfassungsbereich verlässt)
      const cp = tr.pts.reduce((b,p)=> Math.abs(p[0]-tr.t_confirm) < Math.abs(b[0]-tr.t_confirm) ? p : b, tr.pts[0]);
      ctx.fillStyle = "#f43b3b";
      ctx.beginPath(); ctx.arc(X(cp[1]), Y(cp[2]), 6, 0, 7); ctx.fill();
      ctx.strokeStyle = "#fff"; ctx.lineWidth = 1.5;
      ctx.beginPath(); ctx.arc(X(cp[1]), Y(cp[2]), 6, 0, 7); ctx.stroke();
      ctx.fillStyle = col; ctx.font = "bold 11px system-ui";
      const tag = (tr.flags||[]).includes("STATIONAER") ? " · sitzt!" : "";
      ctx.fillText("CatDetected " + tr.score + tag, X(cp[1]) + 12, Y(cp[2]) - 6);
    }
  }
  // angeklickter Track nochmals OBENDRAUF in Gelb (Karte + Zeitleiste zeigen so,
  // wo man gerade ist) — mit Start (○) und Ende (●)
  const selTr = ANA.model?.tracks?.find(t=>t.key===ANA.selKey);
  if(selTr && selTr.pts.length){
    ctx.strokeStyle = SEL_COLOR; ctx.lineWidth = 4; ctx.globalAlpha = 0.9;
    ctx.beginPath();
    selTr.pts.forEach((p,i)=> i ? ctx.lineTo(X(p[1]),Y(p[2])) : ctx.moveTo(X(p[1]),Y(p[2])));
    ctx.stroke();
    ctx.globalAlpha = 1;
    const p0 = selTr.pts[0], p1 = selTr.pts[selTr.pts.length-1];
    ctx.strokeStyle = SEL_COLOR; ctx.lineWidth = 2.5;
    ctx.beginPath(); ctx.arc(X(p0[1]), Y(p0[2]), 6, 0, 7); ctx.stroke();
    ctx.fillStyle = SEL_COLOR;
    ctx.beginPath(); ctx.arc(X(p1[1]), Y(p1[2]), 6, 0, 7); ctx.fill();
    ctx.font = "bold 11px system-ui";
    ctx.fillText("Nr " + (selTr.id ?? "neu"), X(p0[1]) + 9, Y(p0[2]) - 8);
  }
  // Massstab
  ctx.fillStyle = "#778"; ctx.font = "11px system-ui";
  ctx.fillText((step/1000) + " m Raster", 8, H-8);
}

function mapBind(){
  const cv = anaEl("anaMap");
  let drag = null;
  cv.addEventListener("mousedown", e => { drag = {x:e.offsetX, y:e.offsetY}; });
  cv.addEventListener("mousemove", e => {
    if(!drag) return;
    ANA.view.cx -= (e.offsetX - drag.x) / ANA.view.s;
    ANA.view.cy += (e.offsetY - drag.y) / ANA.view.s;
    drag = {x:e.offsetX, y:e.offsetY};
    drawAnaMap();
  });
  window.addEventListener("mouseup", ()=> drag=null);
  cv.addEventListener("wheel", e => {
    e.preventDefault();
    const f = e.deltaY > 0 ? 1/1.25 : 1.25;
    const v = ANA.view, W = cv.clientWidth, H = cv.clientHeight;
    const wx = v.cx + (e.offsetX - W/2)/v.s, wy = v.cy - (e.offsetY - H/2)/v.s;
    v.s *= f;
    v.cx = wx - (e.offsetX - W/2)/v.s;
    v.cy = wy + (e.offsetY - H/2)/v.s;
    drawAnaMap();
  }, {passive:false});
  cv.addEventListener("dblclick", ()=>{ anaFit(); drawAnaMap(); });
}

// ------------------------------------------------------------- Panels

function renderRec(){
  const el = anaEl("anaRec"); if(!el) return;
  const warn = anaEl("anaRecWarn");
  const r = ANA.rec;
  if(!r){ el.innerHTML = "<span style='color:#c66'>keine Verbindung</span>"; return; }
  const mb = (r.bytes/1048576).toFixed(1);
  const info = `<span style="color:#888;font-size:12px">${r.rows.toLocaleString("de-CH")} Events · ${mb} MB` +
               (r.t_min ? ` · seit ${fmtDT(r.t_min)}` : "") + `</span>`;
  el.innerHTML = r.on
    ? `<button class="sw on" id="anaRecBtn" title="klicken = Aufnahme pausieren">● Aufnahme läuft</button> ${info}`
    : `<button class="sw off" id="anaRecBtn" title="klicken = Aufnahme starten (Append)">⏸ AUFNAHME AUS — Klick startet</button> ${info}`;
  if(warn) warn.style.display = r.on ? "none" : "block";
  anaEl("anaRecBtn").onclick = async ()=>{
    if(r.on && !confirm("Aufnahme wirklich PAUSIEREN?\nEs werden dann keine Events mehr gespeichert."))
      return;
    ANA.rec = await anaJson("/rec", {method:"POST",
      headers:{"Content-Type":"application/json"}, body:JSON.stringify({on: r.on?0:1})});
    renderRec(); anaFetchWindow();
  };
}

function renderWinInfo(){
  const el = anaEl("anaWinInfo"); if(!el || !ANA.win) return;
  const span = ANA.win.t1 - ANA.win.t0;
  const tot = ANA.data ? ` · ${ANA.data.total.toLocaleString("de-CH")} Events` +
              (ANA.data.stride>1 ? ` (1/${ANA.data.stride} gezeigt)` : "") : "";
  el.textContent = `${fmtDT(ANA.win.t0)} — ${fmtDT(ANA.win.t1)} (${fmtDur(span)})${tot}`;
}

function renderSensors(){
  const el = anaEl("anaSensors"); if(!el) return;
  const cnt = {};
  for(const e of (ANA.data?.events||[])){
    const k = e.sender + "." + e.sensor;
    cnt[k] = cnt[k] || {n:0, nw:0, sender:e.sender, sensor:e.sensor};
    cnt[k].n++; if(e.wv===1) cnt[k].nw++;
  }
  const storms = ANA.model?.storms || {};
  el.innerHTML = Object.keys(cnt).sort().map(k=>{
    const c = cnt[k], off = ANA.sensorOff.has(k);
    const st = (storms[String(c.sender)]||[]).length ? " ⚡" : "";
    const d = devInfo(c.sender);
    const cov = d.covRange ? ` · ${d.covL}°…${d.covR}° ${(d.covRange/1000).toFixed(0)}m` : "";
    return `<label class="anaSens${off?' off':''}" data-k="${k}" title="${escapeHtml(d.name)} (${escapeHtml(d.type)})${cov}">
      <input type="checkbox" ${off?'':'checked'}>
      <i style="background:${colorFor(c.sender,c.sensor)}"></i>${escapeHtml(d.name)}.${c.sensor}${st}
      <small>${c.n}${c.nw<c.n?` (${c.n-c.nw} o.Welt)`:''}</small></label>`;
  }).join("") || "<div class='hint'>— keine Events im Fenster —</div>";
  el.querySelectorAll(".anaSens input").forEach(inp=>{
    inp.onchange = e=>{
      const k = e.target.closest(".anaSens").dataset.k;
      e.target.checked ? ANA.sensorOff.delete(k) : ANA.sensorOff.add(k);
      drawAnaMap();
    };
  });
}

const FLAG_TXT = {STATIONAER:"sitzt! (koten?)", OFFEN:"läuft noch", EINTRITT:"Eintritt am Rand",
                  AUSTRITT:"Austritt am Rand", FUSION:"mehrere Sensoren sehen dasselbe Objekt"};
// kompakte, IMMER sichtbare Flag-Chips in der Track-Zeile (Tooltip = FLAG_TXT)
const FLAG_CHIP = {STATIONAER:"⏺ sitzt", OFFEN:"… offen", EINTRITT:"→ rein",
                   AUSTRITT:"raus →", FUSION:"⧉ 2+ Sensoren"};
// manuelle Track-Bewertung: was war es wirklich? (alles ausser "cat" = keine Katze)
const MARKS = {cat:"🐱 Katze", person:"🧍 Person", bird:"🐦 Vogel",
               mower:"🤖 Mäher", insect:"🦗 Insekt", storm:"⛈ Sturm",
               nocat:"✕ Störung", surenocat:"🚫 sicher keine Katze"};
const MARK_ICON = {cat:"🐱", person:"🧍", bird:"🐦", mower:"🤖",
                   insect:"🦗", storm:"⛈", nocat:"✕", surenocat:"🚫"};

// manuelle Bewertung eines Tracks setzen/entfernen (Klick auf gesetzten Haken = entfernen)
async function anaSetMark(tr, mark){
  const marks = ANA.model.marks = ANA.model.marks || {};
  const next = marks[tr.key] === mark ? "" : mark;
  try{
    await anaJson("/amark",{method:"POST",headers:{"Content-Type":"application/json"},
      body:JSON.stringify({key:tr.key, t0:tr.t0, t1:tr.t1, mark:next})});
    if(next) marks[tr.key] = next; else delete marks[tr.key];
    renderTracks(); drawAnaMap();
  }catch(e){ alert("Bewertung speichern fehlgeschlagen: " + e.message); }
}

// zwei oder mehr Tracks zusammenkleben (gehören zum selben Tier)
async function anaMerge(){
  try{
    await anaJson("/amerge",{method:"POST",headers:{"Content-Type":"application/json"},
      body:JSON.stringify({ids:[...ANA.mergeSel]})});
    ANA.mergeSel.clear();
    anaFetchWindow();
  }catch(e){ alert("Kleben fehlgeschlagen: " + e.message); }
}
async function anaUnmerge(mergeId){
  try{
    await anaJson("/aunmerge",{method:"POST",headers:{"Content-Type":"application/json"},
      body:JSON.stringify({merge_id:mergeId})});
    anaFetchWindow();
  }catch(e){ alert("Lösen fehlgeschlagen: " + e.message); }
}

function renderTracks(){
  const el = anaEl("anaTracks"); if(!el) return;
  if(!ANA.model){ el.innerHTML = "<div class='hint'>— Modell aus / kein Ergebnis —</div>"; return; }
  const marks = ANA.model.marks || {};
  const all = ANA.model.tracks.filter(t=>t.n>=2 || t.confirmed);
  const trs = all.slice()
              .sort((a,b)=>(b.confirmed-a.confirmed) || (b.score-a.score) || (b.n-a.n)).slice(0,80);
  // Übereinstimmung Modell vs. Mensch: ohne Markierung gilt "einverstanden";
  // jede Markierung ausser "cat" (Person/Vogel/…/Sturm/Insekt) = "keine Katze".
  let diffNoCat = 0, diffCat = 0;
  for(const t of all){
    const mk = marks[t.key];
    if(t.confirmed && mk && mk !== "cat") diffNoCat++;   // Modell sagt Katze, ich nicht
    if(!t.confirmed && mk === "cat") diffCat++;          // ich sage Katze, Modell nicht
  }
  const agree = all.length - diffNoCat - diffCat;
  const agreeTxt = all.length
    ? `<div class="hint">Übereinstimmung ${agree}/${all.length}` +
      (diffNoCat ? ` · <span style="color:#e67e22">${diffNoCat}× Modell-Katze abgelehnt</span>` : "") +
      (diffCat   ? ` · <span style="color:#3ad0d0">${diffCat}× Katze übersehen</span>` : "") + `</div>`
    : "";
  const excl = ANA.model.n_excluded
    ? `<div class="hint" style="color:#c0743a">${ANA.model.n_excluded} Events in „Mäher"-Fenstern von der Analyse ausgeschlossen</div>`
    : "";
  const edge = ANA.model.edge_active
    ? (ANA.model.cov_source==="empirisch"
        ? `<div class="hint" style="color:#c93">Abdeckung noch empirisch — keine Sensor-Posen bekannt (Knopf „Geräte ⟳")</div>`
        : (ANA.model.cov_source==="polygon" || ANA.model.cov_source==="mixed")
          ? `<div class="hint" style="color:#7fe0a4">Abdeckung: ${escapeHtml(ANA.model.cov_source)} (gelernte Polygone/Default-Polygone)</div>` : "")
    : `<div class="hint" style="color:#c93">Randlogik inaktiv — keine Abdeckungsdaten</div>`;
  const ana = ANA.model.ana || {};
  const behind = ana.backlog
    ? `<div class="hint" style="color:#e6c522">⏳ Analysierer rechnet nach — fertig bis ${ana.done_t?fmtDT(ana.done_t):"…"}</div>` : "";
  const trunc = ana.truncated
    ? `<div class="hint">${ana.truncated} unwichtige Tracks im Riesenfenster ausgeblendet</div>` : "";
  const mergeBar = ANA.mergeSel.size
    ? `<div class="mergeBar">🔗 ${ANA.mergeSel.size} gewählt
        <button id="anaMergeGo"${ANA.mergeSel.size<2?" disabled":""}>zusammenkleben</button>
        <button id="anaMergeClr" title="Auswahl aufheben">✕</button></div>` : "";
  el.innerHTML = `<div class="hint">${ANA.model.n_confirmed}× CatDetected / ${ANA.model.tracks.length} Tracks</div>`
    + behind + trunc + agreeTxt + excl + edge + mergeBar +
    trs.map(t=>{
      const mk = marks[t.key];
      const sel = ANA.selKey===t.key;
      const chips = (t.flags||[]).map(f=>
        `<span class="flg" title="${escapeHtml(FLAG_TXT[f]||f)}">${escapeHtml(FLAG_CHIP[f]||f)}</span>`).join("");
      const dis = (t.confirmed && mk && mk!=="cat") || (!t.confirmed && mk==="cat");
      const badge = mk ? `<span class="mkBadge" title="meine Bewertung: ${escapeHtml(MARKS[mk]||mk)}">✋${MARK_ICON[mk]||"?"}</span>` : "";
      const num = t.id!=null ? "Nr "+t.id : "läuft…";
      const glue = t.members
        ? `<span class="flg" title="Klebung aus ${t.members.map(m=>"Nr "+m.id).join(", ")}">🔗×${t.members.length}</span>` : "";
      const chk = t.id!=null
        ? `<input type="checkbox" class="mrgChk" title="zum Zusammenkleben auswählen"${ANA.mergeSel.has(t.id)?" checked":""}>` : "";
      let det;
      if(sel){
        const sd = (t.score_detail||[]).map(d=>`${d[1]>0?'+':''}${d[1]} ${escapeHtml(d[0])}`).join("<br>");
        det = `<small>Aufnahme: ${fmtDT(t.t0)} → ${fmtDT(t.t1)}</small>` +
              (sd ? `<small>${sd}</small>` : "") +
              (t.members ? `<div class="mkRow"><button class="mk" data-unmerge="${t.merge_id}">🔗 Klebung lösen</button></div>` : "") +
              `<div class="mkRow">` +
                Object.entries(MARKS).map(([k,txt])=>
                  `<button class="mk${mk===k?' on':''}" data-mk="${k}" title="meine Einschätzung: das war ${escapeHtml(txt)} (nochmal klicken = entfernen)">${txt}</button>`).join("") +
              `</div>`;
      } else {
        det = t.confirmed ? "" : `<small>${escapeHtml(t.reject||"")}</small>`;
      }
      return `<div class="anaTrack${t.confirmed?' cat':''}${sel?' sel':''}${dis?' dis':''}" data-key="${t.key}">
      ${chk}${t.confirmed?'🐱':'·'} <b>${num}</b> · ${t.score}P${badge}${glue} n=${t.n} · ${fmtDur(t.t1-t.t0)} · ${(t.net_mm/1000).toFixed(1)}m
      ${t.v_mean? '· '+(t.v_mean/1000).toFixed(1)+'m/s':''} ${chips}
      ${det}</div>`;
    }).join("");
  const go = el.querySelector("#anaMergeGo"); if(go) go.onclick = anaMerge;
  const clr = el.querySelector("#anaMergeClr");
  if(clr) clr.onclick = ()=>{ ANA.mergeSel.clear(); renderTracks(); };
  el.querySelectorAll(".anaTrack").forEach(d=>{
    const key = d.dataset.key;
    const tr = ANA.model.tracks.find(t=>t.key===key);
    const chk = d.querySelector(".mrgChk");
    if(chk) chk.onclick = (ev)=>{
      ev.stopPropagation();
      chk.checked ? ANA.mergeSel.add(tr.id) : ANA.mergeSel.delete(tr.id);
      renderTracks();
    };
    d.onclick = (ev)=>{
      const mkBtn = ev.target.closest(".mk");
      if(mkBtn && tr){
        ev.stopPropagation();
        if(mkBtn.dataset.unmerge !== undefined){ anaUnmerge(+mkBtn.dataset.unmerge); return; }
        anaSetMark(tr, mkBtn.dataset.mk); return;
      }
      ANA.selKey = (ANA.selKey===key) ? null : key;
      if(tr && ANA.selKey){
        ANA.view.cx = tr.pts.reduce((s,p)=>s+p[1],0)/tr.pts.length;
        ANA.view.cy = tr.pts.reduce((s,p)=>s+p[2],0)/tr.pts.length;
      }
      renderTracks(); drawAnaMap(); drawTimeline();
    };
  });
}

function renderLabels(){
  const el = anaEl("anaLabels"); if(!el) return;
  el.innerHTML = (ANA.labels||[]).map(l=>
    `<div class="anaLbl"><i style="background:${LBL_COLORS[l.label]||'#999'}"></i>
     ${escapeHtml(l.label)} <small>${fmtDT(l.t0)} · ${fmtDur(l.t1-l.t0)}${l.sender>=0?` · #${l.sender}`:''}</small>
     <button data-id="${l.id}">✕</button></div>`).join("") || "<div class='hint'>— keine Labels im Fenster —</div>";
  el.querySelectorAll(".anaLbl button").forEach(b=>{
    b.onclick = async ()=>{
      await anaJson("/alabel_del",{method:"POST",headers:{"Content-Type":"application/json"},
                    body:JSON.stringify({id:+b.dataset.id})});
      anaFetchWindow();
    };
  });
}

async function anaAddLabel(){
  const label = anaEl("anaLblSel").value;
  const sender = +anaEl("anaLblSender").value;
  const r = ANA.sel || ANA.win;
  await anaJson("/alabel",{method:"POST",headers:{"Content-Type":"application/json"},
    body:JSON.stringify({t0:r.t0, t1:r.t1, sender, label})});
  ANA.sel = null;
  anaFetchWindow();
}


async function anaCoverageLearn(){
  const r = ANA.sel || ANA.win;
  const sid = +(prompt("Sensor-ID für Coverage lernen (Mäherfenster)", anaEl("anaLblSender")?.value !== "-1" ? anaEl("anaLblSender").value : "1") || -1);
  if(sid < 0 || !r) return;
  const qs = {sender:sid, t0:r.t0, t1:r.t1, angle_bin_deg:10, quantile:0.90, target_vertices:8, save:false};
  try{
    const prev = await anaJson("/coverage_learn", {method:"POST",headers:{"Content-Type":"application/json"},body:JSON.stringify(qs)});
    const q = prev.quality || {};
    if(!confirm(`Coverage-Vorschau #${sid}: ${prev.polygon.length} Eckpunkte, ${q.point_count||prev.point_count} Punkte, Fläche ${(q.area_mm2/1e6).toFixed(1)} m². Speichern?`)) return;
    qs.save = true;
    await anaJson("/coverage_learn", {method:"POST",headers:{"Content-Type":"application/json"},body:JSON.stringify(qs)});
    ANA.coverage = await anaJson("/acoverage");
    anaFetchWindow();
  }catch(e){ alert("Coverage lernen fehlgeschlagen: " + e.message); }
}
async function anaCoverageDelete(){
  const sid = +(prompt("Sensor-ID des gelernten Coverage-Polygons löschen/deaktivieren", "1") || -1);
  if(sid < 0) return;
  if(!confirm(`Coverage-Polygon für Sensor #${sid} deaktivieren?`)) return;
  await anaJson("/coverage_delete", {method:"POST",headers:{"Content-Type":"application/json"},body:JSON.stringify({sender:sid})});
  ANA.coverage = await anaJson("/acoverage");
  anaFetchWindow();
}

// ------------------------------------------------------------- Geräte neu laden

async function anaDevReload(){
  // xComDef (Typen + Erfassungsbereiche) sofort neu aus dem Repo lesen und den
  // Manager um frische Welt-Posen bitten. Danach Abdeckung + Modell neu holen.
  const btn = anaEl("anaBDev");
  btn.disabled = true; btn.textContent = "lädt…";
  try{
    const r = await anaJson("/devreload", {method:"POST"});
    ANA.coverage = null;
    if(ANA.showCov){
      try{ ANA.coverage = await anaJson("/acoverage"); }catch(e){ ANA.coverage = null; }
    }
    anaFetchWindow();
    btn.textContent = `Geräte ⟳ (${Object.keys(r.devices||{}).length} · ${r.poses} Posen)`;
    setTimeout(()=>{ btn.textContent = "Geräte ⟳"; }, 4000);
  }catch(e){
    btn.textContent = "Geräte ⟳ (Fehler)";
    setTimeout(()=>{ btn.textContent = "Geräte ⟳"; }, 4000);
  }
  btn.disabled = false;
}

// ------------------------------------------------------------- Parameter

async function anaParams(){
  const ov = anaEl("anaParamsOv");
  const p = await anaJson("/aparams");
  anaEl("anaParamsTxt").value = JSON.stringify(p, null, 2);
  ov.style.display = "flex";
  anaEl("anaParamsSave").onclick = async ()=>{
    try{
      const parsed = JSON.parse(anaEl("anaParamsTxt").value);
      await anaJson("/aparams",{method:"POST",headers:{"Content-Type":"application/json"},
                    body:JSON.stringify(parsed)});
      ov.style.display = "none";
      anaFetchWindow();
    }catch(e){ alert("Ungültig: " + e.message); }
  };
  anaEl("anaParamsClose").onclick = ()=> ov.style.display = "none";
}

// ------------------------------------------------------------- Modell-Check
// Modell gegen ALLE von Hand bewerteten Tracks der ganzen Aufnahme prüfen —
// die Trackliste (inkl. Bewertungen) ist so separat nutzbar (auch /atracks.csv).

async function anaValidate(){
  const ov = anaEl("anaValidOv"), txt = anaEl("anaValidTxt");
  ov.style.display = "flex"; txt.innerHTML = "rechnet…";
  anaEl("anaValidClose").onclick = ()=> ov.style.display = "none";
  try{
    const r = await anaJson("/avalidate");
    const pm = Object.entries(r.per_mark).map(([k,v])=>
      `<tr><td>${MARKS[k]||k}</td><td>${v.n}</td><td>${v.model_cat}</td></tr>`).join("");
    const mm = r.mismatches.map(m=>
      `<tr class="vmm" data-t0="${m.t0}" data-t1="${m.t1}" data-key="${m.key}">
        <td>Nr ${m.id}</td><td>${fmtDT(m.t0)}</td><td>${MARKS[m.mark]||m.mark}</td>
        <td>${m.confirmed ? "🐱 Katze ("+m.score+"P)" : escapeHtml(m.reject||"keine Katze")}</td></tr>`).join("");
    const quote = r.n_marked ? (100*r.agree/r.n_marked).toFixed(0) : "—";
    txt.innerHTML =
      `<p>${r.n_tracks} Tracks insgesamt, <b>${r.n_marked}</b> von Hand bewertet.</p>
       <p>Modell vs. Mensch auf den bewerteten: <b>✔ ${r.agree}</b> übereinstimmend
          (${quote} %) · <span style="color:#e67e22">${r.false_cat}× fälschlich Katze</span>
          · <span style="color:#3ad0d0">${r.missed_cat}× Katze verpasst</span></p>
       <table><tr><th>Bewertung</th><th>Anzahl</th><th>davon Modell „Katze"</th></tr>${pm}</table>` +
      (mm ? `<h4>Abweichungen (klicken = hinspringen)</h4>
             <table><tr><th>Track</th><th>Zeit</th><th>Mensch</th><th>Modell</th></tr>${mm}</table>`
          : `<p>keine Abweichungen 🎉</p>`);
    txt.querySelectorAll(".vmm").forEach(row=>{
      row.onclick = ()=>{
        const t0 = +row.dataset.t0, t1 = +row.dataset.t1;
        const pad = Math.max((t1 - t0) * 0.5, 20);
        ANA.win = {t0: t0 - pad, t1: t1 + pad};
        ANA.selKey = row.dataset.key;
        ANA.follow = false;
        ov.style.display = "none";
        anaScheduleFetch();
      };
    });
  }catch(e){ txt.innerHTML = `<span style="color:#c66">${escapeHtml(e.message)}</span>`; }
}

// ------------------------------------------------------------- Einstieg

let anaInit = false;
async function anaShow(){
  if(!anaInit){
    anaInit = true;
    tlBind(); mapBind();
    anaEl("anaB1").onclick = ()=>anaShift(-0.8);
    anaEl("anaB2").onclick = ()=>anaShift(-0.25);
    anaEl("anaB3").onclick = ()=>anaShift(0.25);
    anaEl("anaB4").onclick = ()=>anaShift(0.8);
    anaEl("anaZi").onclick = ()=>anaZoomTime(1/1.6);
    anaEl("anaZo").onclick = ()=>anaZoomTime(1.6);
    anaEl("anaBNow").onclick = anaNow;
    anaEl("anaBAll").onclick = anaAll;
    anaEl("anaModelChk").onchange = e=>{ ANA.modelOn = e.target.checked; anaFetchWindow(); };
    anaEl("anaCovChk").onchange = async e=>{
      ANA.showCov = e.target.checked;
      if(ANA.showCov && !ANA.coverage){
        try{ ANA.coverage = await anaJson("/acoverage"); }catch(err){ ANA.coverage = null; }
      }
      drawAnaMap();
    };
    anaEl("anaFollowChk").onchange = e=>{ ANA.follow = e.target.checked; if(ANA.follow) anaNow(); };
    anaEl("anaBParams").onclick = anaParams;
    anaEl("anaBValid").onclick = anaValidate;
    anaEl("anaBDev").onclick = anaDevReload;
    anaEl("anaBLabel").onclick = anaAddLabel;
    anaEl("anaLblSel").innerHTML = Object.keys(LBL_COLORS).map(l=>`<option>${l}</option>`).join("");
    window.addEventListener("resize", ()=>{ if(view==="ana"){ drawTimeline(); drawAnaMap(); } });
    setInterval(async ()=>{
      if(view!=="ana") return;
      await anaRefreshRec();
      if(ANA.follow) anaNow();
    }, 5000);
  }
  await anaRefreshRec();
  if(!ANA.win){
    // Start: zuletzt gewähltes Fenster wiederherstellen (schnell), sonst die
    // ganze Aufnahme einpassen (langsam bei viel Daten).
    const saved = anaLoadWin();
    if(saved){ ANA.win = saved; ANA.follow = false; anaFetchWindow(); }
    else anaAll();
  }
  else anaFetchWindow();
}
