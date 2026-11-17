// climate.js — Tab „Klima": Temperatur- und Luftdruckverlauf aus envReport
// (heute Dome-C1 #21, BMP180). Periode + Ende frei waehlbar, Mittelung je Schritt
// serverseitig (/env?hours=&t1=&step= liefert Mittel + Min/Max je Schritt).
// Bedienung: Mausrad = Periode auf/zu (um die Zeit unter dem Mauszeiger),
// Klicken+Ziehen = Zeitfenster verschieben. Geladen wird je eine halbe Periode
// links/rechts mehr, damit beim Ziehen sofort Daten da sind; nachgeladen wird
// kurz nach der letzten Bewegung. Ende = „jetzt" laedt jede Minute nach.
// Gehoert zu index.html (setView ruft climShow/climHide).
"use strict";

const CLIM = {
  hours: 24,
  end: null,          // null = jetzt (laufend), sonst Unix-Sekunden
  data: null,         // /env-Antwort (etwas breiter als das sichtbare Fenster)
  timer: null,        // Minuten-Nachladen bei Ende = jetzt
  loadTimer: null,    // verzoegertes Nachladen nach Rad/Ziehen
  seq: 0,             // nur die neueste Antwort zaehlt
  hoverT: null,
  pan: null,          // {x0, end0, span, w}
  geo: {},
};
const CLIM_COL = ["#ffb347", "#5ec8ff", "#9be36b", "#e87bd8"];
const CLIM_STEPS = [60, 300, 900, 1800, 3600, 10800, 21600, 43200, 86400];
const CLIM_GAP_S = 600;            // Messabstand ~1/min; groessere Luecke = Linie unterbrechen
const CLIM_MIN_H = 0.25, CLIM_MAX_H = 24 * 400;

function climEl(id){ return document.getElementById(id); }
function climPad(n){ return String(n).padStart(2, "0"); }
function climNow(){ return Date.now() / 1000; }
function climFmt(t, withDate){
  const d = new Date(t * 1000);
  const hm = `${climPad(d.getHours())}:${climPad(d.getMinutes())}`;
  return withDate ? `${climPad(d.getDate())}.${climPad(d.getMonth()+1)}.${String(d.getFullYear()).slice(2)} ${hm}` : hm;
}
function climToLocalInput(t){
  const d = new Date(t * 1000);
  return `${d.getFullYear()}-${climPad(d.getMonth()+1)}-${climPad(d.getDate())}T${climPad(d.getHours())}:${climPad(d.getMinutes())}`;
}
function climNum(v, k){ return v == null || !isFinite(v) ? "–" : v.toFixed(k).replace(".", ","); }
function climWin(){ const t1 = CLIM.end == null ? climNow() : CLIM.end; return [t1 - CLIM.hours * 3600, t1]; }
function climSetEnd(t){ CLIM.end = t >= climNow() - 30 ? null : t; }
function climPerLabel(h){
  if(h >= 48) return `${climNum(h / 24, h / 24 < 10 ? 1 : 0)} Tage`;
  if(h >= 1)  return `${climNum(h, h < 10 ? 1 : 0)} h`;
  return `${Math.round(h * 60)} min`;
}

function climStep(){
  const sel = climEl("climStep").value;
  if(sel !== "auto") return +sel;
  const span = CLIM.hours * 3600;
  if(span <= 12 * 3600) return 0;                       // Rohwerte (~1/min) reichen
  const want = span / 700;
  return CLIM_STEPS.find(s => s >= want) || 86400;
}

function climSyncInputs(){
  const per = climEl("climPer");
  let opt = [...per.options].find(o => !o.dataset.custom && Math.abs(+o.value - CLIM.hours) < 1e-6);
  const custom = per.querySelector("option[data-custom]");
  if(opt){ if(custom) custom.remove(); }
  else{
    opt = custom || per.appendChild(document.createElement("option"));
    opt.dataset.custom = "1";
    opt.value = String(CLIM.hours);
    opt.textContent = climPerLabel(CLIM.hours);
  }
  per.value = opt.value;
  if(document.activeElement !== climEl("climEnd"))
    climEl("climEnd").value = climToLocalInput(climWin()[1]);
  climEl("climNow").classList.toggle("active", CLIM.end == null);
}

async function climLoad(){
  clearTimeout(CLIM.loadTimer); CLIM.loadTimer = null;
  const seq = ++CLIM.seq, step = climStep(), [, vt1] = climWin();
  // halbe Periode Rand links/rechts (rechts nie ueber jetzt hinaus)
  const ft1 = Math.min(climNow(), vt1 + CLIM.hours * 1800);
  const fh = (ft1 - (vt1 - CLIM.hours * 3600 - CLIM.hours * 1800)) / 3600;
  climEl("climInfo").textContent = "lädt …";
  try{
    const r = await fetch(`env?hours=${fh.toFixed(4)}&t1=${Math.round(ft1)}&step=${step}`);
    const d = await r.json();
    if(seq !== CLIM.seq) return;                       // inzwischen neuer Auftrag
    CLIM.data = d;
    climEl("climInfo").textContent =
      (step ? `Mittel je ${step >= 3600 ? climNum(step/3600, 0) + " h" : climNum(step/60, 0) + " min"}` : "Rohwerte") +
      (CLIM.end == null ? ` · Stand ${climFmt(climNow())}` : "");
  }catch(e){
    if(seq === CLIM.seq) climEl("climInfo").textContent = "Laden fehlgeschlagen: " + e;
  }
  climSyncInputs();
  climRender();
}
// sofort neu zeichnen (mit vorhandenen Daten), kurz danach nachladen
function climSoon(){
  climSyncInputs(); climRender();
  clearTimeout(CLIM.loadTimer);
  CLIM.loadTimer = setTimeout(climLoad, 250);
}

// ---- Zeichnen -----------------------------------------------------------------
function climNiceTicks(lo, hi, n){
  const raw = (hi - lo) / n, mag = Math.pow(10, Math.floor(Math.log10(raw)));
  const st = [1, 2, 2.5, 5, 10].map(m => m * mag).find(s => s >= raw);
  const out = [];
  for(let v = Math.ceil(lo / st) * st; v <= hi + 1e-9; v += st) out.push(+v.toFixed(6));
  return {ticks: out, step: st};
}
function climTimeTicks(t0, t1, px){
  const span = t1 - t0, want = span / Math.max(2, px / 90);
  const opts = [300, 600, 1800, 3600, 7200, 10800, 21600, 43200, 86400, 172800, 604800, 2592000];
  const st = opts.find(s => s >= want) || 2592000;
  const out = [];
  if(st >= 2592000){       // Monatsanfaenge
    const m = new Date(t0 * 1000);
    for(let k = 0; k < 40; k++){
      const x = new Date(m.getFullYear(), m.getMonth() + k, 1).getTime() / 1000;
      if(x > t1) break; if(x >= t0) out.push(x);
    }
    return {ticks: out, step: st};
  }
  // an lokaler Mitternacht ausrichten (Sommerzeit: ganze Tage per Datum)
  const d = new Date(t0 * 1000); d.setHours(0, 0, 0, 0);
  let t = d.getTime() / 1000;
  if(st >= 86400){
    for(let k = 0; k < 500; k++){
      const x = new Date(d.getFullYear(), d.getMonth(), d.getDate() + k * st / 86400).getTime() / 1000;
      if(x > t1) break; if(x >= t0) out.push(x);
    }
    return {ticks: out, step: st};
  }
  while(t < t0) t += st;
  for(; t <= t1; t += st) out.push(t);
  return {ticks: out, step: st};
}
// Index-Bereich [a,b] der Punkte im Fenster (ein Punkt Rand fuer die Linie)
function climRange(ts, t0, t1){
  let a = 0, b = ts.length - 1;
  while(a < ts.length && ts[a] < t0) a++;
  while(b >= 0 && ts[b] > t1) b--;
  return [a, b];
}

function climDrawChart(cid, key, kmin, kmax, unit){
  const cv = climEl(cid), dpr = window.devicePixelRatio || 1;
  const W = cv.clientWidth, H = cv.clientHeight;
  if(!W || !H) return;
  cv.width = W * dpr; cv.height = H * dpr;
  const g = cv.getContext("2d");
  g.setTransform(dpr, 0, 0, dpr, 0, 0);
  g.clearRect(0, 0, W, H);
  const L = 56, R = W - 12, T = 22, B = H - 26;
  const D = CLIM.data, env = (D && D.env) || {};
  const [t0, t1] = climWin();
  const band = climEl("climBand").checked;
  CLIM.geo[cid] = {L, R, T, B};
  let lo = Infinity, hi = -Infinity;
  for(const o of Object.values(env)){
    const [a, b] = climRange(o.t, t0, t1);
    const va = band && o[kmin] && o[kmin].length ? o[kmin] : o[key];
    const vb = band && o[kmax] && o[kmax].length ? o[kmax] : o[key];
    for(let k = a; k <= b; k++){ if(va[k] < lo) lo = va[k]; if(vb[k] > hi) hi = vb[k]; }
  }
  const title = key === "temp" ? "Temperatur" : "Luftdruck";
  g.font = "12px system-ui, sans-serif"; g.fillStyle = "#99a"; g.textAlign = "left";
  g.fillText(`${title} (${unit})`, L, 14);
  g.fillStyle = "#667"; g.textAlign = "center";
  g.fillText(`${climFmt(t0, true)} – ${climFmt(t1, true)}  (${climPerLabel(CLIM.hours)})`, (L + R) / 2, 14);
  if(!isFinite(lo)){
    g.fillStyle = "#666";
    g.fillText(D ? "keine Messwerte in diesem Zeitraum" : "lädt …", (L + R) / 2, (T + B) / 2);
    return;
  }
  const padv = Math.max((hi - lo) * 0.08, 0.3);
  lo -= padv; hi += padv;
  const yt = climNiceTicks(lo, hi, 5);
  const X = t => L + (t - t0) / (t1 - t0) * (R - L), Y = v => B - (v - lo) / (hi - lo) * (B - T);

  // Raster
  g.strokeStyle = "#24242c"; g.lineWidth = 1; g.fillStyle = "#778"; g.font = "11px ui-monospace, monospace";
  g.textAlign = "right";
  for(const v of yt.ticks){
    const y = Math.round(Y(v)) + 0.5;
    g.beginPath(); g.moveTo(L, y); g.lineTo(R, y); g.stroke();
    g.fillText(v.toFixed(yt.step < 1 ? (yt.step < 0.1 ? 2 : 1) : 0).replace(".", ","), L - 6, y + 4);
  }
  const xt = climTimeTicks(t0, t1, R - L);
  g.textAlign = "center";
  for(const t of xt.ticks){
    const x = Math.round(X(t)) + 0.5, d = new Date(t * 1000);
    const midnight = d.getHours() === 0 && d.getMinutes() === 0;
    g.strokeStyle = midnight ? "#3a3a48" : "#24242c";
    g.beginPath(); g.moveTo(x, T); g.lineTo(x, B); g.stroke();
    const lab = (xt.step >= 86400 || midnight) ? `${climPad(d.getDate())}.${climPad(d.getMonth()+1)}.` : climFmt(t);
    g.fillStyle = midnight ? "#aab" : "#778";
    g.fillText(lab, x, B + 16);
  }

  // Reihen (auf die Zeichenflaeche beschnitten)
  g.save();
  g.beginPath(); g.rect(L, T, R - L, B - T); g.clip();
  const gap = Math.max(CLIM_GAP_S, ((D && D.step) || 60) * 2.5);
  let ci = 0;
  for(const o of Object.values(env)){
    const col = CLIM_COL[ci++ % CLIM_COL.length], ts = o.t, vs = o[key];
    let [a, b] = climRange(ts, t0, t1);
    a = Math.max(0, a - 1); b = Math.min(ts.length - 1, b + 1);
    if(band && o[kmin] && o[kmin].length){
      g.fillStyle = col + "33";
      let i = a;
      while(i <= b){
        let j = i; while(j + 1 <= b && ts[j + 1] - ts[j] <= gap) j++;
        g.beginPath();
        for(let k = i; k <= j; k++) g.lineTo(X(ts[k]), Y(o[kmax][k]));
        for(let k = j; k >= i; k--) g.lineTo(X(ts[k]), Y(o[kmin][k]));
        g.closePath(); g.fill();
        i = j + 1;
      }
    }
    g.strokeStyle = col; g.lineWidth = 1.8; g.lineJoin = "round";
    g.beginPath();
    for(let k = a; k <= b; k++){
      if(k === a || ts[k] - ts[k - 1] > gap) g.moveTo(X(ts[k]), Y(vs[k]));
      else g.lineTo(X(ts[k]), Y(vs[k]));
    }
    g.stroke();
    if(b === a){ g.fillStyle = col; g.beginPath(); g.arc(X(ts[a]), Y(vs[a]), 3, 0, 7); g.fill(); }
  }
  // Fadenkreuz
  if(CLIM.hoverT != null && !CLIM.pan){
    const x = Math.round(X(CLIM.hoverT)) + 0.5;
    g.strokeStyle = "#889"; g.setLineDash([3, 3]);
    g.beginPath(); g.moveTo(x, T); g.lineTo(x, B); g.stroke(); g.setLineDash([]);
    ci = 0;
    for(const o of Object.values(env)){
      const k = climNearest(o.t, CLIM.hoverT), col = CLIM_COL[ci++ % CLIM_COL.length];
      if(k < 0) continue;
      g.fillStyle = col; g.beginPath(); g.arc(X(o.t[k]), Y(o[key][k]), 3.5, 0, 7); g.fill();
    }
  }
  g.restore();
  // Legende
  ci = 0; g.font = "12px system-ui, sans-serif"; g.textAlign = "right";
  for(const o of Object.values(env)){
    g.fillStyle = CLIM_COL[ci % CLIM_COL.length];
    g.fillText(o.name, R - ci * 110, 14); ci++;
  }
}

function climNearest(ts, t){
  if(!ts.length) return -1;
  let lo = 0, hi = ts.length - 1;
  while(hi - lo > 1){ const m = (lo + hi) >> 1; if(ts[m] < t) lo = m; else hi = m; }
  return Math.abs(ts[lo] - t) <= Math.abs(ts[hi] - t) ? lo : hi;
}

function climStats(){
  const box = climEl("climStats"), env = (CLIM.data && CLIM.data.env) || {};
  const [t0, t1] = climWin(), step = (CLIM.data && CLIM.data.step) || 0;
  const tiles = [], multi = Object.keys(env).length > 1;
  for(const o of Object.values(env)){
    const [a, b] = climRange(o.t, t0, t1);
    if(b < a) continue;
    const pre = multi ? o.name + " · " : "";
    const tmin = o.tmin && o.tmin.length ? o.tmin : o.temp, tmax = o.tmax && o.tmax.length ? o.tmax : o.temp;
    const pmin = o.pmin && o.pmin.length ? o.pmin : o.press, pmax = o.pmax && o.pmax.length ? o.pmax : o.press;
    let iMin = a, iMax = a, pl = Infinity, ph = -Infinity, sum = 0;
    for(let k = a; k <= b; k++){
      if(tmin[k] < tmin[iMin]) iMin = k;
      if(tmax[k] > tmax[iMax]) iMax = k;
      if(pmin[k] < pl) pl = pmin[k];
      if(pmax[k] > ph) ph = pmax[k];
      sum += o.temp[k];
    }
    const dP = o.press[b] - o.press[a];
    if(CLIM.end == null)
      tiles.push([`${climNum(o.temp[b], 1)} °C · ${climNum(o.press[b], 1)} hPa`,
                  `${pre}${step ? "letzter Schritt (Mittel)" : "aktuell"} (${climFmt(o.t[b])})`]);
    tiles.push([`${climNum(tmin[iMin], 1)} … ${climNum(tmax[iMax], 1)} °C`,
                `${pre}Temp. min ${climFmt(o.t[iMin], true)} · max ${climFmt(o.t[iMax], true)}`]);
    tiles.push([`${climNum(sum / (b - a + 1), 1)} °C`, `${pre}Temp. Mittel`]);
    tiles.push([`${climNum(pl, 1)} … ${climNum(ph, 1)} hPa`, `${pre}Druck min/max`]);
    tiles.push([`${dP > 0 ? "+" : ""}${climNum(dP, 1)} hPa`, `${pre}Druck Änderung im Fenster`]);
    if(CLIM.end == null && step <= 900)
      tiles.push([`${o.pressTrend3h > 0 ? "+" : ""}${climNum(o.pressTrend3h, 1)} hPa`,
                  `${pre}Druck 3 h ${Math.abs(o.pressTrend3h) > 1 ? "(Wetterwechsel)" : "(ruhig)"}`]);
  }
  box.textContent = "";
  for(const [v, l] of tiles){
    const d = document.createElement("div"); d.className = "climStat";
    const b = document.createElement("b"); b.textContent = v;
    const s = document.createElement("span"); s.textContent = l;
    d.append(b, s); box.appendChild(d);
  }
}

function climRender(){
  climDrawChart("climT", "temp", "tmin", "tmax", "°C");
  climDrawChart("climP", "press", "pmin", "pmax", "hPa");
  climStats();
}

function climTip(ev){
  const tip = climEl("climTip"), env = (CLIM.data && CLIM.data.env) || {};
  if(CLIM.hoverT == null || CLIM.pan){ tip.style.display = "none"; return; }
  const lines = [], multi = Object.keys(env).length > 1;
  let tt = null;
  for(const o of Object.values(env)){
    const k = climNearest(o.t, CLIM.hoverT); if(k < 0) continue;
    tt = o.t[k];
    let s = `${multi ? o.name + ": " : ""}${climNum(o.temp[k], 1)} °C · ${climNum(o.press[k], 1)} hPa`;
    if(o.tmin && o.tmin.length) s += `  (${climNum(o.tmin[k], 1)}…${climNum(o.tmax[k], 1)} °C)`;
    lines.push(s);
  }
  if(tt == null){ tip.style.display = "none"; return; }
  tip.textContent = climFmt(tt, true) + "\n" + lines.join("\n");
  tip.style.whiteSpace = "pre";
  const host = climEl("clim"), hr = host.getBoundingClientRect();
  tip.style.display = "block";
  const w = tip.offsetWidth;
  let x = ev.clientX - hr.left + host.scrollLeft + 14;
  if(x + w > hr.width - 8) x = ev.clientX - hr.left - w - 14;
  tip.style.left = x + "px";
  tip.style.top = (ev.clientY - hr.top + host.scrollTop + 12) + "px";
}

function climBindCanvas(cid){
  const cv = climEl(cid);
  cv.style.touchAction = "pan-y";          // waagrecht ziehen = verschieben, senkrecht = Seite scrollen
  const tAt = clientX => {
    const gm = CLIM.geo[cid]; if(!gm) return null;
    const [t0, t1] = climWin();
    const x = clientX - cv.getBoundingClientRect().left;
    return t0 + (Math.max(gm.L, Math.min(gm.R, x)) - gm.L) / (gm.R - gm.L) * (t1 - t0);
  };
  cv.addEventListener("pointerdown", ev => {
    if(ev.button !== 0) return;
    const gm = CLIM.geo[cid]; if(!gm) return;
    cv.setPointerCapture(ev.pointerId);
    CLIM.pan = {x0: ev.clientX, end0: climWin()[1], span: CLIM.hours * 3600, w: gm.R - gm.L, moved: false};
    cv.style.cursor = "grabbing";
    climEl("climTip").style.display = "none";
  });
  cv.addEventListener("pointermove", ev => {
    const p = CLIM.pan;
    if(p){
      const dx = ev.clientX - p.x0;
      if(Math.abs(dx) > 2) p.moved = true;
      climSetEnd(Math.min(climNow(), p.end0 - dx / p.w * p.span));
      climSoon();
      return;
    }
    CLIM.hoverT = tAt(ev.clientX);
    climRender(); climTip(ev);
  });
  const endPan = ev => {
    if(!CLIM.pan) return;
    const moved = CLIM.pan.moved;
    CLIM.pan = null; cv.style.cursor = "";
    if(moved) climLoad();
    else if(ev && ev.pointerType !== "mouse"){ CLIM.hoverT = tAt(ev.clientX); climRender(); climTip(ev); }
  };
  cv.addEventListener("pointerup", endPan);
  cv.addEventListener("pointercancel", endPan);
  cv.addEventListener("pointerleave", () => {
    if(CLIM.pan) return;
    CLIM.hoverT = null; climEl("climTip").style.display = "none"; climRender();
  });
  cv.addEventListener("wheel", ev => {
    ev.preventDefault();
    const tc = tAt(ev.clientX); if(tc == null) return;
    const [t0, t1] = climWin();
    const frac = (tc - t0) / (t1 - t0);
    const f = Math.pow(1.0015, ev.deltaY * (ev.deltaMode === 1 ? 33 : 1));   // Rad runter = Periode laenger
    const h = Math.min(CLIM_MAX_H, Math.max(CLIM_MIN_H, CLIM.hours * f));
    CLIM.hours = +h.toFixed(4);
    climSetEnd(Math.min(climNow(), tc + (1 - frac) * h * 3600));
    CLIM.hoverT = tc;
    climSoon(); climTip(ev);
  }, {passive: false});
}

function climShift(dir){
  climSetEnd(Math.min(climNow(), climWin()[1] + dir * CLIM.hours * 3600));
  climLoad();
}

function climShow(){
  climSyncInputs();
  climLoad();
  clearInterval(CLIM.timer);
  CLIM.timer = setInterval(() => { if(CLIM.end == null && !CLIM.pan && !document.hidden) climLoad(); }, 60000);
}
function climHide(){ clearInterval(CLIM.timer); CLIM.timer = null; }

(function climInit(){
  if(!climEl("clim")) return;
  climEl("climPer").addEventListener("change", e => { CLIM.hours = +e.target.value; climLoad(); });
  climEl("climEnd").addEventListener("change", e => {
    const t = new Date(e.target.value).getTime() / 1000;
    if(!isFinite(t)) return;
    climSetEnd(t); climLoad();
  });
  climEl("climPrev").addEventListener("click", () => climShift(-1));
  climEl("climNext").addEventListener("click", () => climShift(1));
  climEl("climNow").addEventListener("click", () => { CLIM.end = null; climLoad(); });
  climEl("climStep").addEventListener("change", climLoad);
  climEl("climBand").addEventListener("change", climRender);
  climBindCanvas("climT"); climBindCanvas("climP");
  window.addEventListener("resize", () => { if(climEl("clim").style.display !== "none") climRender(); });
})();
